# Execution Model

BradVector is a SIMT architecture: a single instruction stream drives many threads that can also diverge and reconverge. This document covers warps, scheduling, occupancy, divergence, and synchronization.

## Warps and lanes

The fundamental unit is a **warp**: 32 threads that execute one instruction stream together. Each thread is one *lane*. The ISA exposes the lane index through `LANE_ID` (opcode `0xF6`), which lets a kernel compute per-lane addresses.

```
        one warp = 32 threads
   ┌──┬──┬──┬──┬──┬───┬──┐
   │0 │1 │2 │3 │4 │...│31│   ← lanes
   └──┴──┴──┴──┴──┴───┴──┘
        one instruction stream
```

## Dual-issue

Each warp can issue **up to two instructions per cycle** — typically one ALU instruction and one memory instruction, or two ALU instructions. The vector/matrix and SFU units are separate pipelines, so a warp can overlap a wide-vector multiply with a memory request.

## Warp scheduling

The hardware maintains multiple resident warps and issues them round-robin. A typical schedule:

```
Clock cycle: 0   1   2   3   4   5   6   7   8   9  ...
Warp 0:      I0  I1  I2  I3  I4
Warp 1:          I0  I1  I2  I3  I4
Warp 2:              I0  I1  I2  I3  I4
Warp 3:                  I0  I1  I2  I3  I4
```

Each scheduler issues one warp per cycle. When a warp stalls (e.g. on a long-latency SPMP load), it is de-scheduled and other warps issue in its place.

### Zero-overhead context switching

The register file is physically large so that every resident warp has its registers resident. Switching between warps costs no save/restore — the scheduler simply selects a different warp. This is what makes latency hiding practical.

## Occupancy

Occupancy is bounded by whichever cluster resource runs out first:

| Resource | Max per cluster |
|---|---|
| Threads | 1024 (32 warps) |
| Scalar registers | 8192 |
| Vector registers | 2048 |
| Shared memory | 64 KB |
| Barriers | 16 |

A kernel that uses all 256 scalar registers per thread limits resident threads to 8192 / 256 = 32 threads per cluster — severe under-occupancy. Kernels should use the smallest register footprint that performs well.

## Latency hiding

SPMP latency is 50–200 cycles. The architecture hides it by having many warps in flight:

```
warp 0:  LOAD ────────────────► de-scheduled
warp 1:        ALU ALU ALU ...   (runs while warp 0 waits)
warp 2:            ALU ALU ...
warp 3:                ...
```

More resident warps means more instructions to issue during any single warp's memory wait. This is the same trade-off as the CPU's out-of-order window, but achieved with threads instead of reordering.

## Divergence and reconvergence

When threads within a warp take different branch directions, the warp **diverges**: it executes one side with the non-participating lanes masked, then the other side. This is serialization, not parallelism — divergent warps are slower.

```
before:  all 32 lanes execute
         BR_COND on predicate
         ├── path A (lanes 0–15)   ← executed with lanes 16–31 masked
         └── path B (lanes 16–31)  ← executed with lanes 0–15 masked
after:   reconverged, all 32 lanes
```

The reference engine tracks divergence explicitly:
- `warp_divergent` — warp is currently split
- `warp_lane_mask` — which lanes are active
- `warp_issue_cnt` — outstanding issue count

`BAR` (barrier, opcode `0x44`) synchronizes threads within a warp; `BAR_CLUSTER` (`0x45`) synchronizes across a cluster.

## Predication instead of branching

For short per-lane decisions, the ISA prefers **predication** over branching to avoid the divergence cost. `CMP` sets a predicate; the instruction's `PRED` field gates each lane:

```asm
CMP      P0.EQ, S20, S21   ; P0 = (S20 == S21)
ADD      P0, S22, S23, S24 ; S24 = S22 + S23 only where P0
```

Predicated instructions do not diverge — inactive lanes are masked but the warp stays converged.

## Work distribution

Above the shader cluster, a hardware **work group distributor** hands work to clusters and balances load:

```
Command frontend
   └─► Work group distributor  (hardware scheduler, load balancing)
         ├─► Shader cluster 0
         ├─► Shader cluster 1
         └─► ...
```

Draw calls and compute dispatches are decomposed into workgroups, distributed across clusters, and each cluster's scheduler fills its warps. The distributor also routes ray-tracing work to BradRT cores and neural work to Neuro-Stream units (see [Extensions](08-extensions.md)).

## Compute vs. graphics dispatch

| Workload | Path |
|---|---|
| Graphics | IA → VS → tessellation → GS → rasterizer → PS/MS → OM |
| Compute | dispatched directly to shader clusters as workgroups |
| Ray tracing | BradRT cores at cluster side |
| Neural (upscale, denoise, frame gen) | Neuro-Stream units at cluster side |

All four share the same clusters and the same SPMP.

## Execution model vs. the CPU

| | BradISA (CPU) | BradVector (GPU) |
|---|---|---|
| Unit of execution | one instruction stream | 32-thread warp |
| Parallelism | ILP (out-of-order on Phoenix) | TLP (many warps) |
| Latency hiding | reorder buffer (128 entries) | resident warps |
| Control flow | branches, no predication | predication + divergence |
| Synchronization | interrupts, atomics | barriers, atomics |

See also: [Architecture](02-architecture.md), [Registers](04-registers.md), [RTL](07-rtl.md).
