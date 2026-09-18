# The Reference RTL Engine

`rtl/verilog/bradvector_core.v` is a compact, synthesisable implementation of the BradVector execution model. It is a **reference engine** — a working, inspectable core that demonstrates how the ISA maps to hardware. Production Torox designs are full-width and ship with Brad Silicon.

## What it implements

| Property | Value |
|---|---|
| Module | `bradvector_core` |
| Style | dual-issue superscalar wide-vector |
| Warps | 4 (`NUM_WARPS`) |
| Threads/warp | 32 (`THREADS_PER_WARP`) |
| Registers/thread | 32 (`REGS_PER_THREAD`) |
| Vector datapath | 256-bit (`VEC_WIDTH`), 8 × FP32 lanes |
| Scalar register file | 4 × 32 × 32 × 32-bit |
| Vector register file | 4 × 32 × 32 × 256-bit |
| Scoreboard | depth 16, per-warp per-reg |
| Fetch queue | 4-deep, 128-bit packets (4 × 32-bit instructions) |
| Pipeline | Fetch → Decode → Issue → VectorRF → VALU/FPU/SFU → Writeback |

The released engine is a 256-bit, 4-warp slice. The ISA's architectural register file (256 scalar + 64 × 512-bit vector, 32 warps/cluster) is the full target; this engine shows a smaller instance of the same machine.

## Top-level interface

```verilog
module bradvector_core (
    input  wire         clk,
    input  wire         rst_n,
    input  wire         enable,
    input  wire [4:0]   warp_id,
    output wire [31:0]  ifetch_addr,
    input  wire [127:0] ifetch_data,
    input  wire         ifetch_valid,
    output wire         ifetch_ready,
    output wire [31:0]  mem_addr,
    output wire [255:0] mem_wdata,
    input  wire [255:0] mem_rdata,
    output wire         mem_write,
    output wire         mem_read,
    input  wire         mem_ready,
    output wire         busy,
    output wire [7:0]   perf_cnt
);
```

| Port group | Purpose |
|---|---|
| `ifetch_*` | 32-bit fetch address, 128-bit instruction packets (4 instructions) |
| `mem_*` | 32-bit address, 256-bit read/write data, read/write strobes |
| `enable` | master run enable |
| `warp_id` | external warp identification |
| `busy` | engine is executing |
| `perf_cnt` | 8-bit performance counter |

## Pipeline stages

```
 F ─────► D ─────► Issue ─────► VectorRF ─────► VALU / FPU / SFU ─────► WB
 fetch    decode   dispatch    register read    execute                 writeback
 (PC,     (opcode  (warp        (scalar +        (ALU / FPU / SFU /      (regfile
  128b     groups)  select,     vector read)     memory / branch)        write)
  packet)           scoreboard)
```

### Fetch (F)

- Round-robin warp selection across the 4 warps
- Fetch FSM: `F_IDLE` → `F_WAIT` → `F_READY`
- 128-bit packet latched (`4 × 32-bit` instructions), one slot consumed at a time
- Only active warps are selected (`warp_active[warp] != 0`)

### Decode (D)

- Instruction slots decoded against the 6-bit opcode groups
- Destination/source register fields extracted
- Reads the scalar and vector register files

### Issue

- Warp selected for issue; scoreboard checked
- **Scoreboard**: `sb_scalar_rdy` / `sb_vec_rdy`, depth 16, per warp, per register. An instruction waiting on a not-ready register is held until the producing instruction clears it
- Dual-issue: an ALU instruction and a memory instruction can issue together

### VectorRF

- Vector register read stage
- Scalar operand gathering and lane broadcast

### Execute (VALU / FPU / SFU)

Execute units, selected by opcode group:

| Group | Value | Unit |
|---|---|---|
| `OP_VEC_ALU` | `6'h00` | vector integer ALU |
| `OP_FPU_FMA` | `6'h01` | FP fused multiply-add |
| `OP_FPU_ADD` | `6'h02` | FP add |
| `OP_FPU_MUL` | `6'h03` | FP multiply |
| `OP_FPU_MIN` | `6'h04` | FP min |
| `OP_FPU_MAX` | `6'h05` | FP max |
| `OP_FPU_CMP` | `6'h06` | FP compare |
| `OP_FPU_ABS` | `6'h07` | FP absolute |
| `OP_FPU_NEG` | `6'h08` | FP negate |
| `OP_INT_ADD` | `6'h10` | integer add |
| `OP_INT_SUB` | `6'h11` | integer subtract |
| `OP_INT_MUL` | `6'h12` | integer multiply |
| `OP_INT64_OP` | `6'h13` | 64-bit integer op |
| `OP_LOGIC` | `6'h14` | bitwise logic |
| `OP_SHIFT` | `6'h15` | shifts |
| `OP_GATHER` | `6'h30` | gather load |
| `OP_SCATTER` | `6'h31` | scatter store |
| `OP_LOAD` | `6'h32` | load |
| `OP_STORE` | `6'h33` | store |
| `OP_BRANCH` | `6'h40` | branch |
| `OP_SYNC` | `6'h41` | barrier / sync |

### Writeback (WB)

- Result written to the scalar or vector register file
- Scoreboard bits cleared so dependent instructions can issue

## Warp state

| Register | Purpose |
|---|---|
| `warp_pc[0:3]` | program counter per warp |
| `warp_active[0:3]` | active lane mask per warp |
| `warp_divergent[0:3]` | warp currently split by control flow |
| `warp_lane_mask[0:3]` | participating lanes |
| `warp_issue_cnt[0:3]` | outstanding issue count |

This is the hardware embodiment of the [execution model](06-execution-model.md): per-warp PCs, divergence tracking, and a scoreboard that implements the ISA's dependence rules without stalling the whole machine.

## Register files

Two arrays, each `NUM_WARPS × THREADS_PER_WARP × REGS_PER_THREAD` entries:

- `scalar_rf` — 32-bit per entry
- `vec_rf` — 256-bit per entry

Because every resident thread has its own physical registers, warp switching is free — the scheduler points at a different warp's slice rather than saving and restoring state.

## Simulation and lint

The core is plain Verilog-2001 and requires no vendor primitives. It can be linted with Verilator and simulated with Icarus Verilog:

```bash
verilator --lint-only rtl/verilog/bradvector_core.v
iverilog -o bv.vvp rtl/verilog/bradvector_core.v
```

Wire `ifetch_*` to an instruction memory holding `.brsh` / BVRT-dispatched instructions, and `mem_*` to an SPMP model. The `busy` output indicates execution; `perf_cnt` counts retired work for profiling.

## Honest scope

- This engine is a **reference**, not the production Torox shader core. The production core is full-width (512-bit vector) and integrates more warps, larger caches, and the BradRT / Neuro-Stream units as separate blocks.
- There is **no bundled testbench** for `bradvector_core.v` in this repository — it is provided as an ISA-referencing implementation. The runtime (`BVRT`) is the software golden reference for BradVector behavior.
- Resource utilisation and F_max depend on the target and are not quoted here; production numbers are a Brad Silicon matter.

See also: [Execution model](06-execution-model.md), [ISA reference](../isa/BRADVECTOR_ISA.md), [Platform](09-platform.md).
