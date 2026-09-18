# Architecture

How a Torox GPU is put together, from the command frontend down to the shader cluster.

## The GPU block diagram

```
                    BRADGFX / BRADFX GPU ARCHITECTURE
                    ──────────────────────────────────

                        ┌──────────────────┐
                        │  COMMAND FRONTEND │  ← BradOS driver, Vulkan/Metal/DirectX
                        │  + BradDev API    │
                        └────────┬─────────┘
                                 │ work distribution
                                 ▼
               ┌────────────────────────────────┐
               │     WORK GROUP DISTRIBUTOR      │
               │  (hardware scheduler, load bal.) │
               └────┬──────────┬──────────┬──────┘
                    │          │          │
         ┌──────────┘          │          └──────────┐
         ▼                     ▼                     ▼
   ┌──────────┐         ┌──────────┐          ┌──────────┐
   │SHADER    │         │BRADRT    │          │NEURO-    │
   │CLUSTERS  │◄───────►│CORES     │◄────────►│SENSE AI  │
   │(CUDA-like)│        │(RT units)│          │CORES     │
   │ Raster-  │         │ BVH trav │          │(upscale, │
   │ ization  │         │ Intersect│          │ denoise, │
   │ Compute  │         │ NeRF     │          │ frame gen)│
   └─────┬────┘         └────┬─────┘          └─────┬─────┘
         │                  │                      │
         └──────────────────┼──────────────────────┘
                            │
               ┌────────────┴────────────┐
               │  BRADFUSION FABRIC LINK   │
               │  (SPMP access, L2/L3$)   │
               └─────────────────────────┘
                            │
               ┌────────────┴────────────┐
               │  BRADSEC DISPLAY ENGINE  │
               │  (pixel-level encryption, │
               │   HDMI 2.1 / DP 2.0 out) │
               └─────────────────────────┘
```

## Shader clusters

Each GPU is built from **Shader Clusters** — groups of unified shader cores that handle vertex, pixel, compute and tessellation workloads. Shader cores are the hardware that executes the BradVector ISA.

| SKU | Est. clusters | Shader cores | FP32 TFLOPS (est.) |
|-----|--------------|--------------|-------------------|
| BradGfx Lite | 8 | 512 | 4–6 |
| BradGfx Standard | 16 | 1024 | 10–14 |
| BradGfx Ultra | 32 | 2048 | 20–28 |
| BradGfx Max | 64 | 4096 | 40–55 |

### BradFx integrated

BradFx shares the same shader architecture but scales down for power efficiency. It shares the BradFusion fabric with BradCore CPU cores and accesses SPMP directly — there is no dedicated VRAM.

| SKU | Shader cores | FP32 TFLOPS (est.) | TDP (est.) |
|-----|-------------|-------------------|-----------|
| BradFx Lite | 128 | 1–2 | 5–10 W |
| BradFx Standard | 256 | 2–4 | 10–20 W |
| BradFx Ultra | 512 | 5–8 | 20–35 W |
| BradFx Max | 1024 | 10–15 | 35–65 W |

## Die naming

Every physical Brad GPU die is named **three-letter brand prefix + generation number [+ tier letter]**:

| Prefix | Brand family | Line |
|--------|-------------|------|
| `BGT` | BradGfx Torox | Dedicated GPUs |
| `BFT` | BradFx Torox | Integrated GPUs |
| `BAT` | BradApex Torox | Datacenter / accelerator |

```
BGTnnn[-tier]
│ ││   └──── tier (Lite/Standard/Ultra/Max) — optional
│ │└────── generation number
│ └─────── family
└───────── brand prefix
```

The generation number is **architecture generation, not core count**. `nnn = 100` is Torox G1 (`BGT100`, `BFT100`, `BAT100`); `200` is G2, and so on. The optional tier suffix (`-L`, `-S`, `-U`, `-M`, `-X`) distinguishes SKUs within a generation without clashing with the generation number.

### Torox G1 (100-series)

**BGT — BradGfx Torox (dedicated):**

| Die | SKU | Shader | RT | Neuro-Stream | SPMP BW | TDP |
|-----|-----|--------|----|--------------|---------|-----|
| `BGT100-L` | BradGfx Lite | 512 | 16 | 128 | 256 Gb/s | 75 W |
| `BGT100-S` | BradGfx Standard | 1024 | 32 | 256 | 512 Gb/s | 130 W |
| `BGT100-U` | BradGfx Ultra | 2048 | 64 | 384 | 768 Gb/s | 220 W |
| `BGT100-M` | BradGfx Max | 4096 | 128 | 192 (MoE) | 1 TB/s | 350 W |
| `BGT100-X` | BradGfx Max Xntensive | 4096 | 128 | 512 (MoE) | 2 TB/s GDDR7 | 225 W |

**BFT — BradFx Torox (integrated):**

| Die | SKU | Shader | RT | Neuro-Stream | SPMP BW | TDP |
|-----|-----|--------|----|--------------|---------|-----|
| `BFT100-L` | BradFx Lite | 128 | — | 32 | 64 Gb/s | 5 W |
| `BFT100-S` | BradFx Standard | 256 | — | 64 | 128 Gb/s | 15 W |
| `BFT100-U` | BradFx Ultra | 512 | 16 | 96 | 256 Gb/s | 30 W |
| `BFT100-M` | BradFx Max | 1024 | 32 | 192 | 512 Gb/s | 60 W |

**BAT — BradApex Torox (datacenter):**

| Die | SKU | Notes |
|-----|-----|-------|
| `BAT100` | BradApex (Gen1) | rack-scale accelerator, same BradVector ISA as the client line |

> Neuro-Stream unit count is **not** monotonic with tier: `BGT100-M` carries fewer units (192) than `-U` (384) but adds larger SRAM and MoE expert switching. The registry is authoritative.

## The compute pipeline

Shader work flows through a fixed pipeline:

```
Input Assembler (IA)
  → Vertex Shader (VS)
  → Tessellation (HS + TS + DS)
  → Geometry Shader (GS)
  → Rasterizer
  → Pixel Shader (PS) / Mesh Shader (MS)
  → Output Merger (OM)
```

Compute workloads skip the raster path and dispatch workgroups directly to shader clusters. See [Memory](05-memory.md) for how the pipeline reads and writes SPMP, and [Extensions](08-extensions.md) for the BradRT and Neuro-Stream units that sit alongside the shader clusters.

## BradFusion fabric integration

The GPU connects to the rest of the system through the BradFusion fabric:

- **SPMP access** — the GPU reads and writes the shared memory pool directly. Zero-copy between CPU, GPU and NPU: a buffer written by a BradISA core is visible to a BradVector kernel without a copy.
- **L2/L3 coherent** — coherency is maintained at 64-byte cache-line granularity across CPU and GPU agents.
- **Coordination with the CPU** — the CPU runs BradISA, the GPU runs BradVector; the fabric is where the two programs rendezvous.

## BradSec display engine

The final stage is the BradSec display engine: pixel-level encryption with HDMI 2.1 / DisplayPort 2.0 output. Content is protected end-to-end from the output merger to the display.
