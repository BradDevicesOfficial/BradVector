# BradDev Kit — reference developer toolchain (BRADCVT_DRIVERS.md §6)

One binary, one toolchain — the Kit unifies the §6 tool table into a
single `braddev` command built on the real shipped stack (bradc → .bvbc
→ BVRT → BradTimeline/bradgdb) plus the reference drivers.

```sh
# build (strace-line gcc; -lm needed by BVRT sqrtf):
gcc -std=gnu23 -Wall -Wextra -I../include -I../bradvector \
  braddev.c ../bradvector/bradc.c ../bradvector/bvbc.c ../bradvector/bvrt.c \
  ../bradvector/bradgdb.c ../bradvector/bradtimeline.c ../bradvector/bradlib.c \
  ../drivers/*.c -lm -o braddev

braddev version
braddev gpu info                  # device profile (real constants)

# assemble, then run saxpy: x=1024 y=2048 a=4.0f count=8 one=1 step=4
braddev cvt examples/saxpy.bvbs -o /tmp/saxpy.bvbc -d
braddev run /tmp/saxpy.bvbc --arg 1024 --arg 2048 --arg 0x40800000 \
          --arg 8 --arg 1 --arg 4 --dump S3 M2044 M2048

braddev gpu top /tmp/saxpy.bvbc --arg 1024 --arg 2048 --arg 0x40800000 \
          --arg 8 --arg 1 --arg 4
braddev timeline /tmp/saxpy.bvbc --arg 1024 --arg 2048 --arg 0x40800000 \
          --arg 8 --arg 1 --arg 4 -o /tmp/trace.csv

# fib: break on the IADD (pc 4) and inspect S3; fib(8)=21 lands in S1
braddev cvt examples/fib.bvbs -o /tmp/fib.bvbc
braddev dbg /tmp/fib.bvbc -b 4 --arg 0 --arg 1 --arg 8 --arg 1 --arg 0 \
          --dump S1 S3 S21
braddev test                       # self-test: drivers + toolchain pipeline
```

| Subcommand | §6 tool | What it really is |
|------------|---------|-------------------|
| `cvt` | Brad-CVT CLI | assembles `.bvbs` → `.bvbc` (bradc). `.cu` is the [Gen1] target — no fake CUDA front-end |
| `gpu info` | `brad-gpu info` | device profile from `bradvector.h` constants |
| `gpu top` | `brad-gpu top` | single-sample perf snapshot via BradTimeline |
| `run`, `dbg` | launch + debugger | BVRT + bradgdb breakpoints, S-register dumps |
| `timeline` | `brad-gpu top`/profiler | per-kernel cycle/insn profile + CSV export |
| `test` | — | self-test that links SPMP / EROE / Fabric drivers + the whole toolchain pipeline |

## Reference ISA notes (learned from the real toolchain)

- **S0 is a read-only zero register** (writes discarded); branch conditions
  test predicates, e.g. `CMP P0, Sx, S0` + `BR_COND@P0 fin` for `x==0`.
- `MOV` and all ALUs except `IADD/ISUB/IMUL` are **float-domain** (raw
  u32s reinterpret as float — feeding small ints like `1` hits denormals).
  Use `ADDi Sd, S0, N` to build exact `N.0f` bits.
- Keep **integers in the integer ops**: addresses/counters via `MOV`/`IADD`/
  `ISUB` on `S` regs; `I/IS/IM` take registers only — the *only* seed for
  integer constants is a launch arg (S16..S31).
- So: addresses/counters = integer domain, data = float domain. That
  split is exactly what `examples/saxpy.bvbs` exercises.
- `--arg` values may be decimal or `0x` hex; `--dump` also accepts `M<addr>`
  to read device memory words.

## Honest boundaries

- **Reference implementation.** CPU + `cpu:x86_64` would run cross; on
  the target the same BVRT boot is a thin layer. This Kit is a contract
  sample — the *binary you ship today* is a receipt that the pipeline is
  real, and the kernel code that runs through it is the code that runs
  on silicon.
- **`.cu` translation is [Gen1]** — Brad-CVT's promise (CUDA source in,
  `.bvbc` out) is genuine, but there is no proprietary CUDA front-end on
  this machine; `cvt` accepts `.bvbs`.
- **`brad-gpu top` is batch, not live** — reference profiler, single sample.
- **The drivers are host-side demos** — the test subcommand proves the
  toolchain links them; it does not claim they control silicon.

## Layout

- `braddev.c` — the CLI (single TU, includes podman-free string parsing)
- `examples/` — `.bvbs` smoke kernels (saxpy, fib)
- `CMakeLists.txt` — `braddev` target, links `brad-vector` + drivers