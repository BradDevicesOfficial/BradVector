/* BradDev Kit — reference developer toolchain (BRADCVT_DRIVERS.md §6)
 *
 * One binary, one toolchain.  Subcommands mirror the §6 tool table:
 *
 *   braddev cvt <k.bvbs> [-o out.bvbc] [-d]     Brad-CVT CLI (offline translation)
 *   braddev gpu info                            device profile (real constants)
 *   braddev gpu top <img.bvbc>                  runtime perf snapshot (BradTimeline)
 *   braddev run <img.bvbc> [--kernel NAME] [--dump S# ...]  load+launch+run
 *   braddev dbg <img.bvbc> [-b PC[...]] [--dump S#]         debugger (bradgdb)
 *   braddev timeline <img.bvbc> [-o trace.csv]  per-kernel cycle profile (BradTimeline)
 *   braddev test                                self-test (SPMP/Fabric/EROE + toolchain)
 *
 * Reference implementation.  .cu source is a Brad-CVT [Gen1] target; the toolchain
 * compiles .bvbs assembly today.  It runs on the host BVRT interpreter — the same
 * code that ships in the wasm bridge — not fabricated hardware.
 */

#include "brad/bradvector.h"
#include "brad/bvbc.h"
#include "brad/bvrt.h"
#include "brad/bradgfx.h"
#include "bradgdb.h"
#include "bradtimeline.h"
#include "bradc.h"

#include "brad/spmp.h"
#include "brad/eroe.h"
#include "brad/fabric.h"

#ifdef BRADDEV_HAVE_BVLIB
#include "bvml.h"
#include "bvn.h"
#include <math.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version is supplied by the build system from project(... VERSION ...).
   The fallback only applies to a build that bypassed CMake. */
#ifndef BRADDEV_VERSION
#define BRADDEV_VERSION "0.0.0-unbuilt"
#endif
#define KIT_VERSION "BradDev Kit v" BRADDEV_VERSION " (reference)"

static void usage(const char *prog)
{
    fprintf(stderr,
        "%s\n"
        "usage:\n"
        "  %s cvt <k.bvbs> [-o out.bvbc] [-d]       assemble .bvbs -> .bvbc\n"
        "  %s gpu info                               print device profile\n"
        "  %s gpu top <img.bvbc>                     run + print perf snapshot\n"
        "  %s run <img.bvbc> [--kernel NAME] [--arg N...] [--dump S# ...]\n"
        "  %s dbg <img.bvbc> [-b PC...] [--arg N...] [--dump S# ...]\n"
        "  %s timeline <img.bvbc> [-o trace.csv]\n"
        "  %s test                                   self-test the Kit + drivers\n"
        "  %s lib test                               self-test BVML/BVN libraries\n"
        "  %s version\n",
        KIT_VERSION, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

static const char *status_name(enum bv_status st)
{
    switch (st) {
    case BV_OK:      return "BV_OK";
    case BV_TRAP:    return "BV_TRAP";
    case BV_TIMEOUT: return "BV_TIMEOUT";
    case BV_HALT:    return "BV_HALT";
    case BV_OOB:     return "BV_OOB";
    }
    return "BV_UNKNOWN";
}

/* Read a .bvbc file into a buffer and unpack an image that references it. */
static int load_image(const char *path, uint8_t **buf, struct bvbc_image *img)
{
    FILE *f = fopen(path, "rb");
    long sz;
    if (!f) {
        fprintf(stderr, "braddev: cannot open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > BVRT_MAX_MEM) {
        fclose(f);
        fprintf(stderr, "braddev: bad image size for %s\n", path);
        return -1;
    }
    *buf = malloc((size_t)sz);
    if (!*buf) {
        fclose(f);
        fprintf(stderr, "braddev: out of memory\n");
        return -1;
    }
    if (fread(*buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(*buf);
        *buf = NULL;
        fprintf(stderr, "braddev: read failed for %s\n", path);
        return -1;
    }
    fclose(f);
    if (bvbc_unpack(*buf, (size_t)sz, img) != 0 || img->ninsn == 0) {
        fprintf(stderr, "braddev: %s is not a valid .bvbc image\n", path);
        free(*buf);
        *buf = NULL;
        return -1;
    }
    return 0;
}

/* ─── braddev cvt — Brad-CVT CLI (offline translation) ─── */
static int cmd_cvt(int argc, char **argv)
{
    const char *in = NULL, *out = "a.bvbc";
    int disasm = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out = argv[++i];
        else if (strcmp(argv[i], "-d") == 0)           disasm = 1;
        else if (argv[i][0] != '-' || strcmp(argv[i], "-") == 0) in = argv[i];
        else { usage("braddev"); return 2; }
    }
    if (!in) { usage("braddev"); return 2; }

    FILE *f = (strcmp(in, "-") == 0) ? stdin : fopen(in, "rb");
    long sz;
    char *src = NULL;
    if (f == stdin) {
        fseek(stdin, 0, SEEK_END);
        sz = ftell(stdin);
        fseek(stdin, 0, SEEK_SET);
    } else {
        if (!f) { fprintf(stderr, "braddev: cannot open %s\n", in); return 1; }
        fseek(f, 0, SEEK_END);
        sz = ftell(f);
        fseek(f, 0, SEEK_SET);
    }
    if (sz < 0) {
        char buf[1 << 16];
        size_t got;
        size_t cap = 1 << 16, len = 0;
        char *tmp = malloc(cap);
        if (!tmp) { if (f != stdin) fclose(f); return 1; }
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
            if (len + got > cap) {
                cap *= 2;
                char *nx = realloc(tmp, cap);
                if (!nx) { free(tmp); return 1; }
                tmp = nx;
            }
            memcpy(tmp + len, buf, got);
            len += got;
        }
        sz = (long)len;
        src = tmp;
    } else if (sz == 0 || sz > 4 * 1024 * 1024) {
        if (f != stdin) fclose(f);
        fprintf(stderr, "braddev: bad source size\n");
        return 1;
    } else {
        char *tmp = malloc((size_t)sz + 1);
        if (!tmp) { if (f != stdin) fclose(f); return 1; }
        if (fread(tmp, 1, (size_t)sz, f) != (size_t)sz) {
            if (f != stdin) fclose(f);
            free(tmp);
            return 1;
        }
        tmp[sz] = '\0';
        src = tmp;
    }
    if (f != stdin) fclose(f);

    struct bradc_error err;
    memset(&err, 0, sizeof(err));
    uint64_t insn_buf[BVBC_MAX_INSN];
    struct bvbc_image img;
    memset(&img, 0, sizeof(img));
    img.insns = insn_buf;
    img.ninsn = 0;

    if (bradc_assemble(src, &img, &err) < 0) {
        fprintf(stderr, "braddev cvt: line %d: %s\n", err.line, err.msg);
        free(src);
        return 1;
    }
    free(src);

    size_t nbytes = bvbc_serialize_size(&img);
    uint8_t *buf = malloc(nbytes);
    if (!buf) return 1;
    size_t packed = bvbc_pack(&img, buf, nbytes);
    if (packed == 0) { free(buf); fprintf(stderr, "braddev: serialize failed\n"); return 1; }

    FILE *o = fopen(out, "wb");
    if (!o) { fprintf(stderr, "braddev: cannot write %s\n", out); free(buf); return 1; }
    fwrite(buf, 1, packed, o);
    fclose(o);

    printf("braddev cvt: assembled %u instruction(s) in %u kernel(s), "
           "%zu byte(s) -> %s\n",
           img.ninsn, img.nkernels, packed, out);

    if (disasm) {
        for (uint32_t i = 0; i < img.ninsn; i++) {
            struct bv_insn d = bv_decode(img.insns[i]);
            printf("  %4u:  %-12s pred=%u dst=S%u src1=S%u src2=S%u imm=%d\n",
                   i, bv_opcode_name(d.op), d.pred, d.dst, d.src1, d.src2, d.imm);
        }
    }
    free(buf);
    return 0;
}

/* ─── braddev gpu info — device profile ─── */
static int cmd_gpu_info(void)
{
    printf("BradVector Platform — reference device profile\n");
    printf("  arch            : %s\n", BV_ARCH_NAME);
    printf("  version         : %u.%u (ISA 0x%04X)\n",
           BV_VERSION_MAJOR, BV_VERSION_MINOR, BV_ISA_VERSION);
    printf("  warp size       : %u threads\n", BV_WARP_SIZE);
    printf("  insn size       : %u bytes (fixed)\n", BV_INSN_SIZE);
    printf("  scalar regs     : S0..S%u\n", BV_SCALAR_REGS - 1);
    printf("  vector regs     : V0..V%u (%u x 32-bit lanes each)\n",
           BV_VEC_REGS - 1, BV_VEC_LANES);
    printf("  predicate regs  : P0..P%u\n", BV_PRED_REGS - 1);
    printf("  device mem max  : %u MiB (BVRT host interpreter)\n",
           BVRT_MAX_MEM / (1024 * 1024));
    printf("  kernel args     : S16..S31\n");
    return 0;
}

/* ─── braddev gpu top / timeline — BradTimeline profiler ─── */
static int run_with_timeline(const char *path, const char *kernel,
                             uint32_t *args, int nargs,
                             const char *csv, int quiet)
{
    uint8_t *buf = NULL;
    struct bvbc_image img;
    if (load_image(path, &buf, &img) != 0)
        return 1;

    bvrt_device *dev = bvrt_create(BVRT_MAX_MEM);
    if (!dev) { free(buf); fprintf(stderr, "braddev: device alloc failed\n"); return 1; }
    if (bvrt_load(dev, &img) != 0) {
        fprintf(stderr, "braddev: load failed\n");
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }
    const char *kname = kernel ? kernel : (img.kernels[0].name);
    if (bvrt_launch(dev, kname, nargs ? args : NULL, (unsigned)nargs) != 0) {
        fprintf(stderr, "braddev: unknown kernel '%s'\n", kname);
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }

    struct bvtl *tl = bvtl_create(dev);
    enum bv_status st = bvtl_run(tl, 1ull << 30);
    if (!quiet)
        printf("  kernel '%s' -> %s\n", kname, status_name(st));
    bvtl_summary(tl);
    if (csv)
        printf("  trace CSV -> %s\n", bvtl_export_csv(tl, csv) == 0 ? "ok" : "FAILED");
    bvtl_destroy(tl);
    bvrt_destroy(dev);
    free(buf);
    return 0;
}

static int cmd_gpu_top(int argc, char **argv)
{
    const char *path = NULL;
    uint32_t args[16];
    int nargs = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc && nargs < 16)
            args[nargs++] = (uint32_t)strtoull(argv[++i], NULL, 0);
        else if (argv[i][0] != '-') path = argv[i];
        else { usage("braddev"); return 2; }
    }
    if (!path) { usage("braddev"); return 2; }
    printf("BradVector perf snapshot (BradTimeline)\n");
    return run_with_timeline(path, NULL, args, nargs, NULL, 0);
}

static int cmd_timeline(int argc, char **argv)
{
    const char *path = NULL, *csv = NULL;
    uint32_t args[16];
    int nargs = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) csv = argv[++i];
        else if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc && nargs < 16)
            args[nargs++] = (uint32_t)strtoull(argv[++i], NULL, 0);
        else if (argv[i][0] != '-') path = argv[i];
        else { usage("braddev"); return 2; }
    }
    if (!path) { usage("braddev"); return 2; }
    printf("BradTimeline — per-kernel cycle profile\n");
    return run_with_timeline(path, NULL, args, nargs, csv, 0);
}

/* ─── braddev run — load, launch, run to completion ─── */
static void dump_scalars(bvrt_device *dev, char *const *regs, int nregs)
{
    struct bvrt_warp *w = bvrt_warp(dev);
    for (int i = 0; i < nregs; i++) {
        char *p = regs[i];
        if (p[0] == 'M' && p[1] != '\0') {
            size_t off = (size_t)strtoull(p + 1, NULL, 0);
            uint32_t v = 0;
            uint8_t *mem = (uint8_t *)bvrt_mem(dev);
            if (mem && off + 4 <= BVRT_MAX_MEM)
                memcpy(&v, mem + off, 4);
            printf("    mem[0x%zx] = %u\n", off, v);
            continue;
        }
        if (p[0] != 'S' || p[1] == '\0')
            continue;
        unsigned r = (unsigned)strtoul(p + 1, NULL, 10);
        if (r >= BV_SCALAR_REGS)
            continue;
        printf("    S%u (lane 0) = %u\n", r, w->s[0][r]);
    }
}

static int cmd_run(int argc, char **argv)
{
    const char *path = NULL, *kernel = NULL;
    uint32_t args[16];
    int nargs = 0;
    char *dumps[256];
    int ndumps = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) kernel = argv[++i];
        else if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc && nargs < 16)
            args[nargs++] = (uint32_t)strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--dump") == 0)
            while (i + 1 < argc &&
((argv[i + 1][0] == 'S' || argv[i + 1][0] == 'M') &&
                      ((argv[i + 1][0] == 'M' && argv[i + 1][1] != 0) ||
                       (argv[i + 1][1] >= '0' && argv[i + 1][1] <= '9'))) &&
                     ndumps < 256)
                dumps[ndumps++] = argv[++i];
        else if (argv[i][0] != '-') path = argv[i];
        else { usage("braddev"); return 2; }
    }
    if (!path) { usage("braddev"); return 2; }

    uint8_t *buf = NULL;
    struct bvbc_image img;
    if (load_image(path, &buf, &img) != 0)
        return 1;
    bvrt_device *dev = bvrt_create(BVRT_MAX_MEM);
    if (!dev) { free(buf); return 1; }
    if (bvrt_load(dev, &img) != 0) { bvrt_destroy(dev); free(buf); return 1; }

    const char *kname = kernel ? kernel : img.kernels[0].name;
    if (nargs > 16) {
        fprintf(stderr, "braddev: too many --arg (max 16)\n");
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }
    if (bvrt_launch(dev, kname, nargs ? args : NULL, (unsigned)nargs) != 0) {
        fprintf(stderr, "braddev: unknown kernel '%s'\n", kname);
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }
    enum bv_status st = bvrt_run(dev, 1ull << 30);
    struct bvrt_warp *w = bvrt_warp(dev);
    printf("  kernel '%s' -> %s  pc=%u  cycles=%llu\n",
           kname, status_name(st), w->pc,
           (unsigned long long)w->cycles);
    if (ndumps)
        dump_scalars(dev, dumps, ndumps);
    bvrt_destroy(dev);
    free(buf);
    return 0;
}

/* ─── braddev dbg — run to breakpoints, dump registers ─── */
static int cmd_dbg(int argc, char **argv)
{
    const char *path = NULL;
    uint32_t args[16];
    int nargs = 0;
    uint32_t breaks[BRADGDB_MAX_BREAK];
    int nbreaks = 0;
    char *dumps[256];
    int ndumps = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc && nbreaks < BRADGDB_MAX_BREAK)
            breaks[nbreaks++] = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--arg") == 0 && i + 1 < argc && nargs < 16)
            args[nargs++] = (uint32_t)strtoull(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--dump") == 0)
            while (i + 1 < argc &&
((argv[i + 1][0] == 'S' || argv[i + 1][0] == 'M') &&
                      ((argv[i + 1][0] == 'M' && argv[i + 1][1] != 0) ||
                       (argv[i + 1][1] >= '0' && argv[i + 1][1] <= '9'))) &&
                     ndumps < 256)
                dumps[ndumps++] = argv[++i];
        else if (argv[i][0] != '-') path = argv[i];
        else { usage("braddev"); return 2; }
    }
    if (!path) { usage("braddev"); return 2; }

    uint8_t *buf = NULL;
    struct bvbc_image img;
    if (load_image(path, &buf, &img) != 0)
        return 1;
    bvrt_device *dev = bvrt_create(BVRT_MAX_MEM);
    if (!dev) { free(buf); return 1; }
    if (bvrt_load(dev, &img) != 0) { bvrt_destroy(dev); free(buf); return 1; }

    const char *kname = img.kernels[0].name;
    if (bvrt_launch(dev, kname, nargs ? args : NULL, (unsigned)nargs) != 0) {
        fprintf(stderr, "braddev: launch failed\n");
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }

    struct bradgdb g;
    if (bradgdb_attach(&g, dev) != 0) {
        fprintf(stderr, "braddev: debugger attach failed\n");
        bvrt_destroy(dev);
        free(buf);
        return 1;
    }
    for (int i = 0; i < nbreaks; i++)
        printf("  breakpoint @ pc %u\n", breaks[i]),
        bradgdb_add_break(&g, breaks[i]);

    enum bv_status st = bradgdb_continue(&g);
    printf("  kernel '%s' -> %s  pc=%u  steps=%llu\n",
           kname, status_name(st), bradgdb_pc(&g),
           (unsigned long long)g.steps);
    if (ndumps)
        dump_scalars(dev, dumps, ndumps);
    bvrt_destroy(dev);
    free(buf);
    return 0;
}

/* ─── braddev test — self-test of the Kit + drivers ─── */
static int check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    return ok ? 0 : 1;
}

#ifdef BRADDEV_HAVE_BVLIB
static int lib_val(const char *what, float got, float want)
{
    int ok = fabsf(got - want) <= 1e-3f * (fabsf(want) + 1.0f);
    printf("  [%s] %-22s got %.5f want %.5f\n",
           ok ? "PASS" : "FAIL", what, got, want);
    return ok ? 0 : 1;
}

/* Exercise BVML/BVN on a fixed vector against host references. */
static int bvlib_selfcheck(void)
{
    int fails = 0;
    enum { N = 8 };
    float x[N], y[N], out[N];
    for (int i = 0; i < N; i++) {
        x[i] = (float)(i + 1);
        y[i] = 10.0f + (float)i;
    }

    struct bvml_context *m = bvml_open(1u << 20);
    if (!m)
        return check(0, "bvml_open");
    fails += check(bvml_saxpy(m, 2.0f, x, y, out, N) == 0, "bvml_saxpy launch");
    fails += lib_val("bvml_saxpy out[0]", out[0], 2.0f * x[0] + y[0]);
    float d = 0.0f, dwant = 0.0f;
    fails += check(bvml_dot(m, x, y, &d, N) == 0, "bvml_dot launch");
    for (int i = 0; i < N; i++) dwant += x[i] * y[i];
    fails += lib_val("bvml_dot", d, dwant);

    enum { GM = 3, GN = 3, GK = 4, GN2 = GN * GN };
    float ga[GM * GK], gb[GK * GN], gc[GN2], gw[GN2];
    for (int i = 0; i < GM * GK; i++) ga[i] = (float)(i + 1);
    for (int i = 0; i < GK * GN; i++) gb[i] = (float)(2 * i + 1);
    for (int i = 0; i < GN2; i++) { gc[i] = 0.0f; gw[i] = 0.0f; }
    for (int r = 0; r < GM; r++)
        for (int c = 0; c < GN; c++)
            for (int k = 0; k < GK; k++)
                gw[r * GN + c] += ga[r * GK + k] * gb[k * GN + c];
    fails += check(bvml_gemm(m, 1.0f, ga, gb, 0.0f, gc, GM, GN, GK) == 0,
                   "bvml_gemm launch");
    for (int i = 0; i < GN2; i++)
        fails += lib_val("bvml_gemm C[i]", gc[i], gw[i]);
    bvml_close(m);

    struct bvn_context *c = bvn_open(1u << 20);
    if (!c)
        return check(0, "bvn_open");
    fails += check(bvn_relu(c, x, out, N) == 0, "bvn_relu launch");
    fails += lib_val("bvn_relu out[2]", out[2], x[2]);
    fails += check(bvn_affine(c, 2.0f, 1.0f, x, out, N) == 0, "bvn_affine launch");
    fails += lib_val("bvn_affine out[1]", out[1], 2.0f * x[1] + 1.0f);
    float s = 0.0f;
    fails += check(bvn_softmax(c, x, out, N) == 0, "bvn_softmax launch");
    for (int i = 0; i < N; i++) s += out[i];
    fails += lib_val("bvn_softmax sum", s, 1.0f);
    fails += check(bvn_gelu(c, x, out, N) == 0, "bvn_gelu launch");
    fails += lib_val("bvn_gelu out[0]", out[0], x[0] / (1.0f + expf(-1.702f * x[0])));
    fails += check(bvn_silu(c, x, out, N) == 0, "bvn_silu launch");
    fails += lib_val("bvn_silu out[0]", out[0], x[0] / (1.0f + expf(-x[0])));
    bvn_close(c);

    printf("      %s; %s\n", bvml_version(), bvn_version());
    return fails;
}

static int cmd_lib(void)
{
    printf("BradDev Kit BVLibs self-check\n");
    int fails = bvlib_selfcheck();
    printf(fails ? "\n  %d check(s) FAILED\n" : "\n  all checks PASSED\n", fails);
    return fails ? 1 : 0;
}
#else
static int cmd_lib(void)
{
    fprintf(stderr, "braddev: built without BVLibs "
                    "(configure with -DBRAD_BUILD_BVLIB=ON)\n");
    return 2;
}
#endif

static int cmd_test(void)
{
    printf("BradDev Kit self-test\n");
    int fails = 0;

    /* driver: SPMP allocator */
    struct spmp_pool_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = 0;              /* SPMP_POOL_DRAM */
    cfg.size = 16 * 1024 * 1024;
    unsigned pid = 0;
    fails += check(spmp_init() == 0, "spmp_init");
    fails += check(spmp_pool_create(&cfg, &pid) == 0, "spmp_pool_create");
    struct spmp_allocation alloc;
    memset(&alloc, 0, sizeof(alloc));
    fails += check(spmp_alloc(pid, 4096, 4096, &alloc) == 0, "spmp_alloc 4KiB aligned");
    fails += check(spmp_free(&alloc) == 0, "spmp_free");
    uint64_t used = 0, total = 0;
    unsigned frag = 0;
    spmp_query_stats(pid, &used, &total, &frag);
    printf("      stats: used=%llu total=%llu frag=%u%%\n",
           (unsigned long long)used, (unsigned long long)total, frag);
    spmp_pool_destroy(pid);

    /* driver: EROE thermal governor */
    fails += check(eroe_init() == 0, "eroe_init");
    struct eroe_thermal_state th = eroe_get_thermal();
    printf("      thermal: junction=%uC board=%uC battery=%u%%\n",
           th.junction_c, th.board_c, th.battery_pct);

    /* driver: Fabric bind */
    fails += check(fabric_init() == 0, "fabric_init");
    struct fabric_node_info fni;
    memset(&fni, 0, sizeof(fni));
    fni.address_base = 0x90000000ull;
    fni.address_limit = 0x90000fffull;
    fails += check(fabric_register_node(&fni) == 0, "fabric_register_node");
    unsigned char fence[64];
    memset(fence, 0xA5, sizeof(fence));
    fails += check(fabric_bind_memory(0, fence, sizeof(fence)) == 0,
                   "fabric_bind_memory");

    /* toolchain: assemble + run + debug + profile the Kit's own smoke test */
    static const char smoke_src[] =
        ".kernel smoke\n"
        "    MOV S1, S16\n"
        "loop:\n"
        "    ISUB S1, S1, S17\n"
        "    CMP P0, S1, S0\n"
        "    BR_COND@P0 fin\n"
        "    BR loop\n"
        "fin:\n"
        "    EXIT\n"
        ".end\n";
    struct bradc_error err;
    memset(&err, 0, sizeof(err));
    uint64_t insn_buf[BVBC_MAX_INSN];
    struct bvbc_image img;
    memset(&img, 0, sizeof(img));
    img.insns = insn_buf;
    img.ninsn = 0;
    fails += check(bradc_assemble(smoke_src, &img, &err) == 0, "bradc_assemble smoke");
    if (err.line)
        printf("      bradc: line %d: %s\n", err.line, err.msg);
    if (img.ninsn == 0)
        return fails ? 1 : 0;

    bvrt_device *dev = bvrt_create(BVRT_MAX_MEM);
    fails += check(dev != NULL, "bvrt_create");
    if (dev) {
        fails += check(bvrt_load(dev, &img) == 0, "bvrt_load smoke");
        uint32_t sargs[2] = { 10u, 1u };
        fails += check(bvrt_launch(dev, "smoke", sargs, 2) == 0, "bvrt_launch smoke");
        enum bv_status st = bvrt_run(dev, 100000);
        struct bvrt_warp *w = bvrt_warp(dev);
        printf("      smoke: %s pc=%u cycles=%llu S1=%u\n",
               status_name(st), w->pc, (unsigned long long)w->cycles, w->s[0][1]);
        fails += check(st == BV_HALT && w->s[0][1] == 0, "smoke result (S1 countdown == 0)");
        bvrt_destroy(dev);
    }

#ifdef BRADDEV_HAVE_BVLIB
    printf("  BVLibs (BVML/BVN):\n");
    fails += bvlib_selfcheck();
#endif

    printf(fails ? "\n  %d check(s) FAILED\n" : "\n  all checks PASSED\n", fails);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { usage(argv[0]); return 2; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
        printf("%s\n", KIT_VERSION);
        return 0;
    }
    if (strcmp(cmd, "cvt") == 0)        return cmd_cvt(argc - 2, argv + 2);
    if (strcmp(cmd, "gpu") == 0) {
        if (argc >= 3 && strcmp(argv[2], "info") == 0) return cmd_gpu_info();
        if (argc >= 4 && strcmp(argv[2], "top") == 0)
            return cmd_gpu_top(argc - 3, argv + 3);
        usage(argv[0]);
        return 2;
    }
    if (strcmp(cmd, "run") == 0)        return cmd_run(argc - 2, argv + 2);
    if (strcmp(cmd, "dbg") == 0)        return cmd_dbg(argc - 2, argv + 2);
    if (strcmp(cmd, "timeline") == 0)   return cmd_timeline(argc - 2, argv + 2);
    if (strcmp(cmd, "test") == 0)       return cmd_test();
    if (strcmp(cmd, "lib") == 0)        return cmd_lib();

    usage(argv[0]);
    return 2;
}