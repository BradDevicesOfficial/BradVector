#include "bvml.h"
#include "brad/bradlib.h"
#include "bvml_kernels.h"

#include <stdlib.h>
#include <string.h>

#define BVML_MAX_INSNS 100000000ull

struct bvml_context {
    struct bradlib_program *prog;
    struct bradlib_session *sess;
    size_t mem;
};

static const char KERNEL_SRC[] =
    BVML_KERNEL_SAXPY "\n"
    BVML_KERNEL_DOT "\n"
    BVML_KERNEL_GEMM "\n";

const char *bvml_version(void) { return "BVML 0.2.0 (BVRT reference)"; }

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15; }

struct bvml_context *bvml_open(size_t mem_bytes)
{
    if (!mem_bytes)
        return NULL;
    struct bvml_context *c = calloc(1, sizeof(*c));
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

void bvml_close(struct bvml_context *ctx)
{
    if (!ctx)
        return;
    if (ctx->sess)
        bradlib_destroy(ctx->sess);
    if (ctx->prog)
        bradlib_free_program(ctx->prog);
    free(ctx);
}

static int launch1(struct bvml_context *c, const char *kernel,
                   const uint32_t *args, unsigned nargs)
{
    if (bradlib_launch(c->sess, kernel, args, nargs) != 0)
        return -3;
    return bradlib_run(c->sess, BVML_MAX_INSNS) == BV_HALT ? 0 : -4;
}

int bvml_saxpy(struct bvml_context *ctx, float a, const float *x,
               const float *y, float *out, size_t n)
{
    if (!ctx || !x || !y || !out)
        return -1;
    if (n == 0)
        return 0;

    size_t xoff = 0;
    size_t yoff = align16(n * sizeof(float));
    size_t ooff = align16(yoff + n * sizeof(float));
    if (ooff + n * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));
    memcpy(mem + yoff, y, n * sizeof(float));

    uint32_t abits, one = 1, step = 4;
    memcpy(&abits, &a, sizeof(abits));
    uint32_t args[7] = { (uint32_t)xoff, (uint32_t)yoff, (uint32_t)ooff,
                         abits, (uint32_t)n, step, one };

    int rc = launch1(ctx, "saxpy", args, 7);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, n * sizeof(float));
    return 0;
}

int bvml_dot(struct bvml_context *ctx, const float *x, const float *y,
             float *out, size_t n)
{
    if (!ctx || !x || !y || !out)
        return -1;
    if (n == 0) {
        *out = 0.0f;
        return 0;
    }

    size_t xoff = 0;
    size_t yoff = align16(n * sizeof(float));
    size_t ooff = align16(yoff + n * sizeof(float));
    if (ooff + sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    memcpy(mem + xoff, x, n * sizeof(float));
    memcpy(mem + yoff, y, n * sizeof(float));

    uint32_t one = 1, step = 4;
    uint32_t args[6] = { (uint32_t)xoff, (uint32_t)yoff, (uint32_t)ooff,
                         (uint32_t)n, step, one };

    int rc = launch1(ctx, "dot", args, 6);
    if (rc)
        return rc;
    memcpy(out, mem + ooff, sizeof(float));
    return 0;
}

int bvml_gemm(struct bvml_context *ctx, float alpha, const float *A,
              const float *B, float beta, float *C, size_t M, size_t N,
              size_t K)
{
    if (!ctx || !A || !B || !C)
        return -1;
    if (M == 0 || N == 0)
        return 0;

    size_t aoff = 0;
    size_t boff = align16(M * K * sizeof(float));
    size_t coff = align16(boff + K * N * sizeof(float));
    if (coff + M * N * sizeof(float) > ctx->mem)
        return -2;

    uint8_t *mem = bradlib_mem(ctx->sess);
    if (!mem)
        return -2;
    if (M * K)
        memcpy(mem + aoff, A, M * K * sizeof(float));
    if (K * N)
        memcpy(mem + boff, B, K * N * sizeof(float));
    if (M * N)
        memcpy(mem + coff, C, M * N * sizeof(float));

    uint32_t alpha_bits, beta_bits, one = 1, stepA = 4, stepB =
        (uint32_t)(N * sizeof(float));
    memcpy(&alpha_bits, &alpha, sizeof(alpha_bits));
    memcpy(&beta_bits, &beta, sizeof(beta_bits));

    for (size_t r = 0; r < M; r++) {
        for (size_t c = 0; c < N; c++) {
            uint32_t args[9] = {
                (uint32_t)(aoff + r * K * sizeof(float)),
                (uint32_t)(boff + c * sizeof(float)),
                (uint32_t)(coff + (r * N + c) * sizeof(float)),
                (uint32_t)K, stepA, stepB, one, alpha_bits, beta_bits,
            };
            int rc = launch1(ctx, "gemm", args, 9);
            if (rc)
                return rc;
        }
    }
    memcpy(C, mem + coff, M * N * sizeof(float));
    return 0;
}
