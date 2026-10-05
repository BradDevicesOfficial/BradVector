#include "bvcomm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int eq_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (fabsf(b) + 1.0f);
}

static int test_allreduce(struct bvcomm_world *w, size_t count)
{
    int n = bvcomm_nranks(w);
    int bad = 0;
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (size_t j = 0; j < count; j++)
            buf[j] = (float)(r + 1) + 0.5f * (float)j;
    }
    if (bvcomm_allreduce_sum_f32(w, 0, count) != 0) {
        printf("FAIL allreduce launch (n=%d count=%zu)\n", n, count);
        return 1;
    }
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (size_t j = 0; j < count; j++) {
            float want = (float)(n * (n + 1) / 2) + (float)n * 0.5f * (float)j;
            if (!eq_f(buf[j], want)) {
                printf("FAIL allreduce n=%d count=%zu rank=%d j=%zu: "
                       "got %.4f want %.4f\n", n, count, r, j, buf[j], want);
                bad++;
                break;
            }
        }
    }
    return bad;
}

static int test_broadcast(struct bvcomm_world *w, size_t count)
{
    int n = bvcomm_nranks(w);
    int root = n / 2;
    int bad = 0;
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (size_t j = 0; j < count; j++)
            buf[j] = (r == root) ? (float)(1000 + j) : -999.0f;
    }
    if (bvcomm_broadcast_f32(w, root, 0, count) != 0) {
        printf("FAIL broadcast launch (n=%d count=%zu)\n", n, count);
        return 1;
    }
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (size_t j = 0; j < count; j++) {
            if (!eq_f(buf[j], (float)(1000 + j))) {
                printf("FAIL broadcast n=%d count=%zu rank=%d j=%zu\n",
                       n, count, r, j);
                bad++;
                break;
            }
        }
    }
    return bad;
}

static int test_allgather(struct bvcomm_world *w, size_t count)
{
    int n = bvcomm_nranks(w);
    int bad = 0;
    size_t in_off = 0;
    size_t out_off = count; /* disjoint from the input region */
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (size_t j = 0; j < count; j++)
            buf[in_off + j] = (float)(100 * r) + (float)j;
    }
    if (bvcomm_allgather_f32(w, in_off, out_off, count) != 0) {
        printf("FAIL allgather launch (n=%d count=%zu)\n", n, count);
        return 1;
    }
    for (int r = 0; r < n; r++) {
        float *buf = bvcomm_buf(w, r);
        for (int src = 0; src < n && bad == 0; src++)
            for (size_t j = 0; j < count; j++)
                if (!eq_f(buf[out_off + (size_t)src * count + j],
                          (float)(100 * src) + (float)j)) {
                    printf("FAIL allgather n=%d count=%zu dst=%d src=%d j=%zu\n",
                           n, count, r, src, j);
                    bad++;
                    break;
                }
    }
    return bad;
}

int main(void)
{
    const size_t counts[] = { 1, 5, 64, 130 };
    const size_t ncounts = sizeof(counts) / sizeof(counts[0]);
    const size_t buf_bytes = 8192;
    const size_t scratch = 4096;

    int bad = 0;
    for (int n = 1; n <= 4; n++) {
        struct bvcomm_world *w = bvcomm_init(n, buf_bytes, scratch);
        if (!w) {
            printf("FAIL init (n=%d) — %s\n", n, bvcomm_version());
            return 1;
        }
        for (size_t i = 0; i < ncounts; i++) {
            bad += test_allreduce(w, counts[i]);
            bad += test_broadcast(w, counts[i]);
            bad += test_allgather(w, counts[i]);
        }
        bvcomm_destroy(w);
    }

    if (bad) {
        printf("BVCOMM: %d check(s) FAILED\n", bad);
        return 1;
    }
    printf("BVCOMM: all checks passed (allreduce/broadcast/allgather, "
           "n=1..4) — %s\n", bvcomm_version());
    return 0;
}
