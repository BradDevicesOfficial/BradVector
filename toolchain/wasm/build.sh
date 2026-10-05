#!/usr/bin/env bash
# Build the BradVector toolchain (bradc + BVRT) to WebAssembly.
# No Emscripten / WASI needed: freestanding clang + wasm-ld + wasm_shim.c.
set -euo pipefail
cd "$(dirname "$0")"
REPO="$(cd ../.. && pwd)"
SRC="$REPO/src"
OUT="$REPO/site/assets/bradvector.wasm"

FLAGS=(--target=wasm32 -O2 -ffreestanding)
INC=(-Ishim_inc "-I$SRC/include" "-I$SRC/bradvector" "-I$SRC/bvlibs/include" -I.)

# Embed kernel sources as C string macros (escape backslash, quote, newline).
KSRC="$SRC/bvlibs/kernels"
esc() { local s; s=$(cat); s=${s//\\/\\\\}; s=${s//\"/\\\"}; s=${s//$'\n'/\\n}; printf '%s' "$s"; }
emit_kernel_header() {
  local guard="$1" out="$2"; shift 2
  {
    echo "#ifndef $guard"
    echo "#define $guard"
    while [ "$#" -gt 0 ]; do
      printf '#define %s "' "$1"
      printf '%s' "$(esc < "$KSRC/$2")"
      printf '"\n'
      shift 2
    done
    echo "#endif"
  } > "$out"
}
emit_kernel_header BVML_KERNELS_H bvml_kernels.h \
  BVML_KERNEL_SAXPY bvml_saxpy.bvbs \
  BVML_KERNEL_DOT bvml_dot.bvbs \
  BVML_KERNEL_GEMM bvml_gemm.bvbs
emit_kernel_header BVN_KERNELS_H bvn_kernels.h \
  BVN_KERNEL_RELU bvn_relu.bvbs \
  BVN_KERNEL_AFFINE bvn_affine.bvbs \
  BVN_KERNEL_SOFTMAX bvn_softmax.bvbs \
  BVN_KERNEL_SIGMUL bvn_sigmul.bvbs

clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bradvector/bradc.c" -o bradc.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bradvector/bvbc.c" -o bvbc.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bradvector/bvrt.c" -o bvrt.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bradvector/bradgdb.c" -o bradgdb.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bradvector/bradlib.c" -o bradlib.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bvlibs/src/bvml.c" -o bvml.o
clang "${FLAGS[@]}" "${INC[@]}" -c "$SRC/bvlibs/src/bvn.c" -o bvn.o
clang "${FLAGS[@]}" "${INC[@]}" -c wasm_shim.c    -o wasm_shim.o
clang "${FLAGS[@]}" "${INC[@]}" -c wasm_bridge.c  -o wasm_bridge.o

wasm-ld -m wasm32 --no-entry --export-memory --gc-sections \
  --export=brad_wasm_assemble --export=brad_wasm_run --export=brad_wasm_gdb \
  --export=brad_wasm_out --export=brad_wasm_out_len \
  --export=brad_wasm_compile --export=brad_wasm_error_line \
  --export=brad_wasm_error_msg --export=brad_wasm_error_msg_len \
  --export=brad_wasm_kernel_count --export=brad_wasm_kernel_name \
  --export=brad_wasm_insn_count --export=brad_wasm_disasm \
  --export=brad_wasm_create --export=brad_wasm_launch \
  --export=brad_wasm_exec --export=brad_wasm_step --export=brad_wasm_continue \
  --export=brad_wasm_breakpoint --export=brad_wasm_pc --export=brad_wasm_cycles \
  --export=brad_wasm_read_s --export=brad_wasm_read_v --export=brad_wasm_read_p \
  --export=brad_wasm_mem --export=brad_wasm_mem_size \
  --export=brad_wasm_bvml_saxpy --export=brad_wasm_bvml_dot \
  --export=brad_wasm_bvml_gemm \
  --export=brad_wasm_bvn_relu --export=brad_wasm_bvn_affine \
  --export=brad_wasm_bvn_softmax --export=brad_wasm_bvn_gelu \
  --export=brad_wasm_bvn_silu --export=brad_wasm_lib_check \
  --initial-memory=16777216 --max-memory=16777216 -z stack-size=262144 \
  bradc.o bvbc.o bvrt.o bradgdb.o bradlib.o bvml.o bvn.o wasm_shim.o wasm_bridge.o -o "$OUT"

ls -la "$OUT"
echo "OK -> $OUT"