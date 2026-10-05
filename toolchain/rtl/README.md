# BradISA V1 -- FPGA Implementation Guide

## Overview

This directory contains synthesisable RTL (Verilog and VHDL) for the BradISA V1
instruction set, targeting a simple 5-stage in-order Falcon pipeline:
**Fetch → Decode → Execute → Memory → Writeback**

The design is **single-issue, in-order**, with basic RAW hazard detection
(stall on read-after-write). Branch resolution flushes the pipeline and
redirects fetch.

## Files

```
src/rtl/
  verilog/            # Verilog 2001 RTL
    bradisa_defines.v   Package with opcodes and constants
    brad_regfile.v      Register file (16 × 32-bit, sync write, async read)
    brad_alu.v          ALU (ADD, SUB, MUL, AND, OR, XOR, SHL, SHR)
    brad_core.v         Top-level core (Falcon pipeline)
    tb_brad_core.v      Testbench (counts 1..100 with loop)

  vhdl/               # VHDL RTL (same design)
    bradisa_pkg.vhd     Package with opcodes and constants
    brad_regfile.vhd    Register file
    brad_alu.vhd        ALU
    brad_core.vhd       Top-level core
    tb_brad_core.vhd    Testbench

  fpga/               # FPGA build scripts and constraints
    brad_core.xdc       Xilinx Vivado constraints (Artix-7)
    brad_core.sdc       Intel Quartus constraints (Cyclone V)
    vivado_build.tcl    Vivado build script
    quartus_build.tcl   Quartus build script
    Makefile            Build automation
```

## Building for FPGA

### Xilinx (Vivado)

```bash
cd src/rtl/fpga
make vivado        # or: vivado -mode batch -source vivado_build.tcl
```

The default part is `xc7a35ticsg324-1L` (Artix-7 35T). Edit
`vivado_build.tcl` to change the target.

### Intel (Quartus Prime)

```bash
cd src/rtl/fpga
make quartus       # or: quartus_sh -t quartus_build.tcl
```

The default device is `5CSXFC6D6F31C6ES` (Cyclone V). Edit
`quartus_build.tcl` to change the target.

## Linting

```bash
make lint_verilog   # Requires Verilator
make lint_vhdl      # Requires GHDL
```

## Simulation

No simulator is bundled. Use any of:

- **Verilog:** `iverilog -o tb_brad_core.vvp bradisa_defines.v
  brad_regfile.v brad_alu.v brad_core.v tb_brad_core.v && vvp tb_brad_core.vvp`
- **VHDL:** `ghdl -a --std=08 bradisa_pkg.vhd brad_regfile.vhd brad_alu.vhd
  brad_core.vhd tb_brad_core.vhd && ghdl -e --std=08 tb_brad_core && ghdl -r --std=08 tb_brad_core`
- **Vivado:** Open the project in Vivado, add the testbench, run simulation
- **Quartus:** Open the project in Quartus, add the testbench, run RTL simulation

## Pipeline Hazards

The Falcon pipeline stalls for **RAW hazards**:
when a decoded instruction reads a register that the execute stage is about
to write. Forwarding is not implemented; the pipeline simply stalls one cycle.

Control hazards from branches are handled by flushing the fetch and decode
stages when a branch resolves in the execute stage. The branch penalty is
2 cycles (fetch and decode are flushed).

## Resource Estimates (Artix-7 35T, 100 MHz target)

| Resource  | Estimate |
|-----------|----------|
| LUTs      | ~300     |
| FFs       | ~200     |
| DSP48E1   | 1 (MUL)  |
| BRAM      | 0        |
| F_max     | >200 MHz |
