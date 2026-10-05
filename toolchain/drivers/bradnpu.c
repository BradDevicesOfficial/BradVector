#include "brad/bradnpu.h"
#include <string.h>
#include <errno.h>

static struct {
    struct brad_npu_ncc_config ncc;
    struct brad_npu_hb_aim_config hb_aim;
    int initialized;
} npu_state;

int brad_npu_init(const struct brad_npu_ncc_config *ncc,
                   const struct brad_npu_hb_aim_config *hb_aim)
{
    if (!ncc || !hb_aim)
        return -EINVAL;
    if (ncc->ncc_count > BRAD_NPU_MAX_NCC)
        return -EINVAL;
    if (hb_aim->size_bytes > BRAD_NPU_HB_AIM_MAX)
        return -EINVAL;

    memcpy(&npu_state.ncc, ncc, sizeof(*ncc));
    memcpy(&npu_state.hb_aim, hb_aim, sizeof(*hb_aim));
    npu_state.initialized = 1;
    return 0;
}

int brad_npu_power_state(enum brad_npu_p_state state)
{
    if (!npu_state.initialized)
        return -ENODEV;
    (void)state;
    return 0;
}

int brad_npu_set_priority(enum brad_npu_priority prio)
{
    if (!npu_state.initialized)
        return -ENODEV;
    (void)prio;
    return 0;
}

int brad_npu_moe_configure(unsigned num_experts, unsigned top_k)
{
    if (!npu_state.initialized)
        return -ENODEV;
    if (top_k < 1 || top_k > num_experts)
        return -EINVAL;
    (void)num_experts;
    return 0;
}

int brad_npu_schedule(unsigned ncc_mask_lo, unsigned ncc_mask_hi)
{
    if (!npu_state.initialized)
        return -ENODEV;
    (void)ncc_mask_lo;
    (void)ncc_mask_hi;
    return 0;
}

void brad_npu_wait_idle(void)
{
}

int brad_npu_ai_isa_exec(uint32_t opcode, uint64_t operand)
{
    if (!npu_state.initialized)
        return -ENODEV;
    (void)opcode;
    (void)operand;
    return 0;
}

int brad_npu_tifa_invoke(const void *model_weights,
                          const void *input,
                          void *output,
                          size_t input_size,
                          size_t output_size)
{
    if (!npu_state.initialized)
        return -ENODEV;
    if (!model_weights || !input || !output)
        return -EINVAL;
    (void)input_size;
    (void)output_size;
    return 0;
}
