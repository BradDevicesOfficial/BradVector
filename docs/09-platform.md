# The Platform

BradVector is more than an ISA: it is the software platform that makes one kernel run on every tier. This document covers the product mapping and the platform stack.

## The product line

BradVector runs on every Torox die:

| Brand | Line | Target | ISA |
|---|---|---|---|
| **BradFx** | integrated | phones, tablets, thin-and-light | BradVector |
| **BradGfx** | dedicated | laptops, desktops, workstations | BradVector |
| **BradApex** | datacenter | rack-scale accelerator | BradVector |

One ISA from a phone GPU to a datacenter accelerator. A kernel compiled to `.bvbc` runs on all three — that is the whole point of a coherent graphics and compute line.

### Why the software is the moat

The thesis of the platform is that the durable advantage is **software, not silicon**. A die can be copied; a working toolchain, a portable bytecode, a runtime that runs before the silicon exists, and a community's accumulated kernels are much harder to replicate. So the platform is built as a software stack first:

1. An ISA that is specified before any die is fabricated.
2. A portable bytecode that outlives any single die.
3. A runtime that executes that bytecode on CPUs, in browsers, and on real GPUs.
4. A toolchain that a developer can use *today*.

## The stack

```
  ┌───────────────────────────────────────────────┐
  │  braddev / TIFA SDK        ← developer entry   │
  ├───────────────────────────────────────────────┤
  │  BVML / BVN / BVComm       ← the library stack │
  ├───────────────────────────────────────────────┤
  │  bradgdb + BradTimeline    ← tooling           │
  ├───────────────────────────────────────────────┤
  │  bradc                     ← the assembler     │
  ├───────────────────────────────────────────────┤
  │  BVRT                      ← the runtime       │
  ├───────────────────────────────────────────────┤
  │  .bvbc (portable) / .brsh (native)             │
  ├───────────────────────────────────────────────┤
  │  BradVector ISA            ← the native target │
  └───────────────────────────────────────────────┘
```

### Layer by layer

| Layer | What it is | Status |
|---|---|---|
| **BradVector ISA (`.brsh`)** | the native target every Torox die implements | SOLID |
| **BradVector bytecode (`.bvbc`)** | the portable intermediate form, compiled once | SHIPPED |
| **bradc** | the assembler: `.bvbs` → `.bvbc` | SHIPPED |
| **BVRT** | the runtime that executes `.bvbc` | SHIPPED |
| **BVML / BVN / BVComm** | the math / neural / communication library stack | DESIGNED |
| **bradgdb + BradTimeline** | debugger and event profiler | SHIPPED |
| **braddev / TIFA SDK** | developer entry point (reference kit / AI SDK) | SHIPPED (braddev) |

The distinction matters: the ISA, bytecode, compiler, runtime, and tooling all exist and run. The library stack and the production silicon are the parts still being built out.

## `.bvbc` vs `.brsh`

The two object forms are the platform's portability mechanism:

| | `.bvbc` | `.brsh` |
|---|---|---|
| Level | portable bytecode | native ISA |
| Produced | compile time, any host | install time, per tier |
| Runs on | BVRT everywhere | the target Torox die |
| Contains | instruction stream + relocations + tier metadata | instruction stream + descriptor tables |

Write once as `.bvbc`; ship the native form by re-compiling at install time for the exact die. A new generation can therefore run existing kernels without the developer recompiling anything.

## Absorption: Brad-CVT

The platform's adoption strategy is built around **Brad-CVT**: a translation layer that accepts shaders and kernels written for existing ecosystems and maps them onto BradVector. The intent is that the ISA's CUDA-compatible warp size (32 threads) and familiar shader stages reduce the cost of moving existing code.

This is scoped, not shipped. Where compatibility is claimed, it is the 32-thread warp size and the standard shader-stage model — not a claim of binary compatibility.

## The open-standards hedge

BradVector targets the standard graphics and compute entry points (Vulkan, Metal, DirectX; the compute equivalents) alongside the native path. A developer can enter through the standard that their tools already speak, or through the BradDev Kit (`braddev`) directly. The platform does not require abandoning existing pipelines to use a Torox part.

## The flywheel

```
  more developers
        │
        ▼
  more BradVector kernels ──► a richer toolchain
        │                          │
        ▼                          ▼
  better hardware utilization ◄── more reasons to buy Torox
```

Each turn of the loop deepens the software moat: kernels accumulate, the toolchain gets better, the hardware gets more useful, and more developers arrive.

## Honesty guardrails

The platform commit is explicit about what exists and what does not:

| Claim | Reality |
|---|---|
| ISA specified | SOLID — this repo |
| Toolchain runs | SHIPPED — bradc, BVRT, bradgdb, BradTimeline, braddev |
| Browser execution | SHIPPED — WASM build on the website |
| Reference RTL | SHIPPED — `bradvector_core.v` |
| Library stack | DESIGNED |
| Production silicon | DESIGNED |
| Brad-CVT translation | scoped, not shipped |
| Future generations | VISION |

No claim of shipping silicon. No claim of benchmark parity that has not been measured. The ISA and the software are the deliverable that exists today.

See also: [Toolchain](10-toolchain.md), [Architecture](02-architecture.md), [ISA reference](../isa/BRADVECTOR_ISA.md).
