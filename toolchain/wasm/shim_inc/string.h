/* wasm_shim string.h — freestanding prototypes.  Implementations live
   in wasm_shim.c. */
#ifndef BRAD_WASM_SHIM_STRING_H
#define BRAD_WASM_SHIM_STRING_H

#include <stddef.h>

void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
size_t strcspn(const char *, const char *);
size_t strspn(const char *, const char *);

#endif