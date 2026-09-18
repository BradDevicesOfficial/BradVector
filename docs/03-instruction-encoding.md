# Instruction Encoding

Every BradVector instruction is **64 bits**. This document covers the format mechanics; the complete opcode tables live in the [ISA reference](../isa/BRADVECTOR_ISA.md).

## The 64-bit word

```
Bit:  63  56  55  48  47  40  39  32  31  24  23  16  15   8   7   0
     ┌────┬────┬────┬────┬────┬────┬────┬────┐
     │ OP │ PRED │ DST  │ SRC1 │ SRC2 │ IMM / │ FLAGS │ MOD  │
     │    │      │ REG  │ REG  │ REG  │ EXTRA │       │      │
     └────┴────┴────┴────┴────┴────┴────┴────┘
     8-bit 8-bit  8-bit  8-bit  8-bit  8-bit   8-bit   8-bit
```

| Field | Bits | Name | Description |
|-------|------|------|-------------|
| OP | 63:56 | opcode | one of 256 opcodes |
| PRED | 55:48 | predicate | predicate register selector + condition |
| DST | 47:40 | destination | destination register |
| SRC1 | 39:32 | source 1 | first source register |
| SRC2 | 31:24 | source 2 | second source register |
| IMM/EXTRA | 23:16 | immediate / extended op | literal value, or an extended-opcode byte |
| FLAGS | 15:8 | flags | saturation, rounding, IEEE mode |
| MOD | 7:0 | modifier | negate, abs, broadcast |

Unlike BradISA's fixed opcode-per-format scheme, BradVector uses a flat 8-bit opcode space with byte-wide register fields. Register indices therefore address the full 256-entry scalar register file directly; vector and predicate operands are selected through the `MOD` and `PRED` fields.

## Opcode map

Opcode groups (full tables in the [ISA reference](../isa/BRADVECTOR_ISA.md)):

| Range | Class | Examples |
|---|---|---|
| `0x00`–`0x0F` | float / general ALU | `ADD`, `MAD`, `DIV`, `SQRT`, `CMP`, `SEL` |
| `0x10`–`0x1D` | integer | `IADD`, `IMUL`, `IDIV`, `POPC`, `SHL`, `NOT` |
| `0x20`–`0x29` | vector & matrix | `VADD`, `VDOT`, `MMUL`, `VSHUFFLE`, `VBROADCAST` |
| `0x30`–`0x3D` | memory | `LOAD`, `STORE`, `LOADV`, `ATOMIC_*`, `TEX_*` |
| `0x40`–`0x47` | control flow | `BR`, `BR_COND`, `CALL`, `RET`, `BAR`, `EXIT` |
| `0x50`–`0x57` | ray tracing (BradRT) | `RT_TRACE`, `RT_INTERSECT`, `RT_BVH_WALK`, … |
| `0x60`–`0x69` | Neuro-Stream (NSU) | `NS_CONV2D`, `NS_RELU`, `NS_ATTENTION`, … |
| `0xF0`–`0xF8` | special / SFU | `RCP`, `SIN`, `COS`, `EXP2`, `LOG2`, `LANE_ID` |

## Predication

Every instruction carries a `PRED` field. When a predicate is active, the instruction executes only for lanes/threads where the predicate condition holds — otherwise the destination is left unchanged. Predicate conditions are selected with the `Px.<cond>` form:

| Condition | Meaning |
|---|---|
| `.N` | negative |
| `.Z` | zero |
| `.C` | carry |
| `.V` | overflow |
| `.GT` / `.LT` / `.EQ` / `.NE` | relational |

Predicated execution is how per-thread control flow inside a warp is expressed without branching the whole warp. `CMP` sets a predicate; `SEL` performs a predicated move.

## Flags

The `FLAGS` field (bits 15:8) controls per-instruction numeric behavior:

| Flag | Effect |
|---|---|
| saturation | clamp result to [0, 1] (graphics) |
| rounding mode | round-to-nearest / toward-zero / etc. |
| IEEE mode | strict vs. fast-math IEEE-754 behavior |

Saturation is common in the render path (color math); IEEE mode is common in the compute path (numerics).

## Modifiers

The `MOD` field (bits 7:0) applies operand transforms before execution:

| Modifier | Effect |
|---|---|
| negate | negate a source operand |
| abs | absolute value of a source operand |
| broadcast | replicate a scalar across vector lanes |

Modifiers are free at decode — they ride along in the instruction word rather than consuming an extra opcode.

## Control flow encoding

Control-flow instructions use the same 64-bit word with the target encoded in `IMM/EXTRA` / `SRC1`:

| Opcode | Mnemonic | Operation |
|---|---|---|
| `0x40` | `BR` | unconditional branch |
| `0x41` | `BR_COND` | conditional branch on predicate |
| `0x42` | `CALL` | subroutine call (saves return address) |
| `0x43` | `RET` | return from subroutine |
| `0x44` | `BAR` | barrier — sync threads within the warp |
| `0x45` | `BAR_CLUSTER` | barrier — sync across the cluster |
| `0x46` | `EXIT` | terminate the thread |
| `0x47` | `TRAP` | software trap |

Because SIMT executes a warp together, divergent branches are serialized: the warp executes each side of a divergent branch in turn, masking inactive lanes. `BAR` re-converges threads.

## Instruction stream in a shader binary

A shader binary has a header (64 bytes) followed by the encoded instruction stream:

```
shader header (64 bytes)
  └─ magic, ISA version, register counts, shared-memory size, entry point
instruction stream
  └─ 8-byte BradVector instructions
constant / descriptor tables
  └─ texture descriptors (32 bytes each), sampler state, kernel args
```

The native `.brsh` form and the portable `.bvbc` form carry the same instruction stream — `.bvbc` adds relocations and tier metadata so BVRT can run it on any Torox generation.

## Encoding vs. BradISA

For contrast:

| | BradISA | BradVector |
|---|---|---|
| Width | 32-bit fixed | 64-bit fixed |
| Opcode | 4 bits, format-locked | 8 bits, flat |
| Register fields | 4 bits each | 8 bits each |
| Predication | branch-based | per-instruction predicate field |
| Operand modifiers | separate instructions | in-word `MOD` field |
| SIMD | VSET (CPU-side vector ext) | native SIMT, 32-thread warps |

See also: [Registers](04-registers.md), [Execution model](06-execution-model.md), [ISA reference](../isa/BRADVECTOR_ISA.md).
