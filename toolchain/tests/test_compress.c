#include "brad/bradcompress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(hdb_init() == 0);

    unsigned ratio;
    assert(hdb_query_ratio(HDB_PROFILE_TEXTURE, &ratio) == 0);
    assert(ratio == 280);

    assert(hdb_query_ratio(HDB_PROFILE_GEOMETRY, &ratio) == 0);
    assert(ratio == 350);

    unsigned decomp_gbs, comp_gbs;
    assert(hdb_query_speed(HDB_PROFILE_VIDEO, &decomp_gbs, &comp_gbs) == 0);
    assert(decomp_gbs == 45);
    assert(comp_gbs == 6);

    const char *input = "BradCompress hardware decompression block test data";
    size_t in_len = strlen(input) + 1;
    char comp[256];
    size_t comp_len = sizeof(comp);
    assert(hdb_compress(input, in_len, comp, &comp_len, HDB_PROFILE_GENERAL) == 0);
    assert(comp_len > 0);
    assert(comp_len <= in_len);

    char decomp[256];
    size_t decomp_len = sizeof(decomp);
    assert(hdb_decompress(comp, comp_len, decomp, &decomp_len, HDB_PROFILE_GENERAL) == 0);
    assert(decomp_len >= in_len);

    uint64_t compressed, decompressed, bins, bouts;
    unsigned errs;
    assert(hdb_query_stats(&compressed, &decompressed, &bins, &bouts, &errs) == 0);
    assert(compressed == 1);
    assert(decompressed == 1);
    assert(errs == 0);

    printf("BradCompress tests passed\n");
    return 0;
}
