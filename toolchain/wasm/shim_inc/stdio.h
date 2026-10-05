/* wasm_shim stdio.h — freestanding prototypes.  Implementations live
   in wasm_shim.c. */
#ifndef BRAD_WASM_SHIM_STDIO_H
#define BRAD_WASM_SHIM_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *, size_t, const char *, va_list);
int snprintf(char *, size_t, const char *, ...);
int printf(const char *, ...);

#endif