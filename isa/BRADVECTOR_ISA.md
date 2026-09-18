# BradVector ISA — Instruction Set Architecture

> *The native instruction set for every Torox GPU core: BradGfx (dedicated), BradFx (integrated), BradApex (datacenter).*
> *[Gen1] — the ISA is shipped; the runtime runs today; production silicon is designed.*
> *Two object forms: `.bvbc` portable bytecode (compiled once) and `.brsh` native ISA (install-time AOT per tier).*

BradVector is the GPU/accelerator ISA of the Brad Devices stack. It is **separate from BradISA** (the CPU ISA). The two meet at the BradFusion fabric and share memory (SPMP); they do not share instructions, registers, or execution units.

---

## 1. Architecture Overview

BradVector is a **dual-issue SIMT (Single-Instruction, Multiple-Thread)** architecture designed for graphics and compute workloads. Each BradVector engine processes a 32-thread warp (matching CUDA warp size for Brad-CVT compatibility).

### Core Characteristics

| Property | Value |
|----------|-------|
| Warp size | 32 threads |
| Issue width | dual-issue (ALU + memory, or ALU + ALU) |
| Register file | 256 × 32-bit scalar, 64 × 512-bit vector |
| Shared memory | 64 KB per shader cluster |
| L1 cache | 128 KB per cluster |
| Execution model | SIMT with per-thread program counter |

---

## 2. Instruction Encoding

All instructions are 64 bits (8 bytes).

```
Bit:  63  56  55  48  47  40  39  32  31  24  23  16  15   8   7   0
     ┌────┬────┬────┬────┬────┬────┬────┬────┐
     │ OP │ PRED │ DST  │ SRC1 │ SRC2 │ IMM / │ FLAGS │ MOD  │
     │    │      │ REG  │ REG  │ REG  │ EXTRA │       │      │
     └────┴────┴────┴────┴────┴────┴────┴────┘
     8-bit 8-bit  8-bit  8-bit  8-bit  8-bit   8-bit   8-bit
```

### Instruction Fields

| Field | Bits | Description |
|-------|------|-------------|
| OP | 63:56 | Opcode (256 possible) |
| PRED | 55:48 | Predicate register + condition |
| DST | 47:40 | Destination register |
| SRC1 | 39:32 | Source register 1 |
| SRC2 | 31:24 | Source register 2 |
| IMM/EXTRA | 23:16 | Immediate data or extended op |
| FLAGS | 15:8 | Saturation, rounding, IEEE flags |
| MOD | 7:0 | Modifier (negate, abs, broadcast) |

---

## 3. Instruction Classes

### 3.1 Arithmetic (ALU)

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x00` | `ADD` | Add (32-bit float/int) | 4 cycles |
| `0x01` | `ADDi` | Add with immediate | 4 cycles |
| `0x02` | `SUB` | Subtract | 4 cycles |
| `0x03` | `MUL` | Multiply | 4 cycles |
| `0x04` | `MAD` | Fused multiply-add `dst = a*b + c` | 4 cycles |
| `0x05` | `DIV` | Divide (IEEE-754) | 16 cycles |
| `0x06` | `SQRT` | Square root | 16 cycles |
| `0x07` | `RSQRT` | Reciprocal square root | 8 cycles |
| `0x08` | `MIN` | Minimum | 4 cycles |
| `0x09` | `MAX` | Maximum | 4 cycles |
| `0x0A` | `ABS` | Absolute value | 2 cycles |
| `0x0B` | `NEG` | Negate | 2 cycles |
| `0x0C` | `CMP` | Compare (set predicate) | 4 cycles |
| `0x0D` | `SEL` | Select (predicated move) | 4 cycles |

### 3.2 Integer

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x10` | `IADD` | Integer add | 4 cycles |
| `0x11` | `ISUB` | Integer subtract | 4 cycles |
| `0x12` | `IMUL` | Integer multiply | 8 cycles |
| `0x13` | `IDIV` | Integer divide | 24 cycles |
| `0x14` | `IMAD` | Integer multiply-add | 8 cycles |
| `0x15` | `POPC` | Population count | 2 cycles |
| `0x16` | `CLZ` | Count leading zeros | 2 cycles |
| `0x17` | `BFIND` | Find first bit | 2 cycles |
| `0x18` | `SHL` | Shift left | 4 cycles |
| `0x19` | `SHR` | Shift right | 4 cycles |
| `0x1A` | `AND` | Bitwise AND | 2 cycles |
| `0x1B` | `OR` | Bitwise OR | 2 cycles |
| `0x1C` | `XOR` | Bitwise XOR | 2 cycles |
| `0x1D` | `NOT` | Bitwise NOT | 2 cycles |

### 3.3 Vector & Matrix

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x20` | `VADD` | Vector add (512-bit) | 8 cycles |
| `0x21` | `VMUL` | Vector multiply (element-wise) | 8 cycles |
| `0x22` | `VDOT` | Dot product (32×float) | 12 cycles |
| `0x23` | `VCROSS` | Cross product (3×float) | 8 cycles |
| `0x24` | `VMAD` | Vector fused multiply-add | 8 cycles |
| `0x25` | `MMUL` | Matrix multiply (4×4 float) | 16 cycles |
| `0x26` | `MMUL_B` | Batched matrix multiply (8×8) | 32 cycles |
| `0x27` | `VSHUFFLE` | Vector lane shuffle | 4 cycles |
| `0x28` | `VBROADCAST` | Broadcast scalar to vector | 2 cycles |
| `0x29` | `VREDUCE` | Vector reduction (sum/max/min) | 16 cycles |

### 3.4 Memory

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x30` | `LOAD` | Load 32-bit from SPMP | 8–200 cycles |
| `0x31` | `LOAD2` | Load 64-bit (aligned) | 8–200 cycles |
| `0x32` | `LOADV` | Load vector (512-bit, aligned) | 8–200 cycles |
| `0x33` | `LOAD_DS` | Load via DDS (predictive path) | 4–50 cycles |
| `0x34` | `STORE` | Store 32-bit to SPMP | 8–200 cycles |
| `0x35` | `STORE2` | Store 64-bit (aligned) | 8–200 cycles |
| `0x36` | `STOREV` | Store vector (512-bit, aligned) | 8–200 cycles |
| `0x37` | `PREFETCH` | Prefetch to L1/L2 | 2 cycles |
| `0x38` | `ATOMIC_ADD` | Atomic add | 16–64 cycles |
| `0x39` | `ATOMIC_CAS` | Atomic compare-and-swap | 16–64 cycles |
| `0x3A` | `ATOMIC_EXCH` | Atomic exchange | 16–64 cycles |
| `0x3B` | `TEX_SAMPLE` | Texture sample (2D) | 12–32 cycles |
| `0x3C` | `TEX_SAMPLE_3D` | Texture sample (3D/cube) | 16–48 cycles |
| `0x3D` | `TEX_GATHER` | Texture gather | 12–32 cycles |

### 3.5 Control Flow

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x40` | `BR` | Branch (unconditional) | 2 cycles |
| `0x41` | `BR_COND` | Conditional branch | 2 cycles |
| `0x42` | `CALL` | Subroutine call | 4 cycles |
| `0x43` | `RET` | Return from subroutine | 4 cycles |
| `0x44` | `BAR` | Barrier (sync threads in warp) | 4 cycles |
| `0x45` | `BAR_CLUSTER` | Barrier across cluster | 16 cycles |
| `0x46` | `EXIT` | Terminate thread | 1 cycle |
| `0x47` | `TRAP` | Software trap | — |

### 3.6 Ray Tracing (BradRT)

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x50` | `RT_TRACE` | Trace ray against BVH | 32–256 cycles |
| `0x51` | `RT_INTERSECT` | Ray-primitive intersect test | 16 cycles |
| `0x52` | `RT_BVH_WALK` | Walk BVH node | 4 cycles |
| `0x53` | `RT_ANY_HIT` | Any-hit shader trigger | 8 cycles |
| `0x54` | `RT_CLOSEST_HIT` | Closest-hit shader trigger | 8 cycles |
| `0x55` | `RT_MISS` | Miss shader trigger | 4 cycles |
| `0x56` | `RT_NEARF_EVAL` | Evaluate NeRF at point | 32 cycles |
| `0x57` | `RT_NEARF_TRAIN` | NeRF training step | 128 cycles |

### 3.7 Neuro-Stream (NSU)

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0x60` | `NS_CONV2D` | 2D convolution (3×3) | 16 cycles |
| `0x61` | `NS_CONV2D_5` | 2D convolution (5×5) | 32 cycles |
| `0x62` | `NS_RELU` | ReLU activation | 2 cycles |
| `0x63` | `NS_GELU` | GELU activation | 8 cycles |
| `0x64` | `NS_SILU` | SiLU activation | 8 cycles |
| `0x65` | `NS_MAXPOOL` | Max pooling (2×2) | 8 cycles |
| `0x66` | `NS_UPSAMPLE` | Bilinear upsample (2×) | 16 cycles |
| `0x67` | `NS_ATTENTION` | Multi-head attention step | 64 cycles |
| `0x68` | `NS_EXPERT_SELECT` | Select expert weights | 4 cycles |
| `0x69` | `NS_MOE_FUSE` | Fuse expert outputs | 16 cycles |

### 3.8 Special

| Opcode | Mnemonic | Description | Latency |
|--------|----------|-------------|---------|
| `0xF0` | `RCP` | Reciprocal (float) | 8 cycles |
| `0xF1` | `SIN` | Sine | 16 cycles |
| `0xF2` | `COS` | Cosine | 16 cycles |
| `0xF3` | `EXP2` | Base-2 exponent | 12 cycles |
| `0xF4` | `LOG2` | Base-2 logarithm | 12 cycles |
| `0xF5` | `RAND` | Pseudo-random | 4 cycles |
| `0xF6` | `LANE_ID` | Get lane index | 1 cycle |
| `0xF7` | `WARP_SZ` | Get warp size | 1 cycle |
| `0xF8` | `CLOCK` | Read cycle counter | 1 cycle |

---

## 4. Register File

### 4.1 Scalar Registers (S0–S255)

256 × 32-bit registers, accessible by all instruction types.

| Register | Special Purpose |
|----------|----------------|
| S0 | Zero (reads as 0, writes discarded) |
| S1–S15 | Thread ID, block ID, cluster ID (read-only) |
| S16–S31 | Kernel arguments |
| S32–S255 | General purpose |

### 4.2 Vector Registers (V0–V63)

64 × 512-bit registers (16 × 32-bit lanes).

| Register | Special Purpose |
|----------|----------------|
| V0 | Zero vector |
| V1–V63 | General purpose |

### 4.3 Predicate Registers (P0–P15)

16 × 32-bit predicate registers for predicated execution.

| Value | Condition |
|-------|-----------|
| `Px.N` | Negative |
| `Px.Z` | Zero |
| `Px.C` | Carry |
| `Px.V` | Overflow |
| `Px.GT` | Greater than |
| `Px.LT` | Less than |
| `Px.EQ` | Equal |
| `Px.NE` | Not equal |

---

## 5. Texture & Sampler Descriptors

### 5.1 Texture Descriptor (32 bytes)

```c
struct tex_descriptor {
    uint64_t    data_addr;      // Base address in SPMP
    uint32_t    width;
    uint32_t    height;
    uint32_t    depth;          // 1 for 2D
    uint16_t    format;         // TEX_FMT_RGBA8, TEX_FMT_BC7, etc.
    uint16_t    mip_levels;
    uint16_t    array_size;
    uint16_t    flags;          // CUBEMAP, VOLUME, etc.
    uint32_t    stride;
    uint32_t    data_size;
};
```

### 5.2 Supported Texture Formats

| Format ID | Format | Block Size | Use |
|-----------|--------|------------|-----|
| `0x01` | R8G8B8A8_UNORM | 4 BPP | Standard color |
| `0x02` | R16G16B16A16_FLOAT | 8 BPP | HDR color |
| `0x03` | R32G32B32A32_FLOAT | 16 BPP | High-precision |
| `0x10` | BC1_UNORM | 0.5 BPP | DXT1 compression |
| `0x11` | BC3_UNORM | 1 BPP | DXT5 compression |
| `0x12` | BC5_UNORM | 1 BPP | Normal maps |
| `0x13` | BC7_UNORM | 1 BPP | High-quality |
| `0x20` | ASTC_4x4 | 1 BPP | Mobile-optimized |
| `0x21` | ASTC_8x8 | 0.25 BPP | Ultra-compressed |
| `0x30` | BRAD_TEX | Variable | BradCompress texture profile |

---

## 6. Execution Model

### 6.1 Warp Scheduling

```
Clock Cycle: 0   1   2   3   4   5   6   7   8   9   ...
Warp 0:      I0  I1  I2  I3  I4
Warp 1:          I0  I1  I2  I3  I4
Warp 2:              I0  I1  I2  I3  I4
Warp 3:                  I0  I1  I2  I3  I4
```

Each scheduler issues one warp per cycle, round-robin across active warps. Dual-issue means up to **two instructions per warp per cycle** (e.g., ALU + memory).

### 6.2 Occupancy Limits

| Resource | Max per Cluster |
|----------|-----------------|
| Threads | 1024 (32 warps) |
| Registers (scalar) | 8192 (shared across warps) |
| Registers (vector) | 2048 |
| Shared memory | 64 KB |
| Barriers | 16 |

### 6.3 Latency Hiding

Loads from SPMP have 50–200 cycle latency. The warp scheduler hides this by interleaving up to 32 warps. A warp that issues a load is de-scheduled until the data returns; other warps execute in the meantime.

---

*See also: `docs/02-architecture.md`, `docs/05-memory.md`, `docs/08-extensions.md`.*
