#ifndef BRAD_SPMP_H
#define BRAD_SPMP_H

#include <stdint.h>
#include <stddef.h>

#define SPMP_MAX_POOLS    8
#define SPMP_PAGE_SIZE    4096
#define SPMP_HUGE_PAGE    2097152

enum spmp_pool_type {
    SPMP_POOL_GRAPHICS  = 0,
    SPMP_POOL_COMPUTE   = 1,
    SPMP_POOL_AI        = 2,
    SPMP_POOL_OS        = 3,
    SPMP_POOL_GENERAL   = 4,
};

struct spmp_pool_config {
    enum spmp_pool_type   type;
    size_t                size;
    unsigned              numa_node;
    int                   huge_pages;
    int                   encrypt;
};

struct spmp_allocation {
    uint64_t              addr;
    size_t                size;
    unsigned              pool_id;
    int                   encrypted;
};

struct spmp_stats {
    uint64_t              allocs;
    uint64_t              frees;
    uint64_t              maps;
    uint64_t              unmaps;
    uint64_t              prefetch_hits;
    uint64_t              prefetch_misses;
};

int  spmp_init(void);
int  spmp_pool_create(struct spmp_pool_config *cfg,
                      unsigned *pool_id);
int  spmp_pool_destroy(unsigned pool_id);
int  spmp_alloc(unsigned pool_id, size_t size,
                size_t alignment,
                struct spmp_allocation *alloc);
int  spmp_free(struct spmp_allocation *alloc);
int  spmp_map(uint64_t addr, size_t size,
              unsigned *node_id);
int  spmp_unmap(uint64_t addr, size_t size);
int  spmp_prefetch(uint64_t addr, size_t size);
int  spmp_encrypt(uint64_t addr, size_t size);
int  spmp_decrypt(uint64_t addr, size_t size);
int  spmp_query_stats(unsigned pool_id,
                      uint64_t *used,
                      uint64_t *total,
                      unsigned *frag_pct);

/* Aggregate pool-wide accounting. */
int  spmp_get_stats(struct spmp_stats *s);

#endif
