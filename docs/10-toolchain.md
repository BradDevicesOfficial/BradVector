# Toolchain

The BradVector reference toolchain assembles, serializes, executes, debugs, and profiles `.bvbc` kernels. It builds as a static library plus the `braddev` developer command, and the same C sources compile to WebAssembly for the browser.

## Components

| Component | File(s) | Role |
|---|---|---|
| **bradc** | `bradc.c` / `bradc.h` | assembler: `.bvbs` text → in-memory `.bvbc` image |
| **bvbc** | `bvbc.c` / `bvbc.h` | portable bytecode container (serialize / pack / unpack) |
| **BVRT** | `bvrt.c` / `bvrt.h` | runtime: host-side SIMT interpreter for `.bvbc` |
| **bradgdb** | `bradgdb.c` / `bradgdb.h` | symbol-aware debugger on top of BVRT |
| **BradTimeline** | `bradtimeline.c` / `bradtimeline.h` | per-instruction execution profiler |
| **bradlib** | `bradlib.c` / `bradlib.h` | stable host-facing orchestration API |
| **braddev** | `braddev.c` | reference developer CLI over the whole stack |
| **WASM bridge** | `src/wasm/`, `site/js/bradvector.js` | browser build mirroring `bradlib.h` |

The library set builds as the static library `brad-vector` (linking `brad-hw`); `braddev` links it and the reference drivers.

## `braddev` — the developer command

| Command | Purpose |
|---|---|
| `braddev version` | print the version |
| `braddev cvt in.bvbs -o out.bvbc [-d]` | assemble `.bvbs` → `.bvbc`; `-d` disassembles |
| `braddev run out.bvbc [--kernel NAME] [--arg N]... [--dump SPEC]...` | run a kernel in BVRT |
| `braddev gpu info` | device profile from `bradvector.h` constants |
| `braddev gpu top out.bvbc [--arg N]...` | single-sample performance snapshot |
| `braddev timeline out.bvbc [--arg N]... [-o trace.csv]` | per-kernel cycle/insn profile + CSV |
| `braddev dbg out.bvbc [-b PC]... [--arg N]... [--dump SPEC]...` | breakpoints, single-step, register dumps |
| `braddev test` | self-test: drivers + toolchain pipeline |

`--arg` accepts decimal or `0x` hex. `--dump` takes register names (`S1`, `S3`, `S21`) or `M<addr>` for a device-memory word. `timeline` runs the kernel and writes a CSV trace; `gpu top` prints a one-sample profile.

## Assembly syntax (bradc)

| Syntax | Meaning |
|---|---|
| `.kernel name` | begin a kernel |
| `.end` | end the current kernel |
| `label:` | symbolic label |
| `OP dst, src1, src2` | register operands (`S#`, `V#`, `P#`) |
| `OP dst, src1, imm` | immediate in last position (`ADDi` only) |
| `OP@P# ...` | predicated form |
| `;` or `#` … | comment to end of line |

Operand conventions by class:

| Form | Example |
|---|---|
| three-register | `ADD S4, S5, S6` |
| immediate | `ADDi S4, S5, 8` |
| move | `MOV S4, S5` (assembles as `ADDi S4, S5, 0`) |
| single-source | `ABS S4, S5` (also `NEG`, `NOT`, `POPC`, `CLZ`) |
| compare → predicate | `CMP P3, S1, S2` |
| load | `LOAD S4, S5, 0` |
| vector load | `LOADV V4, S5, 0` |
| store | `STORE S5, S4, 0` |
| vector store | `STOREV S5, V4, 0` |
| atomic | `ATOMIC_ADD S4, S5, 0` |
| vector | `VADD V1, V2, V3`, `VMAD V1, V2, V3` |
| vector + scalar | `VBROADCAST V1, S2` |
| branch / call | `BR label`, `BR_COND label`, `CALL label` |
| no-operand | `RET`, `BAR`, `EXIT`, `TRAP` |
| special | `LANE_ID S4`, `WARP_SZ S4`, `CLOCK S4` |

Notes:
- Branch and call targets resolve to PC-relative 8-bit offsets (range −128…+127 instructions).
- `LOAD`/`STORE` carry an 8-bit signed byte offset.
- The assembler is a reference implementation: it does **not** perform scheduling, register allocation, or `.brsh` AOT emission.

### ISA conventions learned from the reference toolchain

- `S0` is a **read-only zero** register (writes discarded).
- Branches test **predicates**: `CMP P0, Sx, S0` then `BR_COND@P0 label`.
- `MOV` and all ALUs except `IADD`/`ISUB`/`IMUL` are **float-domain** — raw integers reinterpret as floats and small values hit denormals. Use `ADDi Sd, S0, N` to build exact `N.0f` bits.
- Integer addresses and counters live in `MOV`/`IADD`/`ISUB` on `S` registers; the only seed for integer constants is a launch argument (`S16..S31`).

## bvbc — the container

`.bvbc` is little-endian and endian-portable:

```
offset  size  field
0        8    magic    "BVBC0001"
8        4    version
12       4    flags (reserved, 0)
16       4    nkernels
20       4    nconst   (constant blob bytes)
24       4    ninsn    (8-byte words)
28       4    reserved
32       ...  kernel table (nkernels × bvbc_kernel_hdr)
...            constant blob
...            instruction stream (ninsn × 8 bytes)
```

API: `bvbc_serialize_size`, `bvbc_pack`, `bvbc_unpack`, `bvbc_find_kernel`, `bvbc_add_kernel`, `bvbc_emit`.

## BVRT — the runtime

`bvrt.h` exposes a host-side SIMT interpreter. Its reference model executes **one warp of 32 lanes**; each lane owns its scalar and predicate register files, the vector register file is per-warp, and a flat host buffer is the SPMP.

| Function | Description |
|---|---|
| `bvrt_create(mem_bytes)` | create a device with flat SPMP backing |
| `bvrt_destroy(dev)` | destroy the device |
| `bvrt_load(dev, img)` | load a `.bvbc` image (referenced, not copied) |
| `bvrt_launch(dev, kernel, args, nargs)` | launch a kernel; copies args into `S16..S31` |
| `bvrt_run(dev, max_insns)` | run to completion / trap / budget |
| `bvrt_step(dev)` | execute exactly one instruction |
| `bvrt_reset(dev)` | reset per-warp state for a fresh launch |
| `bvrt_mem(dev)` | flat SPMP host pointer |

Status codes: `BV_OK`, `BV_TRAP`, `BV_TIMEOUT`, `BV_HALT`, `BV_OOB`.

## bradgdb — the debugger

| Function | Description |
|---|---|
| `bradgdb_attach(g, dev)` | attach to a device |
| `bradgdb_add_break(g, pc)` | set a breakpoint (up to 64) |
| `bradgdb_enable_break(g, id, on)` | enable / disable |
| `bradgdb_step(g)` | single-step |
| `bradgdb_continue(g)` | run to breakpoint / halt / trap |
| `bradgdb_read_s / _v / _p` | read scalar / vector / predicate per lane |
| `bradgdb_pc(g)` | current warp PC |

## BradTimeline — the profiler

Wraps BVRT stepping to record a per-instruction timeline and aggregate statistics:

| Function | Description |
|---|---|
| `bvtl_create(dev)` | create a tracer |
| `bvtl_step(tl)` | capture the next instruction |
| `bvtl_run(tl, max_insns)` | run with capture |
| `bvtl_summary(tl)` | print a summary report |
| `bvtl_export_csv(tl, path)` | write the trace as CSV |

It holds up to 65,536 events; each carries the cumulative cycle, PC, opcode, and active-lane count, plus a per-opcode retire histogram.

## bradlib — the host API

`bradlib` owns its buffers so host code has one stable surface:

| Function | Description |
|---|---|
| `bradlib_compile(src, err)` | assemble `.bvbs` text → program |
| `bradlib_free_program(p)` | free a program |
| `bradlib_kernel_count / _name` | inspect kernels |
| `bradlib_serialize_size / _pack` | produce a `.bvbc` container |
| `bradlib_disasm(p, pc, buf, cap)` | disassemble one instruction |
| `bradlib_create(p, mem_bytes)` | create a session (device + debugger) |
| `bradlib_destroy(s)` | destroy a session |
| `bradlib_launch(s, kernel, args, nargs)` | launch a kernel |
| `bradlib_run(s, max_insns)` / `bradlib_step(s)` | execute |
| `bradlib_read_s / _v / _p` | inspect registers |
| `bradlib_pc` / `bradlib_cycles` | PC and cycle count |
| `bradlib_mem` / `bradlib_mem_size` | flat SPMP view |
| `bradlib_breakpoint(s, pc)` | add + enable a breakpoint |
| `bradlib_continue(s)` | run to breakpoint / halt / trap |

## The browser build

`site/js/bradvector.js` mirrors `bradlib.h` and drives `site/assets/bradvector.wasm`:

```javascript
await BradVector.load();
BradVector.compile(source);      // { ok, kernels, insnCount } or { ok:false, errorLine, errorMsg }
BradVector.create(1 << 20);
BradVector.launch("fib", args);
BradVector.exec(maxInsns);       // or step() / continue()
BradVector.breakpoint(pc);
BradVector.readS(lane, reg);
```

## Status

| Component | Status |
|---|---|
| `bradc` assembler | SHIPPED |
| `bvbc` container | SHIPPED |
| BVRT runtime (host + WASM) | SHIPPED |
| `bradgdb` | SHIPPED |
| BradTimeline | SHIPPED |
| `bradlib` host API | SHIPPED |
| `braddev` developer command | SHIPPED (reference) |
| `bradvector.js` browser bridge | SHIPPED |
| `.brsh` AOT emission | DESIGNED (not yet implemented) |
| Scheduling / register allocation | DESIGNED |
| BVML / BVN / BVComm | DESIGNED |

See also: [Platform](09-platform.md), [Quick-start](01-quickstart.md).
