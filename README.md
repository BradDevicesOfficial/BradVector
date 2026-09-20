<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/BradDevicesOfficial/brad-devices/main/assets/brad-devices-wordmark/svg/brad-devices-wordmark-white.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/BradDevicesOfficial/brad-devices/main/assets/brad-devices-wordmark/svg/brad-devices-wordmark-black.svg">
    <img alt="BRAD DEVICES" width="360">
  </picture>
</p>

<p align="center">
  <em>of</em>&nbsp;&nbsp;
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/BradDevicesOfficial/brad-devices/main/assets/brad-verse-wordmark/svg/brad-verse-wordmark-flat-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="https://raw.githubusercontent.com/BradDevicesOfficial/brad-devices/main/assets/brad-verse-wordmark/svg/brad-verse-wordmark-flat-light.svg">
    <img alt="BRADVERSE" width="190">
  </picture>
</p>

[![CI](https://github.com/BradDevicesOfficial/BradVector/actions/workflows/ci.yml/badge.svg)](https://github.com/BradDevicesOfficial/BradVector/actions/workflows/ci.yml)

<p align="center">
  <img alt="C" src="https://img.shields.io/badge/C-8A6D1F?style=for-the-badge">&nbsp;
  <img alt="Assembly" src="https://img.shields.io/badge/Assembly-8A6D1F?style=for-the-badge">&nbsp;
  <img alt="Verilog" src="https://img.shields.io/badge/Verilog-8A6D1F?style=for-the-badge">&nbsp;
  <img alt="WebAssembly" src="https://img.shields.io/badge/WebAssembly-8A6D1F?style=for-the-badge">&nbsp;
  <img alt="Linux" src="https://img.shields.io/badge/Linux-8A6D1F?style=for-the-badge">
</p>

# BradVector

The Brad Devices GPU and accelerator instruction set architecture — the ISA behind every Torox GPU, and the shipped software platform that runs it.

```text
This is not a reference to some future silicon.
It is the spec the silicon is built from,
the runtime that runs before the silicon exists,
and the engine that proves it fits on real fabric.
```

## The split

BradVector is the **GPU/accelerator ISA**. It is deliberately separate from **BradISA**, the CPU ISA:

| | [BradISA](https://github.com/BradDevicesOfficial/BradISA) | BradVector |
|---|---|---|
| Runs on | CPU cores (Falcon, Kestrel, Phoenix) | Torox GPU dies |
| Model | 32-bit scalar RISC | dual-issue SIMT |
| Threads | one stream | 32-thread warps |
| Registers | 16 × 32-bit | 256 scalar + 64 × 512-bit vector |
| Encoding | 32-bit fixed | 64-bit fixed |

They meet at the BradFusion fabric and share memory (SPMP). They do not share instructions.

## What this repo contains

```
isa/
  BRADVECTOR_ISA.md         the normative ISA reference — 64-bit encoding, all opcode classes
rtl/
  verilog/bradvector_core.v synthesisable reference compute engine
  README.md                 RTL guide: pipeline, scoreboard, lint/simulate
docs/
  00-index.md               documentation index and reading order
  01-quickstart.md          object forms, the runtime, a first kernel
  02-architecture.md        SIMT, shader clusters, the compute pipeline, dies
  03-instruction-encoding.md the 64-bit format, predicates, modifiers, classes
  04-registers.md           scalar / vector / predicate register files
  05-memory.md              SPMP, cache hierarchy, atomics, textures
  06-execution-model.md     warps, scheduling, occupancy, divergence
  07-rtl.md                 the bradvector_core reference engine
  08-extensions.md          BradRT ray tracing + Neuro-Stream AI
  09-platform.md            BradFx / BradGfx / BradApex, the software platform
  10-toolchain.md           bradc, BVRT, bradgdb, BradTimeline, bradlib
  GLOSSARY.md               every term, one line each
```

## The ISA, briefly

BradVector is a **64-bit fixed-width SIMT ISA**:

- 32-thread warps, dual-issue (ALU + memory, or ALU + ALU)
- 256 × 32-bit scalar registers, 64 × 512-bit vector registers (16 × FP32 lanes)
- 16 predicate registers for predicated execution
- 256 opcodes across ALU, integer, vector/matrix, memory, control, ray tracing, neural and SFU classes
- Per-instruction predicate, flag and modifier fields encoded in the instruction word

Every Torox die — BGT (dedicated), BFT (integrated), BAT (datacenter) — runs the same BradVector engine. Write a kernel once; it runs from integrated to rack scale.

## The two object forms

| Form | What it is | When |
|---|---|---|
| `.bvbc` | portable bytecode, compiled once | build time, any host |
| `.brsh` | native ISA, install-time AOT | per Torox tier |

The runtime (`BVRT`) executes `.bvbc` everywhere, including in the browser via the WASM build. This is the mechanism behind "write once, run on every tier".

## The shipped toolchain

- **bradc** — the assembler (`.bvbs` → `.bvbc`)
- **BVRT** — the runtime (native + WASM)
- **bradgdb** — interactive debugger
- **BradTimeline** — 65,536-event execution recorder
- **bradlib.h** — stable host-facing API
- **braddev** — reference developer CLI (cvt / run / dbg / timeline / test)
- **bradvector.js** — browser bridge exposing bradlib 1:1

`.brsh` native emission is defined but not yet implemented by the reference assembler.

See [docs/10-toolchain.md](docs/10-toolchain.md).

## The RTL

A synthesisable, dual-issue SIMT reference engine: 4 warps, 32 threads per warp, 256-bit vector datapath, round-robin scheduler with a 16-deep scoreboard, in plain Verilog-2001 with no vendor primitives. See [`rtl/README.md`](rtl/README.md).

This is a reference instance, not the production shader core; production parts are full-width and ship with Brad Silicon.

## Links

- **Documentation:** [docs/00-index.md](docs/00-index.md) — the full reference tree
- **BradISA (CPU ISA):** [github.com/BradDevicesOfficial/BradISA](https://github.com/BradDevicesOfficial/BradISA)
- **Site:** [brad-devices.vercel.app](https://brad-devices.vercel.app)
- **Contact:** brad.devices.official@gmail.com
- **GitHub:** [BradDevicesOfficial](https://github.com/BradDevicesOfficial)

---

<p align="center"><em>Write the kernel once; the scheduler decides where it runs. — The Architect</em></p>
