# On-GPU Extensions — BradRT and Neuro-Stream

Two fixed-function engines sit alongside the shader clusters and speak BradVector. Kernels reach them through dedicated opcode classes rather than through the general ALU.

## BradRT — ray tracing cores

Each BradRT core contains four sub-units:

| Sub-unit | Function |
|---|---|
| **Ray Traversal Unit (RTU)** | navigates BVH acceleration structures; tree traversal optimized for cache locality |
| **Intersection Processing Unit (IPU)** | precise ray-primitive intersection (triangles, spheres, line segments), massively parallel |
| **Programmable Ray Pipeline (PRP)** | custom ray behavior, material interaction, secondary ray generation; supports Opacity Micromaps |
| **Triangle Cluster Accelerator** | accelerates ray tracing of clustered geometry |

### Ray-tracing opcodes

| Opcode | Mnemonic | Description |
|---|---|---|
| `0x50` | `RT_TRACE` | trace ray against BVH (32–256 cycles) |
| `0x51` | `RT_INTERSECT` | ray-primitive intersect test (16 cycles) |
| `0x52` | `RT_BVH_WALK` | walk one BVH node (4 cycles) |
| `0x53` | `RT_ANY_HIT` | trigger any-hit shader (8 cycles) |
| `0x54` | `RT_CLOSEST_HIT` | trigger closest-hit shader (8 cycles) |
| `0x55` | `RT_MISS` | trigger miss shader (4 cycles) |
| `0x56` | `RT_NEARF_EVAL` | evaluate a NeRF at a point (32 cycles) |
| `0x57` | `RT_NEARF_TRAIN` | NeRF training step (128 cycles) |

`RT_NEARF_*` bring neural radiance fields into the RT path: a ray can be evaluated against a learned representation instead of (or in addition to) triangle geometry.

### RT core scaling

| SKU | RT cores |
|---|---|
| BradGfx Lite | 16 |
| BradGfx Standard | 32 |
| BradGfx Ultra | 64 |
| BradGfx Max / Max Xntensive | 128 |
| BradFx Ultra | 16 |
| BradFx Max | 32 |

## Neuro-Stream units (NSU)

Neuro-Stream is the on-GPU AI engine. It runs the post-render and generation workloads — upscaling, denoising, and frame generation — on fixed-function units rather than on the shaders.

| Opcode | Mnemonic | Description |
|---|---|---|
| `0x60` | `NS_CONV2D` | 3×3 convolution (16 cycles) |
| `0x61` | `NS_CONV2D_5` | 5×5 convolution (32 cycles) |
| `0x62` | `NS_RELU` | ReLU (2 cycles) |
| `0x63` | `NS_GELU` | GELU (8 cycles) |
| `0x64` | `NS_SILU` | SiLU (8 cycles) |
| `0x65` | `NS_MAXPOOL` | 2×2 max pooling (8 cycles) |
| `0x66` | `NS_UPSAMPLE` | bilinear 2× upsample (16 cycles) |
| `0x67` | `NS_ATTENTION` | multi-head attention step (64 cycles) |
| `0x68` | `NS_EXPERT_SELECT` | select MoE expert weights (4 cycles) |
| `0x69` | `NS_MOE_FUSE` | fuse expert outputs (16 cycles) |

### Unit scaling

| SKU | Neuro-Stream units |
|---|---|
| BGT100-L | 128 |
| BGT100-S | 256 |
| BGT100-U | 384 |
| BGT100-M | 192 (MoE) |
| BGT100-X | 512 (MoE) |
| BFT100-L | 32 |
| BFT100-S | 64 |
| BFT100-U | 96 |
| BFT100-M | 192 |

Unit count is **not** monotonic with tier: the Max-class dies trade raw unit count for larger SRAM and Mixture-of-Experts switching, so `BGT100-M` carries fewer units than `-U` but runs larger models.

## BradSense-Render — the neural rendering pipeline

Neuro-Stream units implement **BradSense-Render**, a five-stage inference pipeline that reconstructs and generates frames:

| Stage | Name | What it does |
|---|---|---|
| 1 | Semantic Feed | input arrives via the BradFusion fabric |
| 2 | Per-Object Neural Reconstruction | reconstruct objects at higher fidelity |
| 3 | Temporal Stabilization | remove flicker across frames |
| 4 | Frame Generation | generate intermediary frames |
| 5 | Display Output | hand off to the BradSec display engine |

This is the mechanism behind upscaling, denoising, and frame generation in the graphics path. It shares the SPMP pool with the CPU, so a model's weights can be prepared by a BradISA program and consumed directly by the NSUs.

## How the engines cooperate

```
                    ┌───────────────┐
                    │ COMMAND       │
                    │ FRONTEND      │
                    └──────┬────────┘
                           ▼
              ┌─────────────────────────┐
              │  WORK GROUP DISTRIBUTOR  │
              └───┬────────┬────────┬────┘
                  ▼        ▼        ▼
            ┌─────────┐┌────────┐┌──────────┐
            │ SHADER  ││ BRADRT ││ NEURO-   │
            │ CLUSTERS││ CORES  ││ STREAM   │
            └────┬────┘└───┬────┘└────┬─────┘
                 └─────────┼──────────┘
                           ▼
                  ┌──────────────────┐
                  │ BRADFUSION FABRIC │
                  └──────────────────┘
```

A typical frame: shader clusters rasterize geometry, BradRT cores trace rays, Neuro-Stream units upscale and generate frames, and BradSec encrypts the result for display — all reading and writing the same SPMP pool through the fabric.

## Programming the extensions

- Ray tracing is driven through `RT_*` instructions and BVH descriptors in the shader binary.
- Neuro-Stream is driven through `NS_*` instructions with weight tensors in SPMP.
- Both are reached from ordinary BradVector kernels — there is no separate ISA or toolchain.
- The TIFA AI toolchain and the BradDev Kit (`braddev`) provide the higher-level entry points (see [Platform](09-platform.md)).

## Status

| Block | Status |
|---|---|
| BradRT ISA + opcodes | SOLID (spec) / DESIGNED (silicon) |
| Neuro-Stream ISA + opcodes | SOLID (spec) / DESIGNED (silicon) |
| BradSense-Render pipeline | DESIGNED |
| BVRT execution of `RT_*` / `NS_*` | SHIPPED (runtime accepts the opcodes) |

See also: [Architecture](02-architecture.md), [Memory](05-memory.md), [ISA reference](../isa/BRADVECTOR_ISA.md).
