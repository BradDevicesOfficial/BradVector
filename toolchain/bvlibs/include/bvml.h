#ifndef BVLIB_BVML_H
#define BVLIB_BVML_H

#include <stddef.h>
#include <stdint.h>

/* ─── BVML — BradVector Math Library (reference) ───
 *
 * A correctness-first math library for the BradVector platform.  Each
 * operation assembles a .bvbs kernel once, launches it on BVRT (the
 * single-warp reference runtime), and copies results back to the host.
 *
 * This is a reference model, not a performance claim: BVRT executes one
 * 32-lane warp, and the reference kernels evaluate the operation across
 * the warp.  Performance tiers belong to production silicon.
 *
 * Status: BVML Phase 0 — SAXPY, DOT, and GEMM are kernel-backed and tested.
 */

struct bvml_context;

/* Open a context with `mem_bytes` of device scratch.  Loads and
 * assembles the bundled reference kernels.  NULL on failure. */
struct bvml_context *bvml_open(size_t mem_bytes);

/* Release a context. */
void bvml_close(struct bvml_context *ctx);

/* Library version string. */
const char *bvml_version(void);

/* out[i] = a*x[i] + y[i] over n elements.  Returns 0 on success. */
int bvml_saxpy(struct bvml_context *ctx, float a, const float *x,
               const float *y, float *out, size_t n);

/* out[0] = sum over i of x[i]*y[i].  Returns 0 on success. */
int bvml_dot(struct bvml_context *ctx, const float *x, const float *y,
             float *out, size_t n);

/* C[M,N] = alpha*(A[M,K] * B[K,N]) + beta*C[M,N], row-major.
 * On success C is overwritten in place and returns 0. */
int bvml_gemm(struct bvml_context *ctx, float alpha, const float *A,
              const float *B, float beta, float *C, size_t M, size_t N,
              size_t K);

#endif /* BVLIB_BVML_H */
