#include "brad/hypercore_atlas.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(HC_NPU_CORES(HC_300) == 32);
    assert(HC_MVRAM_BYTES(HC_ELITE) == 950ULL * 1024 * 1024);
    assert(HC_MVRAM_BYTES(HC_100) == 128ULL * 1024 * 1024);

    assert(hc_init(HC_500, 0x80000000ULL) == 0);
    hc_power_state(HC_P0_TURBO);

    unsigned ctx;
    assert(hc_auc_create_context(&ctx) == 0);
    assert(hc_context_switch(ctx) == 0);
    assert(hc_gpu_power_gate(1) == 0);
    assert(hc_sentinel_configure(SEN_WAKE_SRC_TIFA |
                                 SEN_WAKE_SRC_FACELOCK) == 0);

    printf("HyperCore tests passed\n");
    return 0;
}
