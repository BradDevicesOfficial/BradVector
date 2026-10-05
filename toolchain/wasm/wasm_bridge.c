/* wasm_bridge.c — WebAssembly bridge for the BradVector reference
 * toolchain.  Exposes the real bradc assembler + BVRT interpreter to
 * the browser so the brand-site terminal executes the actual shipped C
 * (bradc.c, bvbc.c, bvrt.c) instead of a replay.
 *
 * Legacy demo exports (terminal replay):
 *   uint32_t brad_wasm_assemble(void)  assemble the demo kernel, emit
 *                                      header + disassembly
 *   uint32_t brad_wasm_run(void)       assemble-only state kept; run the
 *                                      kernel on BVRT (32 lanes) + verify
 *   uint32_t brad_wasm_gdb(void)       break-at-4, one-step, dump regs,
 *                                      continue to BV_HALT
 *   uint32_t brad_wasm_out(void)       pointer to diagnostic buffer
 *   uint32_t brad_wasm_out_len(void)   bytes written
 *
 * Typed API exports (site/js/bradvector.js) — thin mapping onto the
 * host-facing BradVector Platform API (bradlib.c).  Same code the CPU
 * library exposes; these calls do not reset the wasm arena.
 *
 *   brad_wasm_compile(ptr, len)   assemble UTF-8 source at ptr
 *   brad_wasm_error_line()        line of last compile failure
 *   brad_wasm_error_msg()         ptr to NUL-terminated error text
 *   brad_wasm_error_msg_len()     length
 *   brad_wasm_kernel_count()
 *   brad_wasm_kernel_name(idx)    ptr to NUL-terminated name
 *   brad_wasm_insn_count()
 *   brad_wasm_disasm(pc)          line into diagnostic buffer
 *   brad_wasm_create(mem_bytes)   0 / 1
 *   brad_wasm_launch(kernel_ptr, args_ptr, nargs)   0 / 1
 *   brad_wasm_exec(max_insns)    bv_status (typed run; legacy demo
 *                                keeps the no-arg brad_wasm_run)
 *   brad_wasm_step()              bv_status
 *   brad_wasm_continue()          bv_status
 *   brad_wasm_breakpoint(pc)      id or 0xFFFFFFFF
 *   brad_wasm_pc(), brad_wasm_cycles()
 *   brad_wasm_read_s(lane,reg) brad_wasm_read_v(lane,reg,lane16)
 *   brad_wasm_read_p(lane,preg)
 *   brad_wasm_mem(), brad_wasm_mem_size()
 *
 * Returns 0 on success; nonzero error codes map to text in the buffer.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "brad/bradlib.h"
#include "brad/bradvector.h"
#include "brad/bvbc.h"
#include "brad/bvrt.h"
#include "bradc.h"

#include "bvml.h"
#include "bvn.h"

#include "math.h"
#include "stdio.h"

static char g_out[8192];
static size_t g_olen = 0;

/* BVML/BVN contexts are allocated from the freestanding arena on first
 * use and kept across calls (typed API never resets the arena).  The
 * legacy demo exports reset the arena, so those must drop the contexts. */
static struct bvml_context *g_bvml = NULL;
static struct bvn_context *g_bvn = NULL;

static void op(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g_olen < sizeof(g_out)) {
        int n = vsnprintf(g_out + g_olen, sizeof(g_out) - g_olen, fmt, ap);
        if (n > 0) {
            g_olen += (size_t)n;
            if (g_olen >= sizeof(g_out)) g_olen = sizeof(g_out) - 1;
        }
    }
    va_end(ap);
}

/* The exact src/tests/test_bradvector.c saxpy kernel (src/bradvector). */
static const char saxpy_src[] =
    ".kernel saxpy\n"
    "IADD S2, S1, S1\n"      /* lane*2 */
    "IADD S2, S2, S2\n"      /* lane*4 */
    "IADD S7, S16, S2\n"     /* &x[i] */
    "LOAD S3, S7, 0\n"       /* x[i] */
    "IMUL S4, S3, S18\n"     /* a*x[i] */
    "IADD S8, S17, S2\n"     /* &y[i] */
    "LOAD S5, S8, 0\n"       /* y[i] */
    "IADD S6, S4, S5\n"      /* a*x[i]+y[i] */
    "STORE S8, S6, 0\n"      /* y[i] = ... */
    "EXIT\n"
    ".end\n";

static struct bvbc_image g_img;
static uint64_t g_insns[BVBC_MAX_INSN];
static struct bradc_error g_err;

static void put_disasm(void)
{
    for (uint32_t i = 0; i < g_img.ninsn; i++) {
        struct bv_insn d = bv_decode(g_img.insns[i]);
        op("%4u:  %s  pred=%u dst=%u src1=%u src2=%u imm=%d\n",
           i, bv_opcode_name(d.op), d.pred, d.dst, d.src1, d.src2, d.imm);
    }
}

/* x[i] = i*3+1 at [0], y[i] = i+7 at [BV_WARP_SIZE*4].  Mirrors the CI test. */
static void prep_mem(struct bvrt_device *dev)
{
    uint8_t *m = (uint8_t *)bvrt_mem(dev);
    int l;
    for (l = 0; l < BV_WARP_SIZE; l++) {
        uint32_t v = (uint32_t)(l * 3 + 1);
        memcpy(m + (size_t)l * 4, &v, 4);
    }
    for (l = 0; l < BV_WARP_SIZE; l++) {
        uint32_t v = (uint32_t)(l + 7);
        memcpy(m + (size_t)(BV_WARP_SIZE * 4) + (size_t)l * 4, &v, 4);
    }
}

static int run_bvrt(int trace)
{
    bvrt_device *dev = bvrt_create(1u << 20);
    if (!dev) { op("bvrt: create failed\n"); return -1; }
    if (bvrt_load(dev, &g_img) != 0) { bvrt_destroy(dev); op("bvrt: load failed\n"); return -1; }

    prep_mem(dev);
    uint32_t args[3] = { 0, 4u * BV_WARP_SIZE, 4u };
    if (bvrt_launch(dev, "saxpy", args, 3) != 0) { bvrt_destroy(dev); op("bvrt: no kernel\n"); return -1; }

    enum bv_status st = bvrt_run(dev, 10000);

    if (trace) {
        uint8_t *m = (uint8_t *)bvrt_mem(dev);
        for (int l = 0; l < BV_WARP_SIZE; l++) {
            uint32_t got, xv, yv;
            memcpy(&got, m + (size_t)(BV_WARP_SIZE * 4) + (size_t)l * 4, 4);
            memcpy(&xv, m + (size_t)l * 4, 4);
            memcpy(&yv, m + (size_t)(BV_WARP_SIZE * 4) + (size_t)l * 4, 4);
            (void)xv; (void)yv;
            uint32_t exp = 4u * (uint32_t)(l * 3 + 1) + (uint32_t)(l + 7);
            op("  lane %2d: y=%u (exp %u)%s\n", l, got, exp,
               (got == exp) ? "" : "  <-- MISMATCH");
        }
    }
    if (st == BV_HALT)
        op("BVRT saxpy ok (32 lanes) - y[i] = a*x[i] + y[i] verified for all 32 lanes\n");
    else
        op("BVRT status %d\n", (int)st);

    bvrt_destroy(dev);
    return st == BV_HALT ? 0 : -1;
}

/* Real single-step session: set a breakpoint at insn 4 (IMUL S4,S3,S18),
 * step to it, dump the interesting registers, then run to completion. */
static int run_gdb(void)
{
    bvrt_device *dev = bvrt_create(1u << 20);
    if (!dev) { op("bradgdb: create failed\n"); return -1; }
    if (bvrt_load(dev, &g_img) != 0) { bvrt_destroy(dev); op("bradgdb: load failed\n"); return -1; }

    prep_mem(dev);
    uint32_t args[3] = { 0, 4u * BV_WARP_SIZE, 4u };
    bvrt_launch(dev, "saxpy", args, 3);

    op("break 4 set (IMUL S4, S3, S18)\n");
    for (int i = 0; i < 64; i++) {
        struct bvrt_warp *wp = bvrt_warp(dev);
        if (!wp || (int)wp->pc >= (int)g_img.ninsn || (int)wp->pc == 4) break;
        enum bv_status st = bvrt_step(dev);
        if (st != BV_OK) break;
    }

    struct bvrt_warp *wp = bvrt_warp(dev);
    struct bv_insn hi = bv_decode(g_img.insns[wp->pc]);
    op("halt @ PC=%u (%s S4, S3, S18)\n", wp->pc, bv_opcode_name(hi.op));
    op("regs: PC=%u S3=%u S18=%u\n", wp->pc, wp->s[0][3], wp->s[0][18]);

    enum bv_status st = bvrt_run(dev, 10000);
    op("continue -> %s\n", st == BV_HALT ? "BV_HALT - program completed"
                                         : "BVRT status: halt");
    bvrt_destroy(dev);
    return 0;
}

void wasm_reset_arena(void);

uint32_t brad_wasm_assemble(void)
{
    wasm_reset_arena();
    g_olen = 0;
    g_bvml = NULL;
    g_bvn = NULL;
    memset(&g_img, 0, sizeof(g_img));
    memset(&g_err, 0, sizeof(g_err));
    g_img.insns = g_insns;

    if (bradc_assemble(saxpy_src, &g_img, &g_err) != 0) {
        op("bradc: line %d: %s\n", g_err.line, g_err.msg);
        return 1;
    }
    op("bradc: assembled %u instruction(s), %u byte(s) -> saxpy.bvbc\n",
       g_img.ninsn, (unsigned)bvbc_serialize_size(&g_img));
    put_disasm();
    return 0;
}

uint32_t brad_wasm_run(void)
{
    g_olen = 0;
    return run_bvrt(1) == 0 ? 0 : 2;
}

uint32_t brad_wasm_gdb(void)
{
    g_olen = 0;
    return run_gdb() == 0 ? 0 : 3;
}

uint32_t brad_wasm_out(void) { return (uint32_t)(uintptr_t)g_out; }
uint32_t brad_wasm_out_len(void) { return (uint32_t)g_olen; }

/* ══════════════════ typed API (bradlib) ══════════════════ */

static struct bradlib_program *g_prog = NULL;
static struct bradlib_session *g_sess = NULL;

/* The freestanding arena is .bss at low linear-memory addresses, so the
 * shim's bump allocator can zero addresses that a caller-chosen source
 * pointer happens to occupy.  Copy source text into a dedicated scratch
 * buffer (a separate .bss object) so compiler allocations can never
 * clobber it. */
static char g_src[64 * 1024];

static char g_err_buf[256];
static int  g_err_line = 0;

static const char *cstr_of(uint32_t p) { return (const char *)(uintptr_t)p; }
static const uint32_t *u32_of(uint32_t p) { return (const uint32_t *)(uintptr_t)p; }

uint32_t brad_wasm_compile(uint32_t src_ptr, uint32_t src_len)
{
    if (src_len >= sizeof(g_src))
        return 0;
    memcpy(g_src, cstr_of(src_ptr), src_len);
    g_src[src_len] = 0;

    struct bradc_error err;
    memset(&err, 0, sizeof(err));

    struct bradlib_program *np = bradlib_compile(g_src, &err);
    if (!np) {
        g_err_line = err.line;
        snprintf(g_err_buf, sizeof(g_err_buf), "%s", err.msg);
        return 0;
    }
    if (g_prog)
        bradlib_free_program(g_prog);
    g_prog = np;
    g_err_line = 0;
    g_err_buf[0] = 0;
    return 1;
}

uint32_t brad_wasm_error_line(void) { return (uint32_t)g_err_line; }
uint32_t brad_wasm_error_msg(void)  { return (uint32_t)(uintptr_t)g_err_buf; }
uint32_t brad_wasm_error_msg_len(void)
{
    size_t n = 0;
    while (g_err_buf[n] && n < sizeof(g_err_buf)) n++;
    return (uint32_t)n;
}

uint32_t brad_wasm_kernel_count(void) { return g_prog ? bradlib_kernel_count(g_prog) : 0; }
uint32_t brad_wasm_insn_count(void)   { return g_prog ? bradlib_insn_count(g_prog) : 0; }

uint32_t brad_wasm_kernel_name(uint32_t idx)
{
    if (!g_prog) return 0;
    const char *n = bradlib_kernel_name(g_prog, idx);
    return n ? (uint32_t)(uintptr_t)n : 0;
}

uint32_t brad_wasm_disasm(uint32_t pc)
{
    g_olen = 0;
    if (!g_prog) { op("no program\n"); return 1; }
    char buf[128];
    int n = bradlib_disasm(g_prog, pc, buf, sizeof(buf));
    if (n < 0) { op("bad pc\n"); return 1; }
    memcpy(g_out, buf, (size_t)n);
    g_out[n] = 0;
    g_olen = (size_t)n;
    return 0;
}

uint32_t brad_wasm_create(uint32_t mem_bytes)
{
    if (!g_prog) return 0;
    struct bradlib_session *ns = bradlib_create(g_prog, mem_bytes);
    if (!ns) return 0;
    if (g_sess)
        bradlib_destroy(g_sess);
    g_sess = ns;
    return 1;
}

uint32_t brad_wasm_launch(uint32_t kernel_ptr, uint32_t args_ptr, uint32_t nargs)
{
    if (!g_sess) return 0;
    return bradlib_launch(g_sess, cstr_of(kernel_ptr),
                          u32_of(args_ptr), nargs) == 0 ? 1 : 0;
}

uint32_t brad_wasm_exec(uint32_t max_insns)
{
    if (!g_sess) return (uint32_t)BV_TRAP;
    return (uint32_t)bradlib_run(g_sess, max_insns);
}

uint32_t brad_wasm_step(void)
{
    if (!g_sess) return (uint32_t)BV_TRAP;
    return (uint32_t)bradlib_step(g_sess);
}

uint32_t brad_wasm_continue(void)
{
    if (!g_sess) return (uint32_t)BV_TRAP;
    return (uint32_t)bradlib_continue(g_sess);
}

uint32_t brad_wasm_breakpoint(uint32_t pc)
{
    if (!g_sess) return 0xFFFFFFFFu;
    int id = bradlib_breakpoint(g_sess, pc);
    return id < 0 ? 0xFFFFFFFFu : (uint32_t)id;
}

uint32_t brad_wasm_pc(void)          { return g_sess ? bradlib_pc(g_sess) : 0; }
uint32_t brad_wasm_cycles(void)      { return g_sess ? (uint32_t)bradlib_cycles(g_sess) : 0; }

uint32_t brad_wasm_read_s(uint32_t lane, uint32_t reg)
{
    return g_sess ? bradlib_read_s(g_sess, (int)lane, reg) : 0;
}

uint32_t brad_wasm_read_v(uint32_t lane, uint32_t reg, uint32_t lane16)
{
    return g_sess ? bradlib_read_v(g_sess, (int)lane, reg, lane16) : 0;
}

uint32_t brad_wasm_read_p(uint32_t lane, uint32_t preg)
{
    return g_sess ? bradlib_read_p(g_sess, (int)lane, preg) : 0;
}

uint32_t brad_wasm_mem(void)      { return g_sess ? (uint32_t)(uintptr_t)bradlib_mem(g_sess) : 0; }
uint32_t brad_wasm_mem_size(void) { return g_sess ? (uint32_t)bradlib_mem_size(g_sess) : 0; }

/* ─────────────────────────── BVLibs (BVML / BVN) ───────────────────────────
 * Thin typed-API exports over the reference libraries.  Pointers are
 * absolute wasm linear addresses; float scalars travel as f32 bit
 * patterns (wasm has no 32-bit float ABI in i32 params).  Return 0 on
 * success, nonzero on failure. */

static int chkf(const char *name, float got, float want)
{
    int ok = fabsf(got - want) <= 1e-3f * (fabsf(want) + 1.0f);
    op("  [%s] %-22s got %.5f want %.5f\n", ok ? "PASS" : "FAIL", name, got, want);
    return ok ? 0 : 1;
}

static struct bvml_context *bvml_ctx(void)
{
    if (!g_bvml) g_bvml = bvml_open(1u << 20);
    return g_bvml;
}

static struct bvn_context *bvn_ctx(void)
{
    if (!g_bvn) g_bvn = bvn_open(1u << 20);
    return g_bvn;
}

static const float *f32p(uint32_t p) { return (const float *)(uintptr_t)p; }
static float *f32w(uint32_t p) { return (float *)(uintptr_t)p; }

uint32_t brad_wasm_bvml_saxpy(uint32_t a_bits, uint32_t x, uint32_t y,
                              uint32_t out, uint32_t count)
{
    float a;
    memcpy(&a, &a_bits, sizeof a);
    struct bvml_context *c = bvml_ctx();
    if (!c) return 1;
    return bvml_saxpy(c, a, f32p(x), f32p(y), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvml_dot(uint32_t x, uint32_t y, uint32_t out, uint32_t count)
{
    struct bvml_context *c = bvml_ctx();
    if (!c) return 1;
    return bvml_dot(c, f32p(x), f32p(y), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvml_gemm(uint32_t alpha_bits, uint32_t A, uint32_t B,
                             uint32_t beta_bits, uint32_t C,
                             uint32_t M, uint32_t N, uint32_t K)
{
    float alpha, beta;
    memcpy(&alpha, &alpha_bits, sizeof alpha);
    memcpy(&beta, &beta_bits, sizeof beta);
    struct bvml_context *c = bvml_ctx();
    if (!c) return 1;
    return bvml_gemm(c, alpha, f32p(A), f32p(B), beta, f32w(C),
                     (size_t)M, (size_t)N, (size_t)K) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvn_relu(uint32_t x, uint32_t out, uint32_t count)
{
    struct bvn_context *c = bvn_ctx();
    if (!c) return 1;
    return bvn_relu(c, f32p(x), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvn_affine(uint32_t scale_bits, uint32_t bias_bits,
                              uint32_t x, uint32_t out, uint32_t count)
{
    float s, b;
    memcpy(&s, &scale_bits, sizeof s);
    memcpy(&b, &bias_bits, sizeof b);
    struct bvn_context *c = bvn_ctx();
    if (!c) return 1;
    return bvn_affine(c, s, b, f32p(x), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvn_softmax(uint32_t x, uint32_t out, uint32_t count)
{
    struct bvn_context *c = bvn_ctx();
    if (!c) return 1;
    return bvn_softmax(c, f32p(x), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvn_gelu(uint32_t x, uint32_t out, uint32_t count)
{
    struct bvn_context *c = bvn_ctx();
    if (!c) return 1;
    return bvn_gelu(c, f32p(x), f32w(out), (int)count) == 0 ? 0 : 2;
}

uint32_t brad_wasm_bvn_silu(uint32_t x, uint32_t out, uint32_t count)
{
    struct bvn_context *c = bvn_ctx();
    if (!c) return 1;
    return bvn_silu(c, f32p(x), f32w(out), (int)count) == 0 ? 0 : 2;
}

/* Run a compact BVML/BVN self-check, emit the report into g_out. */
uint32_t brad_wasm_lib_check(void)
{
    g_olen = 0;
    enum { N = 8 };
    float x[N], y[N], out[N];
    for (int i = 0; i < N; i++) {
        x[i] = (float)(i + 1);
        y[i] = 10.0f + (float)i;
    }
    struct bvml_context *m = bvml_ctx();
    struct bvn_context *c = bvn_ctx();
    if (!m || !c) {
        op("bvlib: context open failed\n");
        return 1;
    }

    int fails = 0;
    if (bvml_saxpy(m, 2.0f, x, y, out, N) == 0) {
        fails += chkf("bvml_saxpy out[0]", out[0], 2.0f * x[0] + y[0]);
        fails += chkf("bvml_saxpy out[7]", out[7], 2.0f * x[7] + y[7]);
    } else { op("  [FAIL] bvml_saxpy launch\n"); fails++; }
    float d = 0.0f, dw = 0.0f;
    if (bvml_dot(m, x, y, &d, N) == 0) {
        for (int i = 0; i < N; i++) dw += x[i] * y[i];
        fails += chkf("bvml_dot", d, dw);
    } else { op("  [FAIL] bvml_dot launch\n"); fails++; }

    enum { GM = 3, GN = 3, GK = 4, GN2 = GN * GN };
    float ga[GM * GK], gb[GK * GN], gc[GN2], gw[GN2];
    for (int i = 0; i < GM * GK; i++) ga[i] = (float)(i + 1);
    for (int i = 0; i < GK * GN; i++) gb[i] = (float)(2 * i + 1);
    for (int i = 0; i < GN2; i++) { gc[i] = 0.0f; gw[i] = 0.0f; }
    for (int r = 0; r < GM; r++)
        for (int c = 0; c < GN; c++)
            for (int k = 0; k < GK; k++)
                gw[r * GN + c] += ga[r * GK + k] * gb[k * GN + c];
    if (bvml_gemm(m, 1.0f, ga, gb, 0.0f, gc, GM, GN, GK) == 0) {
        for (int i = 0; i < GN2; i++)
            fails += chkf("bvml_gemm C[i]", gc[i], gw[i]);
    } else { op("  [FAIL] bvml_gemm launch\n"); fails++; }

    if (bvn_relu(c, x, out, N) == 0) {
        fails += chkf("bvn_relu out[2]", out[2], x[2]);
        fails += chkf("bvn_relu out[0]", out[0], x[0]);
    } else { op("  [FAIL] bvn_relu launch\n"); fails++; }
    if (bvn_affine(c, 2.0f, 1.0f, x, out, N) == 0) {
        fails += chkf("bvn_affine out[1]", out[1], 2.0f * x[1] + 1.0f);
    } else { op("  [FAIL] bvn_affine launch\n"); fails++; }
    float s = 0.0f;
    if (bvn_softmax(c, x, out, N) == 0) {
        for (int i = 0; i < N; i++) s += out[i];
        fails += chkf("bvn_softmax sum", s, 1.0f);
    } else { op("  [FAIL] bvn_softmax launch\n"); fails++; }
    if (bvn_gelu(c, x, out, N) == 0) {
        float w = x[0] / (1.0f + exp2f(-1.702f * x[0] * 1.442695f));
        fails += chkf("bvn_gelu out[0]", out[0], w);
    } else { op("  [FAIL] bvn_gelu launch\n"); fails++; }
    if (bvn_silu(c, x, out, N) == 0) {
        float w = x[0] / (1.0f + exp2f(-x[0] * 1.442695f));
        fails += chkf("bvn_silu out[0]", out[0], w);
    } else { op("  [FAIL] bvn_silu launch\n"); fails++; }

    op("  %s; %s\n", bvml_version(), bvn_version());
    op(fails ? "\n  %d check(s) FAILED\n" : "\n  all checks PASSED\n", fails);
    return (uint32_t)fails;
}