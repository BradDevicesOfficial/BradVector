# BVLibs — BradVector Math / Neural / Communication Libraries

Reference libraries for the BradVector platform. Correctness-first, built on
the shipped reference stack (`bradc` → `.bvbc` → `BVRT` → `bradlib`) plus the
`BradFusion` fabric reference for collectives.

> **Status.** BVML (`bvml_saxpy`, `bvml_dot`, `bvml_gemm`), BVN
> (`bvn_relu`, `bvn_affine`,
> `bvn_softmax`, `bvn_gelu`, `bvn_silu`) and BVComm
> (`bvcomm_allreduce_sum_f32`, `bvcomm_broadcast_f32`,
> `bvcomm_allgather_f32`) are kernel/fabric-backed and verified against
> independent host references. Nothing here is a performance claim: BVRT is a
> single-warp reference interpreter and the fabric is a host-simulated mesh.
>
> **Published.** The public boundary of this suite — the BVML/BVN API headers,
> the `.bvbs` kernels, the shared tests, and an independent host-side
> reference implementation — is shipped as `BradDevicesOfficial/BVLibs` (MIT)
> and runs in the brand site's in-browser wasm. The ISA-level reference
> runtime (`bradc`, `BVRT`) and the fabric-bound `BVComm` stay in this private
> repo.
>
> The reference ISA's special-function opcodes (`SQRT`, `RSQRT`, `RCP`,
> `SIN`, `COS`, `EXP2`, `LOG2`) are now implemented in BVRT and the
> assembler, which is what makes the exp-based activations possible.
> `bvn_gelu` uses the sigmoid approximation `x/(1+exp(-1.702x))`; exact
> `erf`-based GELU would need an `ERF` opcode.

## Layout

```
bvlibs/
  include/bvml.h        BVML host API (math)
  include/bvn.h         BVN host API (neural)
  include/bvcomm.h      BVComm host API (communication)
  src/bvml.c            BVML implementation
  src/bvn.c             BVN implementation
  src/bvcomm.c          BVComm implementation (over brad/drivers fabric)
  kernels/*.bvbs        BradVector assembly kernels (source of truth)
  kernels/*_kernels.h.in      generated header templates
  tests/test_bvml.c     BVML host-reference checks (n = 1, 7, 64, 100; 8 gemm shapes)
  tests/test_bvn.c      BVN host-reference checks  (n = 1, 7, 64, 100)
  tests/test_bvcomm.c   BVComm checks (n ranks 1..4, ring all-reduce)
```

CMake reads the `.bvbs` files and generates the kernel headers, so kernels are
compiled into each library with no runtime file dependency and no duplicated
source. Editing a `.bvbs` triggers reconfigure automatically.

## Build & test

```
cmake -S src -B build -DBRAD_BUILD_BVLIB=ON
cmake --build build --target test_bvml test_bvn test_bvcomm
cd build && ctest -R "bvml|bvn|bvcomm" --output-on-failure
```

## API

```c
/* BVML — math */
struct bvml_context *c = bvml_open(1u << 20);
bvml_saxpy(c, a, x, y, out, n);   /* out[i] = a*x[i] + y[i] */
bvml_dot(c, x, y, &d, n);         /* d = sum x[i]*y[i]        */
bvml_gemm(c, alpha, A, B, beta, C, M, N, K); /* C = alpha*A·B + beta*C */
bvml_close(c);

/* BVN — neural */
struct bvn_context *n = bvn_open(1u << 20);
bvn_relu(n, x, out, count);        /* out[i] = max(0, x[i])          */
bvn_affine(n, scale, bias, x, out, count); /* out[i] = scale*x[i]+bias */
bvn_softmax(n, x, out, count);     /* stable softmax                 */
bvn_gelu(n, x, out, count);        /* x / (1 + exp(-1.702*x))        */
bvn_silu(n, x, out, count);        /* x / (1 + exp(-x))              */
bvn_close(n);

/* BVComm — communication (f32 collectives over the fabric) */
struct bvcomm_world *w = bvcomm_init(nranks, buf_bytes, scratch_bytes);
float *local = bvcomm_buf(w, rank);          /* rank-local buffer       */
bvcomm_allreduce_sum_f32(w, 0, count);       /* sum into every rank     */
bvcomm_broadcast_f32(w, root, 0, count);     /* root -> every rank      */
bvcomm_allgather_f32(w, in, out, count);     /* gather into out region  */
bvcomm_destroy(w);
```

`*_open`/`*_init` return NULL on failure. Ops return `0` on success, `-1` bad
args, `-2` buffer/scratch too small, `-3` launch failure, `-4` non-halting
run.

## How BVComm works

Each rank's buffer is bound into a fabric node address window
(`fabric_bind_memory`); collectives move bytes through `fabric_dma` between
windows, so traffic accrues the same link latency the mesh model reports.
All-reduce is a ring reduce-scatter + all-gather (the canonical collective
topology), using the world's scratch region as a per-rank inbox. The fabric
is process-global, so one world per process.

## Tooling integration

- **braddev** (`src/braddev`, built when `BRAD_BUILD_BVLIB` is on): `braddev lib test`
  runs a BVML/BVN self-check, and `braddev test` folds the same check into its
  Kit self-test output.
- **WASM bridge** (`src/wasm/build.sh`): `/assets/bradvector.wasm` compiles `bvml.c` +
  `bvn.c` freestanding (no libc/libm — `wasm_shim.c` provides the
  transcendental approximations the reference runtime needs for
  `EXP2`/`LOG2`/`SIN`/`COS`).
  Exports: `brad_wasm_bvml_saxpy`, `brad_wasm_bvml_dot`, `brad_wasm_bvml_gemm`,
  `brad_wasm_bvn_relu`, `brad_wasm_bvn_affine`, `brad_wasm_bvn_softmax`, `brad_wasm_bvn_gelu`,
  `brad_wasm_bvn_silu`, `brad_wasm_lib_check`. The JS wrapper exposes them as
  `BradVector.lib.*` in `site/js/bradvector.js`. Note the shim's runtime math is
  polynomial (~1e-6 accuracy), so any PR on the SFU ops should be validated
  against libm, not through the wasm (a kernel fault there is not a substitute
  for an independent reference).
- CMake generates the kernel headers at configure time; editing a `.bvbs` file
  bumps `CMAKE_CONFIGURE_DEPENDS` so the build re-generates them (otherwise a
  stale binary would hide kernel edits).

## Notes for kernel authors

- The reference runtime executes one 32-lane warp; Phase 0 kernels evaluate
  the operation per lane over the same addresses (correct, not yet
  lane-partitioned).
- `bradc`'s label table is shared across all kernels in one assembly unit, so
  **label names must be unique across kernels** (hence `dloop`/`ddone`,
  `rloop`/`rdone`, `aloop`/`adone`).
- Addresses flow as raw `u32` values; integer pointer arithmetic uses
  `IADD`/`ISUB`, while data uses the float ALU (`ADD`/`MUL`/`MAD`/`MAX`).
- `CMP` only supports EQ; compare a countdown against `S0` (zero) to loop.