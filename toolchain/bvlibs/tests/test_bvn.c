#include "bvn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int check(const char *name, float got, float want)
{
    float tol = 1e-4f * (fabsf(want) + 1.0f);
    if (fabsf(got - want) > tol) {
        printf("FAIL %s: got %.6g want %.6g\n", name, got, want);
        return 1;
    }
    return 0;
}

static int run_case(struct bvn_context *c, size_t n)
{
    float *x = malloc(n * sizeof(float));
    float *out = malloc(n * sizeof(float));
    if (!x || !out) {
        printf("FAIL malloc (n=%zu)\n", n);
        free(x);
        free(out);
        return 1;
    }
    for (size_t i = 0; i < n; i++)
        x[i] = (float)(i % 7) - 3.0f;

    const float scale = 1.5f, bias = -0.25f;
    int bad = 0;

    if (bvn_relu(c, x, out, n) != 0) {
        printf("FAIL relu launch (n=%zu)\n", n);
        bad++;
    } else {
        for (size_t i = 0; i < n; i++)
            bad += check("relu", out[i], x[i] > 0.0f ? x[i] : 0.0f);
    }

    if (bvn_affine(c, scale, bias, x, out, n) != 0) {
        printf("FAIL affine launch (n=%zu)\n", n);
        bad++;
    } else {
        for (size_t i = 0; i < n; i++)
            bad += check("affine", out[i], scale * x[i] + bias);
    }

    if (bvn_softmax(c, x, out, n) != 0) {
        printf("FAIL softmax launch (n=%zu)\n", n);
        bad++;
    } else {
        float m = x[0];
        for (size_t i = 1; i < n; i++)
            if (x[i] > m)
                m = x[i];
        float sum = 0.0f;
        for (size_t i = 0; i < n; i++)
            sum += expf(x[i] - m);
        for (size_t i = 0; i < n; i++)
            bad += check("softmax", out[i], expf(x[i] - m) / sum);
    }

    if (bvn_gelu(c, x, out, n) != 0) {
        printf("FAIL gelu launch (n=%zu)\n", n);
        bad++;
    } else {
        for (size_t i = 0; i < n; i++)
            bad += check("gelu", out[i], x[i] / (1.0f + expf(-1.702f * x[i])));
    }

    if (bvn_silu(c, x, out, n) != 0) {
        printf("FAIL silu launch (n=%zu)\n", n);
        bad++;
    } else {
        for (size_t i = 0; i < n; i++)
            bad += check("silu", out[i], x[i] / (1.0f + expf(-x[i])));
    }

    free(x);
    free(out);
    return bad;
}

int main(void)
{
    const size_t sizes[] = { 1, 7, 64, 100 };
    const size_t ncases = sizeof(sizes) / sizeof(sizes[0]);

    struct bvn_context *c = bvn_open(1u << 20);
    if (!c) {
        printf("FAIL open — %s\n", bvn_version());
        return 1;
    }

    int bad = 0;
    for (size_t i = 0; i < ncases; i++)
        bad += run_case(c, sizes[i]);

    bvn_close(c);

    if (bad) {
        printf("BVN: %d check(s) FAILED\n", bad);
        return 1;
    }
    printf("BVN: all checks passed (relu/affine/softmax/gelu/silu, "
           "n=1/7/64/100) — %s\n", bvn_version());
    return 0;
}
