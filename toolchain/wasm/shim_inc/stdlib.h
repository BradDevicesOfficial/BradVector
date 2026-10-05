/* wasm_shim stdlib.h — freestanding prototypes.  Implementations live
   in wasm_shim.c. */
#ifndef BRAD_WASM_SHIM_STDLIB_H
#define BRAD_WASM_SHIM_STDLIB_H

#include <stddef.h>

void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);
long strtol(const char *, char **, int);
int rand(void);
void srand(unsigned);

#endif