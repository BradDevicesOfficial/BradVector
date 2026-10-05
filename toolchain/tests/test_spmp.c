#include "brad/spmp.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(spmp_init() == 0);

    unsigned pool = 0;
    struct spmp_pool_config cfg = {
        .type = SPMP_POOL_GRAPHICS,
        .size = 1024 * 1024,       /* 1 MB pool */
        .numa_node = 0,
        .huge_pages = 0,
        .encrypt = 1,
    };
    assert(spmp_pool_create(&cfg, &pool) == 0);

    /* Allocation respects alignment. */
    struct spmp_allocation a, b;
    assert(spmp_alloc(pool, 128, 64, &a) == 0);
    assert(a.addr % 64 == 0);
    assert(a.size >= 128);
    assert(a.encrypted == 1);

    assert(spmp_alloc(pool, 256, 4096, &b) == 0);
    assert(b.addr % 4096 == 0);

    uint64_t used = 0, total = 0;
    unsigned frag = 0;
    assert(spmp_query_stats(pool, &used, &total, &frag) == 0);
    assert(used >= a.size + b.size);
    assert(total == cfg.size);

    /* Free + reuse works. */
    uint64_t a_addr = a.addr;
    assert(spmp_free(&a) == 0);
    struct spmp_allocation c;
    assert(spmp_alloc(pool, 128, 64, &c) == 0);
    assert(c.addr == a_addr);   /* first-fit reuses the top of the hole */
    assert(spmp_free(&c) == 0);
    assert(spmp_free(&b) == 0);

    /* Mapping + prefetch accounting. */
    struct spmp_stats st;
    assert(spmp_get_stats(&st) == 0);
    assert(st.allocs == 3 && st.frees == 3);

    unsigned node = 0;
    assert(spmp_map(0x100000000ULL, 4096, &node) == 0);
    assert(spmp_map(0x100000000ULL, 4096, NULL) == -EEXIST);
    assert(spmp_prefetch(0x100000000ULL, 64) == 0);     /* mapped  -> hit */
    assert(spmp_prefetch(0x800000000ULL, 64) == 0);     /* unmapped-> miss */

    assert(spmp_get_stats(&st) == 0);
    assert(st.maps == 1);
    assert(st.prefetch_hits == 1);
    assert(st.prefetch_misses == 1);

    assert(spmp_unmap(0x100000000ULL, 4096) == 0);
    assert(spmp_unmap(0x100000000ULL, 4096) == -ENOENT);

    /* Encryption toggle only touches live allocations. */
    assert(spmp_encrypt(0xDEAD0000ULL, 64) == -ENOENT);

    /* Real fragmentation: holes between live blocks stay unusable.
     * Fill a small pool almost completely so holes matter, then free
     * every other block in the middle.  Fragmentation % must be > 0. */
    unsigned p2 = 0;
    struct spmp_pool_config fcfg = {
        .type = SPMP_POOL_COMPUTE,
        .size = 16 * 1024,
        .numa_node = 1,
        .huge_pages = 0,
        .encrypt = 0,
    };
    assert(spmp_pool_create(&fcfg, &p2) == 0);
    struct spmp_allocation f[15];
    for (unsigned i = 0; i < 15; i++)
        assert(spmp_alloc(p2, 1024, 4, &f[i]) == 0);
    assert(spmp_free(&f[1]) == 0);
    assert(spmp_free(&f[3]) == 0);
    assert(spmp_free(&f[5]) == 0);

    assert(spmp_query_stats(p2, &used, &total, &frag) == 0);
    assert(frag > 0);
    assert(spmp_pool_destroy(p2) == 0);

    /* Pool destroy. */
    assert(spmp_pool_destroy(pool) == 0);
    assert(spmp_query_stats(pool, &used, &total, &frag) == -EINVAL);

    printf("SPMP tests passed\n");
    return 0;
}