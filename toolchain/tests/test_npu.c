#include "brad/bradnpu.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void)
{
    struct brad_npu_ncc_config ncc = {
        .ncc_count = 48,
        .systolic_width = 512,
        .simd_width_fp16 = 32,
        .simd_width_fp32 = 16,
        .aggregation_kb = 256,
    };

    struct brad_npu_hb_aim_config hb = {
        .size_bytes = 128ULL * 1024 * 1024,
        .tile_count = 16,
        .tile_size = 8ULL * 1024 * 1024,
        .bandwidth_gbs = 2000,
        .retention_voltage_mv = 400,
    };

    assert(brad_npu_init(&ncc, &hb) == 0);
    assert(brad_npu_power_state(NPU_P0_TURBO) == 0);
    assert(brad_npu_set_priority(NPU_PRIO_HIGH) == 0);
    assert(brad_npu_moe_configure(64, 2) == 0);
    assert(brad_npu_schedule(0xFF, 0) == 0);

    int x = 1, y = 0;
    assert(brad_npu_tifa_invoke(NULL, &x, &y, 4, 4) == -EINVAL);
    assert(brad_npu_tifa_invoke(&x, &x, &y, 4, 4) == 0);

    printf("NPU tests passed\n");
    return 0;
}
