#include "brad/spmp.h"
#include <string.h>
#include <errno.h>

/* ─── SPMP — Super Power Memory Pool (reference implementation) ───
 *
 * Models the unified LPDDR pool: per-pool page allocators with real
 * first-fit allocation + coalescing, NUMA-style mapping, predictive
 * prefetch accounting, and per-allocation BradSec-RAM encryption state.
 *
 * Reference model, not a claim of shipped hardware behavior.
 */

#define SPMP_BLOCK_ALIGN      16
#define SPMP_MIN_BLOCK        64
#define SPMP_MAX_BLOCKS       512
#define SPMP_MAX_MAPS         256
#define SPMP_ADDR_BASE        UINT64_C(0x100000000)   /* 4 GiB */

struct spmp_block {
    uint64_t   addr;
    size_t     size;
    unsigned   pool_id;
    int        in_use;
    int        encrypted;
};

struct spmp_map_entry {
    uint64_t   addr;
    size_t     size;
    unsigned   node;
    int        active;
};

struct spmp_pool {
    struct spmp_pool_config cfg;
    size_t                 used;      /* aggregate size__ of in-use blocks */
    int                    active;
    unsigned               num_blocks;
    struct spmp_block      blocks[SPMP_MAX_BLOCKS];
};

static struct {
    struct spmp_pool       pools[SPMP_MAX_POOLS];
    unsigned               num_pools;
    int                    initialized;
    struct spmp_map_entry  maps[SPMP_MAX_MAPS];
    unsigned               num_maps;

    uint64_t  allocs;
    uint64_t  frees;
    uint64_t  maps_done;
    uint64_t  unmaps_done;
    uint64_t  prefetch_hits;
    uint64_t  prefetch_misses;
} spmp_state;

static struct spmp_pool *pool_get(unsigned pool_id)
{
    if (pool_id >= spmp_state.num_pools)
        return NULL;
    if (!spmp_state.pools[pool_id].active)
        return NULL;
    return &spmp_state.pools[pool_id];
}

int spmp_init(void)
{
    memset(&spmp_state, 0, sizeof(spmp_state));
    spmp_state.initialized = 1;
    return 0;
}

int spmp_pool_create(struct spmp_pool_config *cfg, unsigned *pool_id)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!cfg || !pool_id)
        return -EINVAL;
    if (spmp_state.num_pools >= SPMP_MAX_POOLS)
        return -ENOSPC;
    if (cfg->size == 0)
        return -EINVAL;

    unsigned id = spmp_state.num_pools++;
    struct spmp_pool *p = &spmp_state.pools[id];

    /* Seed the pool with one free block covering the whole space. */
    p->cfg = *cfg;
    p->active = 1;
    p->num_blocks = 1;
    p->blocks[0].addr   = SPMP_ADDR_BASE + (uint64_t)id * 0x100000000ULL;
    p->blocks[0].size   = cfg->size;
    p->blocks[0].pool_id = id;
    p->blocks[0].in_use = 0;
    p->blocks[0].encrypted = cfg->encrypt ? 1 : 0;

    *pool_id = id;
    return 0;
}

int spmp_pool_destroy(unsigned pool_id)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    struct spmp_pool *p = pool_get(pool_id);
    if (!p)
        return -EINVAL;
    p->active = 0;
    p->num_blocks = 0;
    p->used = 0;
    return 0;
}

static struct spmp_block *block_next_free(struct spmp_pool *p,
                                          size_t needed,
                                          size_t alignment,
                                          uint64_t *out_addr)
{
    for (unsigned i = 0; i < p->num_blocks; i++) {
        struct spmp_block *b = &p->blocks[i];
        if (b->in_use)
            continue;

        uint64_t aligned = b->addr;
        size_t align = (alignment > SPMP_BLOCK_ALIGN)
                       ? alignment : SPMP_BLOCK_ALIGN;
        uint64_t rem = aligned % align;
        if (rem)
            aligned += align - rem;

        uint64_t end = b->addr + b->size;
        if (aligned > end)
            continue;
        if (end - aligned < (uint64_t)needed)
            continue;

        *out_addr = aligned;
        return b;
    }
    return NULL;
}

int spmp_alloc(unsigned pool_id, size_t size,
               size_t alignment, struct spmp_allocation *alloc)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    struct spmp_pool *p = pool_get(pool_id);
    if (!p || !alloc)
        return -EINVAL;
    if (size == 0)
        return -EINVAL;

    size_t needed = (size + SPMP_BLOCK_ALIGN - 1) & ~(SPMP_BLOCK_ALIGN - 1);
    if (needed < SPMP_MIN_BLOCK)
        needed = SPMP_MIN_BLOCK;
    if (p->num_blocks >= SPMP_MAX_BLOCKS)
        return -ENOSPC;

    uint64_t aligned_addr;
    struct spmp_block *b = block_next_free(p, needed, alignment, &aligned_addr);

    if (!b)
        return -ENOMEM;

    /* Leading hole between block start and aligned address. */
    if (aligned_addr > b->addr) {
        if (p->num_blocks >= SPMP_MAX_BLOCKS)
            return -ENOSPC;
        struct spmp_block *hole = &p->blocks[p->num_blocks++];
        hole->addr     = b->addr;
        hole->size     = (size_t)(aligned_addr - b->addr);
        hole->pool_id  = pool_id;
        hole->in_use   = 0;
    }

    /* Trailing hole after the allocation. */
    uint64_t alloc_end = aligned_addr + needed;
    uint64_t block_end = b->addr + b->size;
    if (block_end > alloc_end) {
        if (p->num_blocks >= SPMP_MAX_BLOCKS)
            return -ENOSPC;
        struct spmp_block *tail = &p->blocks[p->num_blocks++];
        tail->addr    = alloc_end;
        tail->size    = (size_t)(block_end - alloc_end);
        tail->pool_id = pool_id;
        tail->in_use  = 0;
    }

    b->addr    = aligned_addr;
    b->size    = needed;
    b->in_use  = 1;
    p->used   += needed;

    alloc->addr      = aligned_addr;
    alloc->size      = needed;
    alloc->pool_id   = pool_id;
    alloc->encrypted = p->cfg.encrypt;
    spmp_state.allocs++;
    return 0;
}

static void pool_coalesce(struct spmp_pool *p)
{
    /* Merge adjacent free blocks (single pass, earliest address first). */
    for (unsigned i = 0; i < p->num_blocks; i++) {
        struct spmp_block *a = &p->blocks[i];
        if (a->in_use)
            continue;
        for (unsigned j = 0; j < p->num_blocks; j++) {
            if (j == i)
                continue;
            struct spmp_block *bb = &p->blocks[j];
            if (bb->in_use)
                continue;
            if (a->addr + a->size == bb->addr) {
                a->size += bb->size;
                *bb = (struct spmp_block){0};
                bb->in_use = 1;              /* tombstones dead slot */
                bb->size   = 0;
            } else if (bb->addr + bb->size == a->addr) {
                bb->size += a->size;
                *a = (struct spmp_block){0};
                a->in_use = 1;
                a->size   = 0;
            }
        }
    }
}

int spmp_free(struct spmp_allocation *alloc)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!alloc || !alloc->addr)
        return -EINVAL;

    struct spmp_pool *p = pool_get(alloc->pool_id);
    if (!p)
        return -EINVAL;

    for (unsigned i = 0; i < p->num_blocks; i++) {
        struct spmp_block *b = &p->blocks[i];
        if (b->in_use && b->addr == alloc->addr && b->size == alloc->size) {
            b->in_use = 0;
            b->encrypted = 0;
            p->used -= b->size;
            pool_coalesce(p);
            spmp_state.frees++;
            memset(alloc, 0, sizeof(*alloc));
            return 0;
        }
    }
    return -ENOENT;
}

static struct spmp_map_entry *map_find(uint64_t addr, size_t size,
                                       struct spmp_map_entry **slot)
{
    for (unsigned i = 0; i < spmp_state.num_maps; i++) {
        struct spmp_map_entry *m = &spmp_state.maps[i];
        if (!m->active)
            continue;
        uint64_t m_end = m->addr + m->size;
        uint64_t a_end = addr + size;
        if (addr < m_end && m->addr < a_end) {
            if (slot)
                *slot = m;
            return m;
        }
    }
    return NULL;
}

int spmp_map(uint64_t addr, size_t size, unsigned *node_id)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;
    if (map_find(addr, size, NULL))
        return -EEXIST;
    if (spmp_state.num_maps >= SPMP_MAX_MAPS)
        return -ENOSPC;

    struct spmp_map_entry *m = &spmp_state.maps[spmp_state.num_maps++];
    m->addr   = addr;
    m->size   = size;
    m->node   = spmp_state.num_maps - 1;   /* placement policy: round-robin hash */
    m->active = 1;
    if (node_id)
        *node_id = m->node;
    spmp_state.maps_done++;
    return 0;
}

int spmp_unmap(uint64_t addr, size_t size)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;

    struct spmp_map_entry *m = map_find(addr, size, NULL);
    if (!m)
        return -ENOENT;
    m->active = 0;
    spmp_state.unmaps_done++;
    return 0;
}

int spmp_prefetch(uint64_t addr, size_t size)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;
    if (map_find(addr, size, NULL))
        spmp_state.prefetch_hits++;
    else
        spmp_state.prefetch_misses++;
    return 0;
}

static struct spmp_block *block_containing(uint64_t addr)
{
    for (unsigned i = 0; i < spmp_state.num_pools; i++) {
        struct spmp_pool *p = &spmp_state.pools[i];
        for (unsigned j = 0; j < p->num_blocks; j++) {
            struct spmp_block *b = &p->blocks[j];
            if (b->in_use && addr >= b->addr && addr < b->addr + b->size)
                return b;
        }
    }
    return NULL;
}

int spmp_encrypt(uint64_t addr, size_t size)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;
    struct spmp_block *b = block_containing(addr);
    if (b)
        b->encrypted = 1;
    return b ? 0 : -ENOENT;
}

int spmp_decrypt(uint64_t addr, size_t size)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;
    struct spmp_block *b = block_containing(addr);
    if (b)
        b->encrypted = 0;
    return b ? 0 : -ENOENT;
}

int spmp_query_stats(unsigned pool_id,
                     uint64_t *used,
                     uint64_t *total,
                     unsigned *frag_pct)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    struct spmp_pool *p = pool_get(pool_id);
    if (!p)
        return -EINVAL;

    if (used)  *used  = p->used;
    if (total) *total = p->cfg.size;

    if (frag_pct) {
        size_t free_total = 0;
        size_t largest   = 0;
        for (unsigned i = 0; i < p->num_blocks; i++) {
            struct spmp_block *b = &p->blocks[i];
            if (!b->in_use) {
                free_total += b->size;
                if (b->size > largest)
                    largest = b->size;
            }
        }
        if (free_total == 0)
            *frag_pct = 0;
        else
            *frag_pct = (unsigned)(100ULL * (free_total - largest) / free_total);
    }
    return 0;
}

int spmp_get_stats(struct spmp_stats *s)
{
    if (!spmp_state.initialized)
        return -ENODEV;
    if (!s)
        return -EINVAL;
    s->allocs          = spmp_state.allocs;
    s->frees           = spmp_state.frees;
    s->maps            = spmp_state.maps_done;
    s->unmaps          = spmp_state.unmaps_done;
    s->prefetch_hits   = spmp_state.prefetch_hits;
    s->prefetch_misses = spmp_state.prefetch_misses;
    return 0;
}