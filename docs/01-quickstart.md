# Quick-start

The BradVector toolchain runs today. This guide covers the object forms, the `braddev` command, and a first kernel.

## The platform in one page

```
  kernel source (.bvbs)
        │
        ▼
  bradc ──► .bvbc   portable bytecode  ──► BVRT runtime (runs anywhere, today)
        │
        └────► .brsh   native ISA (install-time AOT, per Torox die — planned)
```

- **Write once:** your kernel assembles to `.bvbc`, a portable bytecode blob.
- **Run anywhere:** BVRT executes `.bvbc` on integrated, dedicated and datacenter tiers — including in a browser via the WASM build.
- **Go native:** at install time on real hardware, the native path emits `.brsh` for that die. The reference assembler does not emit `.brsh` yet; the container and convention are defined, the AOT step is future work.

## Object forms

| Extension | Name | Role | Status |
|---|---|---|---|
| `.bvbs` | BradVector assembly source | human-written BradVector assembly | SHIPPED |
| `.bvbc` | BradVector ByteCode | portable, tier-independent container | SHIPPED |
| `.brsh` | Brad shader / native ISA | native form for a specific Torox die | DESIGNED |

## The `braddev` command

`braddev` is the reference developer toolchain: one command over `bradc` → `.bvbc` → BVRT → `bradgdb` / BradTimeline.

| Command | What it does |
|---|---|
| `braddev version` | print the version |
| `braddev cvt in.bvbs -o out.bvbc [-d]` | assemble (optionally disassemble) |
| `braddev run out.bvbc --arg N ... --dump S1 S3 ...` | run in BVRT and dump registers/memory |
| `braddev gpu info` | device profile |
| `braddev gpu top out.bvbc --arg ...` | single-sample performance snapshot |
| `braddev timeline out.bvbc --arg ... -o trace.csv` | per-instruction trace + CSV |
| `braddev dbg out.bvbc -b PC --arg ... --dump ...` | breakpoints + register inspection |
| `braddev test` | self-test of the drivers and the pipeline |

## A first kernel

BradVector is SIMT: one kernel body runs across a warp of 32 lanes. `S0` is a read-only zero; `S1` is the lane index; kernel arguments arrive in `S16..S31`. Loop counters and addresses live in the integer domain (`MOV`/`IADD`/`ISUB`), while the float ALU (`ADD`/`MUL`/…) carries float data.

```asm
; fib.bvbs — iterative fib(n)
;   args: S16=f0  S17=f1  S18=n  S19=one  S20=zero
;   fib(8) = 21 lands in S1
.kernel fib
    MOV S1, S16              ; f0
    MOV S2, S17              ; f1
    MOV S21, S18             ; n
top:
    IADD S3, S1, S2          ; f2 = f0 + f1
    MOV S1, S2               ; f0 = f1
    MOV S2, S3               ; f1 = f2
    ISUB S21, S21, S19       ; n -= 1
    CMP P0, S21, S20         ; n == 0?
    BR_COND@P0 fin
    BR top
fin:
    EXIT
.end
```

Assemble and run it — passing `f0=0 f1=1 n=8 one=1 zero=0`:

```
$ braddev cvt fib.bvbs -o fib.bvbc -d
$ braddev run fib.bvbc --arg 0 --arg 1 --arg 8 --arg 1 --arg 0 --dump S1 S3 S21
$ braddev dbg fib.bvbc -b 4 --arg 0 --arg 1 --arg 8 --arg 1 --arg 0 --dump S1 S3 S21
```

`--arg` accepts decimal or `0x` hex; `--dump` also accepts `M<addr>` to read a device-memory word.

## Running it (host C)

The `bradlib` API is the stable host surface: compile → create session → launch → run → inspect.

```c
#include "brad/bradlib.h"

struct bradc_error err;
struct bradlib_program *p = bradlib_compile(source, &err);
struct bradlib_session *s = bradlib_create(p, 1 << 20);   /* 1 MB SPMP */

uint32_t args[5] = { 0, 1, 8, 1, 0 };
bradlib_launch(s, "fib", args, 5);
bradlib_run(s, 10000);

uint32_t result = bradlib_read_s(s, 0, 1);   /* lane 0, S1 */

bradlib_destroy(s);
bradlib_free_program(p);
```

## Running it (browser)

The website embeds the same C code compiled to WebAssembly. `bradvector.js` mirrors `bradlib.h` 1:1:

```javascript
await BradVector.load();                 // fetches /assets/bradvector.wasm
BradVector.compile(source);              // .bvbs text
BradVector.create(1 << 20);
BradVector.launch("fib", [0, 1, 8, 1, 0]);
BradVector.exec(10000);

console.log(BradVector.readS(0, 1));     // fib result
```

## Debugging and profiling

Because the runtime is a single-warp interpreter, debugging is exact:

- **`bradgdb`** — attach to a device, set breakpoints, single-step, read `S`/`V`/`P` per lane.
- **`BradTimeline`** — wrap stepping to record up to 65,536 events with active-lane counts and an opcode histogram, then export CSV.

See the complete [Toolchain](10-toolchain.md) reference.

## Next steps

- [Architecture](02-architecture.md) — how a Torox GPU is built
- [Instruction encoding](03-instruction-encoding.md) — the 64-bit format
- [Execution model](06-execution-model.md) — warps, occupancy, divergence
- [ISA reference](../isa/BRADVECTOR_ISA.md) — every opcode
