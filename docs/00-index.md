# BradVector Documentation

The official programmer-visible reference for the Brad Devices GPU / accelerator instruction-set architecture — the ISA behind every Torox GPU.

## Repository layout

```
isa/
  BRADVECTOR_ISA.md         the normative ISA reference (64-bit encoding, all opcode classes)
rtl/
  verilog/bradvector_core.v synthesisable reference compute engine
  README.md                 RTL reference guide
docs/
  00-index.md               you are here
  01-quickstart.md          the platform, the object forms, first kernel
  02-architecture.md        SIMT, shader clusters, the compute pipeline
  03-instruction-encoding.md the 64-bit format, predicates, modifiers, classes
  04-registers.md           scalar / vector / predicate register files
  05-memory.md              SPMP, cache hierarchy, atomics, textures
  06-execution-model.md     warps, scheduling, occupancy, divergence
  07-rtl.md                 the bradvector_core reference engine
  08-extensions.md          BradRT ray tracing + Neuro-Stream AI
  09-platform.md            BradFx / BradGfx / BradApex, dies, the software platform
  10-toolchain.md           bradc, BVRT, bradgdb, BradTimeline, bradlib
  GLOSSARY.md               every term, one line each
```

## What BradVector is

BradVector is the **GPU and accelerator ISA**. It is not BradISA. The split is deliberate:

| | BradISA | BradVector |
|---|---|---|
| Runs on | CPU cores (Falcon, Kestrel, Phoenix) | Torox GPU dies |
| Model | 32-bit scalar RISC | dual-issue SIMT |
| Threads | one instruction stream | 32-thread warps |
| Registers | 16 × 32-bit GPRs | 256 scalar + 64 × 512-bit vector |
| Encoding | 32-bit fixed | 64-bit fixed |
| Meets the other at | BradFusion fabric + SPMP | BradFusion fabric + SPMP |

They share memory, not instructions. A CPU program is BradISA; a GPU kernel is BradVector.

## The two object forms

BradVector ships in two object forms, which together are what makes the platform portable:

| Form | What it is | When it is produced |
|---|---|---|
| `.bvbc` | portable bytecode — compiled once | at build time on any host |
| `.brsh` | native BradVector ISA — install-time AOT | per GPU tier, at install time |

Write once as `.bvbc`; `BVRT` runs it anywhere, and the native path re-compiles to `.brsh` for the exact Torox die. This is the mechanism behind "write a kernel once; it runs on integrated, dedicated and datacenter."

## Reading order

New to the platform?

1. [Quick-start](01-quickstart.md)
2. [Architecture](02-architecture.md)
3. [ISA reference](../isa/BRADVECTOR_ISA.md)
4. [Execution model](06-execution-model.md)

Looking something up?

- [Instruction encoding](03-instruction-encoding.md)
- [Registers](04-registers.md)
- [Memory](05-memory.md)
- [RTL engine](07-rtl.md)
- [Extensions](08-extensions.md)
- [Platform](09-platform.md)
- [Toolchain](10-toolchain.md)

## Authority

- `isa/BRADVECTOR_ISA.md` is the normative ISA reference in this repo.
- The GPU architecture specifics (`docs/02`, `docs/05`, `docs/09`) are design specifications — marked by tag where they describe silicon.
- The RTL in `rtl/` is a reference engine; production Torox designs ship with Brad Silicon.

## Quality tags

| Tag | Meaning |
|---|---|
| SHIPPED | runs today (software: assembler, runtime, tooling) |
| SOLID | spec + docs + tests agree |
| DESIGNED | silicon defined, not yet fabricated |
| VISION | honest scoping, no promise |

## Licence

ISA documentation and RTL are released under the [MIT licence](../LICENSE).
