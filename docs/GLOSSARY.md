# Glossary

Every BradVector term, one line each.

## The split

| Term | Definition |
|---|---|
| **BradVector** | The GPU/accelerator ISA — dual-issue SIMT, 32-thread warps. Separate from BradISA. |
| **BradISA** | The CPU ISA. The two meet at the BradFusion fabric and share SPMP, not instructions. |
| **Torox** | The GPU die family (BGT/BFT/BAT). |
| **BradFx** | Integrated GPU line; shares the fabric with BradCore CPUs, no dedicated VRAM. |
| **BradGfx** | Dedicated GPU line. |
| **BradApex** | Datacenter / rack-scale accelerator line. |

## Execution model

| Term | Definition |
|---|---|
| **SIMT** | Single Instruction, Multiple Thread: one instruction stream drives many threads. |
| **Warp** | 32 threads executing together. |
| **Lane** | One thread within a warp; index obtained via `LANE_ID`. |
| **Dual-issue** | Up to two instructions issued per warp per cycle (e.g. ALU + memory). |
| **Divergence** | Threads in a warp taking different branch paths; sides are serialized with masking. |
| **Reconvergence** | Threads rejoining after divergent execution; `BAR` synchronizes. |
| **Predication** | Per-lane execution gated by a `PRED` field; avoids divergence. |
| **Occupancy** | Resident threads per cluster, bounded by registers / shared memory / barriers. |
| **Latency hiding** | Filling memory stalls by issuing other resident warps. |
| **Scoreboard** | Per-warp, per-register readiness table that gates instruction issue. |

## Registers

| Term | Definition |
|---|---|
| **S0–S255** | 256 × 32-bit scalar registers per thread. S0 is zero. |
| **V0–V63** | 64 × 512-bit vector registers (16 × FP32 lanes each). |
| **P0–P15** | 16 predicate registers, one bit per thread. |
| **S1–S15** | Thread / block / cluster identity registers. |
| **S16–S19** | Conventional first four kernel arguments. |

## ISA

| Term | Definition |
|---|---|
| **`.bvbs`** | BradVector assembly source. |
| **`.bvbc`** | Portable bytecode, compiled once. |
| **`.brsh`** | Native ISA object, install-time AOT per Torox die. |
| **64-bit encoding** | OP / PRED / DST / SRC1 / SRC2 / IMM / FLAGS / MOD, 8 bits each. |
| **Predicate field** | `PRED[55:48]`; selects predicate register + condition. |
| **FLAGS** | `[15:8]`; saturation, rounding, IEEE mode. |
| **MOD** | `[7:0]`; negate, abs, broadcast modifiers. |
| **SFU** | Special-function unit: `RCP`, `SIN`, `COS`, `EXP2`, `LOG2`. |

## Memory

| Term | Definition |
|---|---|
| **SPMP** | Shared Memory Pool — the unified CPU/GPU/NPU off-chip memory. |
| **BradRAM / L4** | On-die SRAM last-level cache. |
| **Infinity Buffer** | 2 GB 3D-stacked on-package L3 (BradGfx Max / Max Xntensive). |
| **L1** | 128 KB per cluster, 4-way, 64-byte lines, 4-cycle hit. |
| **Backing store** | The L2 slice; sizes 512 KB (BradFx Lite) to 8 MB (BradGfx Max). |
| **Coherency line** | 64 bytes — the granularity of CPU/GPU coherence. |
| **DDS** | The predictive load path used by `LOAD_DS` (4–50 cycles). |
| **ATOMIC_ADD / CAS / EXCH** | The three SPMP atomics. |
| **BRAD_TEX** | The BradCompress texture profile (format `0x30`). |
| **Tiling** | Address swizzling for 2D locality. |

## Extensions

| Term | Definition |
|---|---|
| **BradRT** | Ray-tracing cores (RTU, IPU, PRP, Triangle Cluster Accelerator). |
| **RTU** | Ray Traversal Unit — BVH navigation. |
| **IPU** | Intersection Processing Unit — ray-primitive tests. |
| **PRP** | Programmable Ray Pipeline — custom ray behavior, opacity micromaps. |
| **Neuro-Stream (NSU)** | On-GPU AI engine: conv, activations, attention, MoE. |
| **BradSense-Render** | Five-stage neural rendering pipeline (feed → reconstruct → stabilize → generate → display). |
| **NeRF** | Neural radiance field; evaluated/trained via `RT_NEARF_*`. |
| **MoE** | Mixture-of-Experts; selected by `NS_EXPERT_SELECT`, fused by `NS_MOE_FUSE`. |
| **BradSec** | Display engine — pixel-level encryption, HDMI 2.1 / DP 2.0. |

## Toolchain

| Term | Definition |
|---|---|
| **bradc** | The assembler (`.bvbs` → `.bvbc`). |
| **BVRT** | The runtime that executes `.bvbc`. |
| **bradgdb** | The debugger. |
| **BradTimeline** | 65,536-event execution recorder. |
| **bradlib.h** | Host-facing C API. |
| **bradvector.js** | Browser WASM bridge exposing bradlib 1:1. |
| **braddev** | Reference developer CLI (cvt / run / dbg / timeline / test). |
| **BVML / BVN / BVComm** | Math / neural / communication library stack (DESIGNED). |
| **TIFA SDK** | AI toolchain entry point. |
| **BradDev Kit** | Reference developer toolchain (`braddev`). |
| **Brad-CVT** | The translation strategy for absorbing code from other ecosystems. |

## Fabric

| Term | Definition |
|---|---|
| **BradFusion** | The system fabric linking CPU, GPU, NPU, memory. |
| **Work Group Distributor** | Hardware scheduler that balances workgroups across clusters. |
| **Command Frontend** | Driver-facing entry (BradOS driver; Vulkan / Metal / DirectX). |
| **SPMP access path** | GPU → fabric → SPMP controller; 50–200 cycles. |
