/* wasm_shim math.h — freestanding prototypes.  Only the float helpers
   BVRT actually calls; sqrt/fabs use native wasm opcodes, the
   transcendentals use polynomial approximations in wasm_shim.c. */
#ifndef BRAD_WASM_SHIM_MATH_H
#define BRAD_WASM_SHIM_MATH_H

float sqrtf(float);
float fabsf(float);
float exp2f(float);
float log2f(float);
float sinf(float);
float cosf(float);

#endif