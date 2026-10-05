// Node smoke test: instantiate the wasm (no imports), run the three
// exported bridge entries, and print their output.  Mirrors the JS the
// site's terminal uses.
import fs from "fs";

const buf = fs.readFileSync(process.argv[2] || "bradvector.wasm");
const { instance } = await WebAssembly.instantiate(buf, {});
const e = instance.exports;

function cstr(p) {
  const b = new Uint8Array(e.memory.buffer);
  let s = "";
  while (b[p]) s += String.fromCharCode(b[p++]);
  return s;
}
function call(name) {
  const rc = e[name]();
  return { rc, out: cstr(e.brad_wasm_out()) };
}

console.log("memory:", e.memory.buffer.byteLength, "bytes");

const a = call("brad_wasm_assemble");
console.log("=== assemble rc", a.rc, "===");
console.log(a.out);

const r = call("brad_wasm_run");
console.log("=== run rc", r.rc, "===");
console.log(r.out.slice(0, 1500));
console.log("…(lanes 0-31 shown above)");

const g = call("brad_wasm_gdb");
console.log("=== gdb rc", g.rc, "===");
console.log(g.out);

const fails = [];
if (a.rc !== 0) fails.push("assemble failed");
if (r.rc !== 0) fails.push("bvrt run failed");
if (!/BVRT saxpy ok \(32 lanes\)/.test(r.out)) fails.push("saxpy verification missing");
if (g.rc !== 0) fails.push("gdb failed");
if (!/BV_HALT - program completed/.test(g.out) && !/BV_HALT/.test(g.out)) fails.push("no BV_HALT in gdb");
if (!/assembled 10 instruction\(s\), 134 byte\(s\)/.test(a.out)) fails.push("header mismatch");
if (fails.length) {
  console.error("FAIL:", fails.join("; "));
  process.exit(1);
}
console.log("SMOKE OK");