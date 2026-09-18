# Memory Model and Cache Hierarchy

The GPU reads and writes the shared SPMP pool through the BradFusion fabric. There is no private VRAM on integrated parts, and on dedicated parts memory is still managed through the same unified model.

## Memory architecture

```
        CPU (BradISA)              GPU (BradVector)
             │                          │
             └──────────┬───────────────┘
                        ▼
              ┌───────────────────┐
              │  BRADFUSION FABRIC │
              └─────────┬─────────┘
                        ▼
        ┌───────────────────────────────┐
        │   SPMP — shared memory pool    │
        │   (CPU + GPU + NPU, unified)   │
        └───────────────────────────────┘
```

- **SPMP** is the unified off-chip pool. CPU cores, GPU shader clusters, and the NPU all address it.
- **BradRAM / L4** is the on-die SRAM last-level cache.
- **Infinity Buffer** is an on-package 3D-stacked L3 present only on the top dedicated parts.

## Cache hierarchy

### L1 data cache

| Parameter | Value |
|---|---|
| Size | 128 KB per cluster |
| Associativity | 4-way |
| Line size | 64 bytes |
| Hit latency | 4 cycles |
| Miss latency | 32–64 cycles (L2 hit) |
| Policy | write-back, write-allocate |
| Coherency | snoop-based within cluster |
| Partitions | 8 banks for concurrent access |

### L2 cache

| SKU | L2 size | Associativity | Hit latency | Bandwidth |
|-----|---------|--------------|-------------|-----------|
| BradGfx Lite | 2 MB | 8-way | 32 cycles | 512 GB/s |
| BradGfx Standard | 4 MB | 16-way | 48 cycles | 1 TB/s |
| BradGfx Ultra | 6 MB | 16-way | 56 cycles | 1.5 TB/s |
| BradGfx Max | 8 MB | 16-way | 64 cycles | 2 TB/s |
| BradGfx Max Xntensive | 8 MB | 16-way | 64 cycles | 2 TB/s (SPMP) + 0.75–2.0 TB/s (GDDR7) |
| BradFx Lite | 512 KB | 4-way | 16 cycles | 128 GB/s |
| BradFx Standard | 1 MB | 8-way | 24 cycles | 256 GB/s |
| BradFx Ultra | 2 MB | 8-way | 32 cycles | 512 GB/s |
| BradFx Max | 4 MB | 16-way | 48 cycles | 1 TB/s |

### Infinity Buffer

The Infinity Buffer is a 3D-stacked SRAM tile acting as a massive L3, present on BradGfx Max / Max Xntensive only.

| Parameter | Value |
|---|---|
| Size | 2 GB |
| Technology | custom 6T SRAM, 3 nm BradLabs process |
| Stack | 8 dies, 256 MB each, TSV interposer |
| Bandwidth | 8 TB/s (2048-bit @ 4 GHz) |
| Hit latency | 16 cycles (tile-local) |
| Miss latency | 50–200 cycles (SPMP access) |
| Allocation | software-managed + hardware prefetch |

It exists to keep an entire game level's textures, a model's weights, BVH structures, and a generated frame buffer on-package.

## SPMP access paths

| Access type | Path | Latency | Bandwidth |
|-------------|------|---------|-----------|
| GPU → SPMP (direct) | fabric → SPMP controller | 50–200 cycles | up to SKU max |
| GPU → SPMP (via L2) | L2 fill from SPMP | 50–200 + 32–64 cycles | same |
| CPU → SPMP (GPU-visible) | coherent fabric path | 100–300 cycles | fabric-limited |
| HDB → SPMP (zero-copy) | direct DMA (no GPU involved) | — | PCIe 6.0 ×4 |
| Infinity Buffer → SPMP | background DMA | 200 cycles / 2 MB tile | 256 GB/s |

## Coherency

CPU and GPU share the same SPMP pool. Coherency is maintained at **64-byte cache-line** granularity:

| Agent | Reads | Writes |
|-------|-------|--------|
| CPU core | coherent via fabric snoop | write-update to SPMP |
| GPU cluster | loads through L1/L2 | write-back through L2 to SPMP |

A buffer written by a BradISA program is visible to a BradVector kernel without an explicit copy. This is the memory half of the CPU/GPU contract; the instruction halves stay separate.

## Atomics

The ISA exposes three atomics, operating on SPMP:

| Opcode | Mnemonic | Operation |
|---|---|---|
| `0x38` | `ATOMIC_ADD` | atomic add |
| `0x39` | `ATOMIC_CAS` | atomic compare-and-swap |
| `0x3A` | `ATOMIC_EXCH` | atomic exchange |

Atomics serialize at the SPMP controller and cost 16–64 cycles. They are the synchronization primitive for cross-warp and cross-cluster communication.

## Memory instructions

| Opcode | Mnemonic | Width | Notes |
|---|---|---|---|
| `0x30` | `LOAD` | 32-bit | from SPMP |
| `0x31` | `LOAD2` | 64-bit | aligned |
| `0x32` | `LOADV` | 512-bit | aligned vector load |
| `0x33` | `LOAD_DS` | 32-bit | via DDS predictive path, 4–50 cycles |
| `0x34` | `STORE` | 32-bit | to SPMP |
| `0x35` | `STORE2` | 64-bit | aligned |
| `0x36` | `STOREV` | 512-bit | aligned vector store |
| `0x37` | `PREFETCH` | — | prefetch to L1/L2 |

`LOAD_DS` uses the DDS (data-directed / predictive) path to bring a line in before it is explicitly requested, cutting effective latency for predictable access patterns.

## Tiling and compression

- **Texture tiling modes** reduce DRAM row switching for 2D locality. The tiler swizzles addresses so that neighboring texels land in the same DRAM row.
- **Render target compression** stores color/depth in a compressed on-package form, expanding transparently on read.
- **BRAD_TEX** (`0x30`) is the BradCompress texture profile — the highest-ratio format, variable block size.

## Texture formats

| Format ID | Format | Block size | Use |
|---|---|---|---|
| `0x01` | R8G8B8A8_UNORM | 4 BPP | standard color |
| `0x02` | R16G16B16A16_FLOAT | 8 BPP | HDR color |
| `0x03` | R32G32B32A32_FLOAT | 16 BPP | high-precision |
| `0x10` | BC1_UNORM | 0.5 BPP | DXT1 |
| `0x11` | BC3_UNORM | 1 BPP | DXT5 |
| `0x12` | BC5_UNORM | 1 BPP | normal maps |
| `0x13` | BC7_UNORM | 1 BPP | high-quality |
| `0x20` | ASTC_4x4 | 1 BPP | mobile-optimized |
| `0x21` | ASTC_8x8 | 0.25 BPP | ultra-compressed |
| `0x30` | BRAD_TEX | variable | BradCompress profile |

## Latency hiding

SPMP loads cost 50–200 cycles. The warp scheduler hides this by interleaving up to 32 warps per cluster: a warp that issues a load is de-scheduled until its data returns, while other warps issue. The larger the L2 slice and the more resident warps, the less the effective latency. This is why [occupancy](06-execution-model.md) is the first thing to tune.

See also: [Architecture](02-architecture.md), [Execution model](06-execution-model.md), [ISA reference](../isa/BRADVECTOR_ISA.md).
