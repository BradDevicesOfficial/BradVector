/* native_test.c — host-side checkout: drive wasm_bridge entry points
   with a native clang build (identical shim sources).  Lets us tell a
   logic bug from a wasm-only bug. */
#include <stdio.h>
#include <string.h>

uint32_t brad_wasm_assemble(void);
uint32_t brad_wasm_run(void);
uint32_t brad_wasm_gdb(void);
uint32_t brad_wasm_out(void);
uint32_t brad_wasm_out_len(void);

static const char *cstr(uint32_t p)
{
    return (const char *)(unsigned long)p;
}

int main(void)
{
    uint32_t rc = brad_wasm_assemble();
    printf("assemble rc=%u len=%u\n---\n%s\n", rc, brad_wasm_out_len(), (rc==0)?cstr(brad_wasm_out()):cstr(brad_wasm_out()));
    rc = brad_wasm_run();
    printf("run rc=%u\n---\n%s\n", rc, cstr(brad_wasm_out()));
    rc = brad_wasm_gdb();
    printf("gdb rc=%u\n---\n%s\n", rc, cstr(brad_wasm_out()));
    return 0;
}