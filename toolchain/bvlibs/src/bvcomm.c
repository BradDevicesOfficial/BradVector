#include "bvcomm.h"
#include "brad/fabric.h"

#include <stdlib.h>
#include <string.h>

/* Each rank's address window is spaced 4 GiB apart, so a window must be
 * smaller than that (checked at init). */
#define BVCOMM_WINDOW_STRIDE (1ull << 32)
#define BVCOMM_MAX_RANKS     FABRIC_MAX_NODES

struct bvcomm_world {
    int nranks;
    size_t buf_bytes;
    size_t scratch_bytes;
    size_t window_bytes;
    uint8_t *mem[BVCOMM_MAX_RANKS];
    uint64_t base[BVCOMM_MAX_RANKS];
    struct fabric_channel ch[BVCOMM_MAX_RANKS][BVCOMM_MAX_RANKS];
};

const char *bvcomm_version(void) { return "BVCOMM 0.1.0 (BradFusion reference)"; }

int bvcomm_nranks(const struct bvcomm_world *w) { return w ? w->nranks : 0; }

void *bvcomm_buf(struct bvcomm_world *w, int rank)
{
    if (!w || rank < 0 || rank >= w->nranks)
        return NULL;
    return w->mem[rank];
}

struct bvcomm_world *bvcomm_init(int nranks, size_t buf_bytes,
                                 size_t scratch_bytes)
{
    if (nranks < 1 || nranks > BVCOMM_MAX_RANKS || buf_bytes == 0)
        return NULL;
    if (buf_bytes + scratch_bytes >= BVCOMM_WINDOW_STRIDE)
        return NULL;

    struct bvcomm_world *w = calloc(1, sizeof(*w));
    if (!w)
        return NULL;
    w->nranks = nranks;
    w->buf_bytes = buf_bytes;
    w->scratch_bytes = scratch_bytes;
    w->window_bytes = buf_bytes + scratch_bytes;

    if (fabric_init() != 0) {
        free(w);
        return NULL;
    }

    for (int r = 0; r < nranks; r++) {
        w->base[r] = (uint64_t)r * BVCOMM_WINDOW_STRIDE;
        w->mem[r] = calloc(1, w->window_bytes);
        if (!w->mem[r])
            goto fail;

        struct fabric_node_info info = {
            .type = FABRIC_NODE_GPU,
            .address_base = w->base[r],
            .address_limit = w->base[r] + w->window_bytes - 1,
            .bandwidth_gbs = 512,
            .latency_ns = 20,
        };
        if (fabric_register_node(&info) != 0)
            goto fail;
        if (fabric_bind_memory((unsigned)r, w->mem[r], w->window_bytes) != 0)
            goto fail;
    }

    for (int s = 0; s < nranks; s++)
        for (int d = 0; d < nranks; d++)
            if (fabric_channel_open((unsigned)s, (unsigned)d,
                                    FABRIC_CH_DMA | FABRIC_CH_READ |
                                        FABRIC_CH_WRITE,
                                    &w->ch[s][d]) != 0)
                goto fail;

    return w;

fail:
    bvcomm_destroy(w);
    return NULL;
}

void bvcomm_destroy(struct bvcomm_world *w)
{
    if (!w)
        return;
    for (int r = 0; r < w->nranks; r++)
        free(w->mem[r]);
    free(w);
}

/* Prefix offsets of `count` elements split as evenly as possible across
 * n ranks: chunk i has size base + (i < rem).  out[] holds n+1 offsets. */
static void chunk_offsets(size_t count, int n, size_t *out)
{
    size_t base = count / (size_t)n;
    size_t rem = count % (size_t)n;
    out[0] = 0;
    for (int i = 0; i < n; i++)
        out[i + 1] = out[i] + base + ((size_t)i < rem ? 1 : 0);
}

static size_t max_chunk_bytes(size_t count, int n)
{
    size_t base = count / (size_t)n;
    size_t rem = count % (size_t)n;
    size_t elems = base + (rem ? 1 : 0);
    return elems * sizeof(float);
}

int bvcomm_allreduce_sum_f32(struct bvcomm_world *w, size_t off, size_t count)
{
    if (!w)
        return -1;
    if (count == 0)
        return 0;
    if ((off + count) * sizeof(float) > w->buf_bytes)
        return -2;

    int n = w->nranks;
    if (n == 1)
        return 0;
    if (max_chunk_bytes(count, n) > w->scratch_bytes)
        return -2;

    size_t cs[BVCOMM_MAX_RANKS + 1];
    chunk_offsets(count, n, cs);

    /* Reduce-scatter: after this, rank r owns the full sum for chunk
     * (r + 1) mod n. */
    for (int s = 0; s < n - 1; s++) {
        for (int r = 0; r < n; r++) {
            int send = (r - s + n) % n;
            int nxt = (r + 1) % n;
            size_t len = (cs[send + 1] - cs[send]) * sizeof(float);
            if (len)
                fabric_dma(&w->ch[r][nxt],
                           w->base[r] + (off + cs[send]) * sizeof(float),
                           w->base[nxt] + w->buf_bytes, len);
        }
        for (int r = 0; r < n; r++) {
            int recv = (r - s - 1 + n) % n;
            size_t len = (cs[recv + 1] - cs[recv]) * sizeof(float);
            if (len) {
                float *dst = (float *)(w->mem[r] + (off + cs[recv]) * sizeof(float));
                float *inbox = (float *)(w->mem[r] + w->buf_bytes);
                size_t k = len / sizeof(float);
                for (size_t j = 0; j < k; j++)
                    dst[j] += inbox[j];
            }
        }
    }

    /* All-gather: propagate the reduced chunks around the ring. */
    for (int s = 0; s < n - 1; s++) {
        for (int r = 0; r < n; r++) {
            int send = (r - s + 1 + n) % n;
            int nxt = (r + 1) % n;
            size_t len = (cs[send + 1] - cs[send]) * sizeof(float);
            if (len)
                fabric_dma(&w->ch[r][nxt],
                           w->base[r] + (off + cs[send]) * sizeof(float),
                           w->base[nxt] + w->buf_bytes, len);
        }
        for (int r = 0; r < n; r++) {
            int recv = (r - s + n) % n;
            size_t len = (cs[recv + 1] - cs[recv]) * sizeof(float);
            if (len)
                memcpy(w->mem[r] + (off + cs[recv]) * sizeof(float),
                       w->mem[r] + w->buf_bytes, len);
        }
    }

    return 0;
}

int bvcomm_broadcast_f32(struct bvcomm_world *w, int root,
                         size_t off, size_t count)
{
    if (!w || root < 0 || root >= w->nranks)
        return -1;
    if (count == 0)
        return 0;
    if ((off + count) * sizeof(float) > w->buf_bytes)
        return -2;

    size_t len = count * sizeof(float);
    for (int r = 0; r < w->nranks; r++) {
        if (r == root)
            continue;
        fabric_dma(&w->ch[root][r],
                   w->base[root] + off * sizeof(float),
                   w->base[r] + off * sizeof(float), len);
    }
    return 0;
}

int bvcomm_allgather_f32(struct bvcomm_world *w, size_t in_off,
                         size_t out_off, size_t count)
{
    if (!w)
        return -1;
    if (count == 0)
        return 0;
    if ((in_off + count) * sizeof(float) > w->buf_bytes)
        return -2;

    int n = w->nranks;
    size_t total = (size_t)n * count;
    if ((out_off + total) * sizeof(float) > w->buf_bytes)
        return -2;
    if (count * sizeof(float) > w->scratch_bytes)
        return -2;

    /* Snapshot inputs into each rank's inbox so overlapping in/out
     * regions cannot corrupt a source mid-gather. */
    for (int r = 0; r < n; r++)
        memcpy(w->mem[r] + w->buf_bytes,
               w->mem[r] + in_off * sizeof(float), count * sizeof(float));

    for (int src = 0; src < n; src++)
        for (int dst = 0; dst < n; dst++)
            fabric_dma(&w->ch[src][dst],
                       w->base[src] + w->buf_bytes,
                       w->base[dst] + (out_off + (size_t)src * count) *
                                          sizeof(float),
                       count * sizeof(float));
    return 0;
}
