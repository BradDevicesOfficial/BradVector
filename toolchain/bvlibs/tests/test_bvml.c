#include "bvml.h"

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

static int run_case(struct bvml_context *c, size_t n)
{
    float *x = malloc(n * sizeof(float));
    float *y = malloc(n * sizeof(float));
    float *out = malloc(n * sizeof(float));
    if (!x || !y || !out) {
        printf("FAIL malloc (n=%zu)\n", n);
        free(x);
        free(y);
        free(out);
        return 1;
    }
    for (size_t i = 0; i < n; i++) {
        x[i] = (float)(i + 1);
        y[i] = 2.0f * (float)(i + 1);
    }

    int bad = 0;

    if (bvml_saxpy(c, 0.5f, x, y, out, n) != 0) {
        printf("FAIL saxpy launch (n=%zu)\n", n);
        bad++;
    } else {
        for (size_t i = 0; i < n; i++)
            bad += check("saxpy", out[i], 0.5f * x[i] + y[i]);
    }

    float d = 0.0f;
    if (bvml_dot(c, x, y, &d, n) != 0) {
        printf("FAIL dot launch (n=%zu)\n", n);
        bad++;
    } else {
        float want = 0.0f;
        for (size_t i = 0; i < n; i++)
            want += x[i] * y[i];
        bad += check("dot", d, want);
    }

    free(x);
    free(y);
    free(out);
    return bad;
}

static int gemm_case(struct bvml_context *c, size_t M, size_t N, size_t K)
{
    float alpha = 0.75f, beta = 0.5f;
    float *A = malloc((M * K) ? M * K * sizeof(float) : sizeof(float));
    float *B = malloc((K * N) ? K * N * sizeof(float) : sizeof(float));
    float *C = malloc((M * N) ? M * N * sizeof(float) : sizeof(float));
    float *ref = malloc((M * N) ? M * N * sizeof(float) : sizeof(float));
    if (!A || !B || !C || !ref) {
        printf("FAIL malloc (%zux%zux%zu)\n", M, N, K);
        return 1;
    }

    unsigned seed = (unsigned)(M * 131u + N * 17u + K * 3u) + 7u;
    for (size_t i = 0; i < M * K; i++) {
        seed = seed * 1103515245u + 12345u;
        A[i] = (float)((seed >> 8) & 0x3ffu) / 200.0f - 1.0f;
    }
    for (size_t i = 0; i < K * N; i++) {
        seed = seed * 1103515245u + 12345u;
        B[i] = (float)((seed >> 8) & 0x3ffu) / 200.0f - 1.0f;
    }
    for (size_t i = 0; i < M * N; i++) {
        seed = seed * 1103515245u + 12345u;
        C[i] = (float)((seed >> 8) & 0xffu) / 128.0f - 1.0f;
        ref[i] = beta * C[i];
    }
    for (size_t r = 0; r < M; r++)
        for (size_t c = 0; c < N; c++) {
            double s = 0.0;
            for (size_t kk = 0; kk < K; kk++)
                s += (double)A[r * K + kk] * (double)B[kk * N + c];
            ref[r * N + c] += (double)alpha * s;
        }

    int bad = 0;
    if (bvml_gemm(c, alpha, A, B, beta, C, M, N, K) != 0) {
        printf("FAIL gemm launch (%zux%zux%zu)\n", M, N, K);
        bad++;
    } else {
        for (size_t i = 0; i < M * N; i++) {
            float tol = 1e-3f * (fabsf((float)ref[i]) + 1.0f);
            if (fabsf(C[i] - (float)ref[i]) > tol) {
                printf("FAIL gemm[%zu] (%zux%zux%zu): got %.6g want %.6g\n",
                       i, M, N, K, C[i], (float)ref[i]);
                bad++;
            }
        }
    }

    free(A);
    free(B);
    free(C);
    free(ref);
    return bad;
}

int main(void)
{
    const size_t sizes[] = { 1, 7, 64, 100 };
    const size_t ncases = sizeof(sizes) / sizeof(sizes[0]);
    const struct { size_t M, N, K; } gemm_sizes[] = {
        { 1, 1, 1 }, { 2, 3, 4 }, { 5, 7, 3 }, { 8, 6, 5 },
        { 7, 9, 13 }, { 16, 12, 8 }, { 4, 5, 1 }, { 3, 4, 0 },
    };
    const size_t ngemm = sizeof(gemm_sizes) / sizeof(gemm_sizes[0]);

    struct bvml_context *c = bvml_open(1u << 20);
    if (!c) {
        printf("FAIL open — %s\n", bvml_version());
        return 1;
    }

    int bad = 0;
    for (size_t i = 0; i < ncases; i++)
        bad += run_case(c, sizes[i]);
    for (size_t i = 0; i < ngemm; i++)
        bad += gemm_case(c, gemm_sizes[i].M, gemm_sizes[i].N, gemm_sizes[i].K);

    bvml_close(c);

    if (bad) {
        printf("BVML: %d check(s) FAILED\n", bad);
        return 1;
    }
    printf("BVML: all checks passed (saxpy + dot, n=1/7/64/100; "
           "gemm, 8 shapes) — %s\n",
           bvml_version());
    return 0;
}
