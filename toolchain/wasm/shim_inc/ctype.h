/* wasm_shim ctype.h — freestanding header overriding the compiler's
   <ctype.h> so bradc.c's ctype calls need no libc table symbols. */
#ifndef BRAD_WASM_SHIM_CTYPE_H
#define BRAD_WASM_SHIM_CTYPE_H

static int isalpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int isdigit(int c) { return c >= '0' && c <= '9'; }
static int isalnum(int c) { return isalpha(c) || isdigit(c); }
static int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

#endif