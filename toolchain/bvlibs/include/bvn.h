#ifndef BVLIB_BVN_H
#define BVLIB_BVN_H

#include <stddef.h>
#include <stdint.h>

/* ─── BVN — BradVector Neural Library (reference) ───
 *
 * Correctness-first neural primitives for the BradVector platform, built
 * the same way as BVML: assemble a .bvbs kernel, launch on BVRT (the
 * single-warp reference runtime), copy results back.
 *
 * Phase 0 covers element-wise and reduction primitives.  The special
 * functions EXP2/LOG2/SIN/COS/RCP are now implemented in BVRT and the
 * assembler, so exp-based activations (softmax, GELU, SiLU) are available.
 *
 * Status: BVN Phase 0 — ReLU, affine, softmax, GELU and SiLU are
 * kernel-backed and tested against host references.
 */

struct bvn_context;

/* Open a context with `mem_bytes` of device scratch.  Loads and
 * assembles the bundled reference kernels.  NULL on failure. */
struct bvn_context *bvn_open(size_t mem_bytes);

/* Release a context. */
void bvn_close(struct bvn_context *ctx);

/* Library version string. */
const char *bvn_version(void);

/* out[i] = max(0, x[i]) over n elements.  Returns 0 on success. */
int bvn_relu(struct bvn_context *ctx, const float *x, float *out, size_t n);

/* out[i] = scale*x[i] + bias over n elements.  Returns 0 on success. */
int bvn_affine(struct bvn_context *ctx, float scale, float bias,
               const float *x, float *out, size_t n);

/* Numerically stable softmax over n elements (max-subtracted). */
int bvn_softmax(struct bvn_context *ctx, const float *x, float *out, size_t n);

/* GELU, sigmoid approximation: x / (1 + exp(-1.702*x)). */
int bvn_gelu(struct bvn_context *ctx, const float *x, float *out, size_t n);

/* SiLU / swish: x / (1 + exp(-x)). */
int bvn_silu(struct bvn_context *ctx, const float *x, float *out, size_t n);

#endif /* BVLIB_BVN_H */
