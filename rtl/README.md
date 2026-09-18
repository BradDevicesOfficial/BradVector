# BradVector RTL — Reference Engine Guide

This directory contains the synthesisable reference implementation of the BradVector execution model.

## Files

```
verilog/
  bradvector_core.v   dual-issue SIMT compute engine, 4 warps, 256-bit vector datapath
```

## Overview

`bradvector_core` is a compact, inspectable instance of a Torox shader core:

| Property | Value |
|---|---|
| Warps | 4 |
| Threads per warp | 32 |
| Registers per thread | 32 |
| Vector datapath | 256-bit (8 × FP32 lanes) |
| Scalar register file | 4 × 32 × 32 × 32-bit |
| Vector register file | 4 × 32 × 32 × 256-bit |
| Scoreboard | depth 16, per-warp per-register |
| Fetch | 128-bit packets (4 × 32-bit instructions), 4-deep |
| Pipeline | Fetch → Decode → Issue → VectorRF → VALU/FPU/SFU → Writeback |

It implements 6-bit opcode groups covering vector ALU, FP FMA/add/mul/min/max/compare/abs/neg, integer add/sub/mul/64-bit/logic/shift, gather/scatter/load/store, branch and sync.

## Interface

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

- `ifetch_*` — instruction fetch: 32-bit address, 128-bit packet
- `mem_*` — data memory: 32-bit address, 256-bit read/write data
- `enable` — master run enable
- `warp_id` — external warp identification
- `busy` — engine executing
- `perf_cnt` — retirement counter

## Pipeline and hazards

- **Fetch:** round-robin warp selection; only active warps are fetched; a 128-bit packet yields four instructions.
- **Issue:** gated by the scoreboard — a warp waits only on the specific registers its instruction consumes; other warps keep issuing.
- **Execute:** separate VALU / FPU / SFU paths selected by opcode group.
- **Writeback:** clears scoreboard bits so dependent instructions release.

Warp switching is free: every resident warp's registers are physically present, so the scheduler selects a different warp instead of saving state.

## Lint and simulate

No vendor primitives are required.

Verilator:
```bash
verilator --lint-only rtl/verilog/bradvector_core.v
```

Icarus Verilog:
```bash
iverilog -o bv.vvp rtl/verilog/bradvector_core.v
```

To run, attach an instruction memory serving `.brsh` / BVRT-dispatched packets to `ifetch_*`, and an SPMP model to `mem_*`. Watch `busy` for completion and `perf_cnt` for retired work.

## Scope and honesty

- This is a **reference engine**, not the production Torox shader core. Production parts are full-width (512-bit vector), integrate more warps, and include the BradRT and Neuro-Stream blocks as separate units.
- **No testbench is bundled** in this repository. BVRT (the runtime) is the software golden reference for BradVector behavior; use it to check the RTL instruction-for-instruction.
- Resource utilisation and F_max are target-dependent and not quoted. Production numbers are a Brad Silicon matter.

See [`../docs/07-rtl.md`](../docs/07-rtl.md) for the full guide.
