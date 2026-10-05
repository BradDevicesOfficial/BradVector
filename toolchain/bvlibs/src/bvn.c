#include "bvn.h"
#include "brad/bradlib.h"
#include "bvn_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BVN_MAX_INSNS 100000000ull

struct bvn_context {
    struct bradlib_program *prog;
    struct bradlib_session *sess;
    size_t mem;
};

static const char KERNEL_SRC[] =
    BVN_KERNEL_RELU "\n"
    BVN_KERNEL_AFFINE "\n"
    BVN_KERNEL_SOFTMAX "\n"
    BVN_KERNEL_SIGMUL "\n";

const char *bvn_version(void) { return "BVN 0.2.0 (BVRT reference)"; }

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15; }

struct bvn_context *bvn_open(size_t mem_bytes)
{
    if (!mem_bytes)
        return NULL;
    struct bvn_context *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    struct bradc_error err;
    c->prog = bradlib_compile(KERNEL_SRC, &err);
    if (!c->prog) {
        free(c);
        return NULL;
    }
    c->sess = bradlib_create(c->prog, mem_bytes);
    if (!c->sess) {
        bradlib_free_program(c->prog);
        free(c);
        return NULL;
    }
    c->mem = mem_bytes;
    return c;
}

void bvn_close(struct bvn_context *ctx)
{
    if (!ctx)
        return;
    if (ctx->sess)
        bradlib_destroy(ctx->sess);
    if (ctx->prog)
        bradlib_free_program(ctx->prog);
    free(ctx);
}

static int launch1(struct bvn_context *c, const char *kernel,
                   const uint32_t *args, unsigned nargs)
{
    if (bradlib_launch(c->sess, kernel, args, nargs) != 0)
        return -3;
    return bradlib_run(c->sess, BVN_MAX_INSNS) == BV_HALT ? 0 : -4;
}

int bvn_relu(struct bvn_context *ctx, const float *x, float *out, size_t n)
{
    if (!ctx || !x || !out)
        return -1;
    if (n == 0)
        return 0;

    size_t xoff = 0;
    size_t ooff = align16(n * sizeof(float));
    if (ooff + n * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));

    uint32_t one = 1, step = 4;
    uint32_t args[5] = { (uint32_t)xoff, (uint32_t)ooff, (uint32_t)n, step, one };

    int rc = launch1(ctx, "relu", args, 5);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, n * sizeof(float));
    return 0;
}

int bvn_affine(struct bvn_context *ctx, float scale, float bias,
               const float *x, float *out, size_t n)
{
    if (!ctx || !x || !out)
        return -1;
    if (n == 0)
        return 0;

    size_t xoff = 0;
    size_t ooff = align16(n * sizeof(float));
    if (ooff + n * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));

    uint32_t sbits, bbits, one = 1, step = 4;
    memcpy(&sbits, &scale, sizeof(sbits));
    memcpy(&bbits, &bias, sizeof(bbits));
    uint32_t args[7] = { (uint32_t)xoff, (uint32_t)ooff, sbits, bbits,
                         (uint32_t)n, step, one };

    int rc = launch1(ctx, "affine", args, 7);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, n * sizeof(float));
    return 0;
}

#define BVN_LOG2E 1.4426950408889634f

static int sigmul(struct bvn_context *ctx, float c, const float *x,
                  float *out, size_t n)
{
    if (!ctx || !x || !out)
        return -1;
    if (n == 0)
        return 0;

    size_t xoff = 0;
    size_t ooff = align16(n * sizeof(float));
    if (ooff + n * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));

    uint32_t cbits, one = 1, step = 4;
    memcpy(&cbits, &c, sizeof(cbits));
    uint32_t args[6] = { (uint32_t)xoff, (uint32_t)ooff, (uint32_t)n,
                         step, one, cbits };

    int rc = launch1(ctx, "sigmul", args, 6);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, n * sizeof(float));
    return 0;
}

int bvn_gelu(struct bvn_context *ctx, const float *x, float *out, size_t n)
{
    return sigmul(ctx, -1.702f * BVN_LOG2E, x, out, n);
}

int bvn_silu(struct bvn_context *ctx, const float *x, float *out, size_t n)
{
    return sigmul(ctx, -BVN_LOG2E, x, out, n);
}

int bvn_softmax(struct bvn_context *ctx, const float *x, float *out, size_t n)
{
    if (!ctx || !x || !out)
        return -1;
    if (n == 0)
        return 0;

    size_t xoff = 0;
    size_t ooff = align16(n * sizeof(float));
    if (ooff + n * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));

    uint32_t lbits, one = 1, step = 4;
    float log2e = BVN_LOG2E;
    memcpy(&lbits, &log2e, sizeof(lbits));
    uint32_t args[6] = { (uint32_t)xoff, (uint32_t)ooff, (uint32_t)n,
                         step, one, lbits };

    int rc = launch1(ctx, "softmax", args, 6);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, n * sizeof(float));
    return 0;
}
