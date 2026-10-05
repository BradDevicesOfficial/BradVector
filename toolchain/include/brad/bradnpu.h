#ifndef BRAD_NPU_H
#define BRAD_NPU_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_NPU_MAX_NCC   1024
#define BRAD_NPU_HB_AIM_MAX (256ULL * 1024 * 1024)

struct brad_npu_ncc_config {
    unsigned    ncc_count;
    unsigned    systolic_width;    
    unsigned    simd_width_fp16;
    unsigned    simd_width_fp32;
    unsigned    aggregation_kb;
};

struct brad_npu_hb_aim_config {
    size_t      size_bytes;
    unsigned    tile_count;
    size_t      tile_size;
    unsigned    bandwidth_gbs;
    int         retention_voltage_mv;
};

enum brad_npu_priority {
    NPU_PRIO_CRITICAL = 0,
    NPU_PRIO_HIGH     = 1,
    NPU_PRIO_MEDIUM   = 2,
    NPU_PRIO_LOW      = 3,
    NPU_PRIO_BULK     = 4,
};

enum brad_npu_p_state {
    NPU_P0_TURBO     = 0,
    NPU_P1_BALANCED  = 1,
    NPU_P2_EFFICIENT = 2,
    NPU_P3_RETENTION = 3,
    NPU_P4_OFF       = 4,
};

struct brad_npu_regs {
    volatile uint32_t NPU_CTRL;
    volatile uint32_t NPU_STATUS;
    volatile uint32_t NPU_NCC_COUNT;
    volatile uint32_t NPU_PWR_STATE;
    volatile uint32_t NPU_CLK_GATE;
    volatile uint32_t NPU_PRIORITY;
    volatile uint32_t NPU_INT_ENABLE;
    volatile uint32_t NPU_INT_STATUS;
    volatile uint64_t NPU_HB_AIM_BASE;
    volatile uint64_t NPU_HB_AIM_LIMIT;
    volatile uint32_t NPU_MOE_CTRL;
    volatile uint32_t NPU_MOE_EXPERT_COUNT;
    volatile uint32_t NPU_SCHED_CTRL;
    volatile uint32_t NPU_SCHED_NCC_MASK_LO;
    volatile uint32_t NPU_SCHED_NCC_MASK_HI;
    volatile uint32_t NPU_PERF_CNT_LO;
    volatile uint32_t NPU_PERF_CNT_HI;
};

#define NPU_CTRL_ENABLE          (1U << 0)
#define NPU_CTRL_RESET           (1U << 1)
#define NPU_CTRL_MOE_ENABLE      (1U << 8)
#define NPU_CTRL_SPARSE_ENABLE   (1U << 9)
#define NPU_STATUS_READY         (1U << 0)
#define NPU_STATUS_BUSY          (1U << 1)

#define NPU_MOE_CTRL_ENABLE      (1U << 0)
#define NPU_MOE_CTRL_TOP_K       (0x3 << 4)
#define NPU_MOE_CTRL_LOAD_BALANCE (1U << 8)

int  brad_npu_init(const struct brad_npu_ncc_config *ncc,
                   const struct brad_npu_hb_aim_config *hb_aim);
int  brad_npu_power_state(enum brad_npu_p_state state);
int  brad_npu_set_priority(enum brad_npu_priority prio);
int  brad_npu_moe_configure(unsigned num_experts, unsigned top_k);
int  brad_npu_schedule(unsigned ncc_mask_lo, unsigned ncc_mask_hi);
void brad_npu_wait_idle(void);

int  brad_npu_ai_isa_exec(uint32_t opcode, uint64_t operand);
int  brad_npu_tifa_invoke(const void *model_weights,
                          const void *input,
                          void *output,
                          size_t input_size,
                          size_t output_size);

#endif
