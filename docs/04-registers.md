# Registers

BradVector has three register files per shader core: scalar, vector, and predicate. All are visible to the ISA; there is no hidden register renaming at the architectural level.

## Scalar registers — S0–S255

256 × 32-bit scalar registers, addressable by every instruction.

| Register | Special purpose |
|---|---|
| S0 | zero — reads as 0, writes discarded |
| S1–S15 | thread ID, block ID, cluster ID (read-only) |
| S16–S31 | kernel arguments |
| S32–S255 | general purpose |

The special-purpose block (S1–S15) lets a kernel discover exactly where it runs — the SIMT equivalent of `CORE_ID` / `CLUSTER_ID` on the CPU side.

## Vector registers — V0–V63

64 × 512-bit vector registers, each holding **16 × 32-bit lanes**.

| Register | Special purpose |
|---|---|
| V0 | zero vector |
| V1–V63 | general purpose |

Vector instructions (`VADD`, `VMUL`, `VMAD`, `VDOT`, `VREDUCE`, `VSHUFFLE`, …) operate lane-wise across the 16 lanes. `VBROADCAST` replicates a scalar into all lanes; `VSHUFFLE` permutes lanes; `VREDUCE` collapses a vector to a scalar (sum / max / min).

### Lane layout

```
V0 (512 bits)
┌────────┬────────┬────────┬─────┬────────┐
│ lane 0 │ lane 1 │ lane 2 │ ... │ lane 15│
│ 32 bit │ 32 bit │ 32 bit │     │ 32 bit │
└────────┴────────┴────────┴─────┴────────┘
```

Lane `i` occupies bits `[32i+31 : 32i]`. Little-endian within the register.

## Predicate registers — P0–P15

16 predicate registers, each 32 bits wide (one bit per thread in a warp). Predicates drive predicated execution and `BR_COND`.

| Condition | Meaning |
|---|---|
| `Px.N` | negative |
| `Px.Z` | zero |
| `Px.C` | carry |
| `Px.V` | overflow |
| `Px.GT` | greater than |
| `Px.LT` | less than |
| `Px.EQ` | equal |
| `Px.NE` | not equal |

Predicates are set by `CMP` and consumed by any instruction's `PRED` field or by `BR_COND`. They are the primary mechanism for per-lane control flow.

## Per-thread register file

The ISA's 256 scalar + 64 vector registers are the **per-thread** view. Inside a shader cluster, the hardware allocates these from a shared pool:

| Resource | Max per cluster |
|---|---|
| Threads | 1024 (32 warps) |
| Scalar registers | 8192 (shared across warps) |
| Vector registers | 2048 |
| Shared memory | 64 KB |
| Barriers | 16 |

Occupancy is limited by whichever resource runs out first. A kernel that uses all 256 scalar registers can run fewer concurrent threads than one that uses 64.

## Kernel arguments

The first four kernel arguments arrive in `S16`–`S19` by convention. A pointer-based kernel (e.g. `vadd`) receives its buffer addresses there and computes per-thread addresses from `LANE_ID`:

```asm
LANE_ID  S20              ; lane index
SHL      S21, S20, 2      ; byte offset (×4)
ADD      S22, S16, S21    ; &A[lane] from arg base S16
```

Kernels with more arguments read them from the constant / descriptor tables in the shader binary.

## Special values and identities

| Value | Meaning |
|---|---|
| S0 | hardwired zero |
| V0 | zero vector |
| `LANE_ID` | current lane index within the warp (opcode `0xF6`) |
| `WARP_SZ` | warp size (opcode `0xF7`) |
| `CLOCK` | cycle counter (opcode `0xF8`) |

These give a kernel everything it needs to address its own lane of work without external setup.

## Register width and data types

Registers are untyped 32-bit words; interpretation is per-instruction:

| Type | Where |
|---|---|
| FP32 | `ADD`, `MUL`, `MAD`, `DIV`, `SQRT`, `CMP`, `SEL` |
| INT32 | `IADD`, `IMUL`, `IDIV`, `SHL`, `SHR`, `AND`, `POPC`, `CLZ` |
| vector (16 × FP32) | `VADD`, `VMUL`, `VMAD`, `VDOT`, `VREDUCE` |
| matrix (4×4 / 8×8) | `MMUL`, `MMUL_B` |

A 64-bit value uses two adjacent scalar registers (`SRCn`, `SRCn+1`), consistent with the CPU-side convention of pairing words.

## Comparison with BradISA registers

| | BradISA (CPU) | BradVector (GPU) |
|---|---|---|
| Scalar GPRs | 16 × 32-bit | 256 × 32-bit |
| Vector regs | 32 × 256-bit (VSET) | 64 × 512-bit |
| Predicates | none (branch on register) | 16 × 32-bit |
| Zero register | r0 | S0, V0 |
| Thread identity | CORE_ID / CLUSTER_ID MSRs | S1–S15 |

The CPU has a small register file tuned for scalar code; the GPU has a large, wide file tuned for thousands of in-flight threads. That difference is the architectural reason the two ISAs are separate.
