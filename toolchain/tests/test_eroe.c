#include "brad/eroe.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(eroe_init() == 0);

    assert(eroe_set_profile(EROE_PROFILE_GAMING) == 0);
    struct eroe_power_budget b = eroe_get_budget();
    assert(b.cpu_pct == 30);
    assert(b.gpu_pct == 70);
    assert(b.npu_pct == 20);

    assert(eroe_set_profile(EROE_PROFILE_AI_INF) == 0);
    b = eroe_get_budget();
    assert(b.npu_pct == 80);

    struct eroe_thermal_state t = eroe_get_thermal();
    assert(t.junction_c > 0);

    printf("EROE tests passed\n");
    return 0;
}
