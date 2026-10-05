#ifndef BRAD_HYPERCORE_ATLAS_H
#define BRAD_HYPERCORE_ATLAS_H

#include <stdint.h>
#include <stddef.h>

#define BRAD_HYPERCORE_GEN1_ATLAS   1
#define HC_MAX_TIERS                4
#define HC_MAX_CONTEXTS             256
#define HC_SENTINEL_POWER_UA        40

#define HC_100   0
#define HC_300   1
#define HC_500   2
#define HC_ELITE 3

#define HC_NPU_CORES(hc)   ((const unsigned[]){ 16, 32, 48, 64 }[(hc)])
#define HC_MVRAM_BYTES(hc) ((const size_t[]){    \
    128ULL * 1024 * 1024,                         \
    256ULL * 1024 * 1024,                         \
    450ULL * 1024 * 1024,                         \
    950ULL * 1024 * 1024  }[(hc)])

#define HC_MVRAM_BANDWIDTH_GBS(hc) ((const unsigned[]){ 64, 128, 192, 256 }[(hc)])

enum hc_p_state {
    HC_P0_TURBO      = 0,
    HC_P1_BALANCED   = 1,
    HC_P2_EFFICIENT  = 2,
    HC_P3_RETENTION  = 3,
    HC_P4_OFF        = 4,
};

enum hc_sku {
    HC_SKU_NOVA      = 0,
    HC_SKU_PHONE     = 1,
    HC_SKU_ULTRA     = 2,
    HC_SKU_ELITE     = 3,
};

struct hc_sku_cfg {
    enum hc_sku                 sku;
    const char                  name[32];
    unsigned                    tier_id;
    unsigned                    npu_cores;
    size_t                      mvram_bytes;
    unsigned                    bandwidth_gbs;
    unsigned                    phoenix_count;
    unsigned                    sentinel_count;
    unsigned                    ace_freq_mhz;
    unsigned                    gpu_vec_engines;
    unsigned                    gpu_nsu_units;
};

struct hc_mvram_config {
    uint64_t    base;
    size_t      size;
    uint32_t    tiling;
    uint8_t     ecc_enabled;
    uint8_t     retention_mode;
} __attribute__((packed));

struct hc_mvram_alloc {
    uint64_t    addr;
    size_t      size;
    unsigned    context_id;
    int         pinned;
};

struct hc_ace_regs {
    volatile uint32_t ACE_CTRL;
    volatile uint32_t ACE_STATUS;
    volatile uint32_t ACE_CLK_GATE;
    volatile uint32_t ACE_PWR_STATE;
    volatile uint64_t ACE_MVRAM_BASE;
    volatile uint64_t ACE_MVRAM_LIMIT;
    volatile uint32_t ACE_CONTEXT_ID;
    volatile uint32_t ACE_CONTEXT_SWITCH;
    volatile uint32_t ACE_INT_ENABLE;
    volatile uint32_t ACE_INT_STATUS;
};

struct hc_sentinel_regs {
    volatile uint32_t SEN_CTRL;
    volatile uint32_t SEN_STATUS;
    volatile uint32_t SEN_WAKE_SRC;
    volatile uint32_t SEN_TIMER_CFG;
    volatile uint32_t SEN_CLK_GATE;
    volatile uint32_t SEN_PWR_CTRL;
    volatile uint32_t SEN_TIFA_CTRL;
    volatile uint32_t SEN_FACELOCK_CTRL;
};

struct hc_gpu_regs {
    volatile uint32_t GPU_CTRL;
    volatile uint32_t GPU_STATUS;
    volatile uint32_t GPU_NUM_VEC;
    volatile uint32_t GPU_NUM_NSU;
    volatile uint32_t GPU_CLK_GATE;
    volatile uint32_t GPU_PWR_STATE;
    volatile uint32_t GPU_AUC_BASE_LO;
    volatile uint32_t GPU_AUC_BASE_HI;
    volatile uint32_t GPU_VRS_CTRL;
};

struct hc_auc_context {
    uint64_t    mvram_addr;
    size_t      mvram_used;
    uint32_t    gpu_state[128];
    int         frozen;
    int         active;
};

struct hc_tifa_callback {
    void      (*fn)(void *userdata);
    void       *userdata;
};

struct hc_bradlink_state {
    int         tethered;
    unsigned    bandwidth_mbps;
    unsigned    latency_us;
    uint64_t    bytes_sent;
    uint64_t    bytes_recv;
};

struct hc_perf_stats {
    uint64_t    context_switches;
    uint64_t    mvram_alloc_bytes;
    unsigned    active_contexts;
    unsigned    sentinel_wake_events;
    unsigned    gpu_power_gate_count;
    unsigned    ace_clk_mhz;
    unsigned    temp_c;
};

#define ACE_CTRL_ENABLE          (1U << 0)
#define ACE_CTRL_RESET           (1U << 1)
#define ACE_CTRL_PWR_GATE_GPU    (1U << 8)
#define ACE_CTRL_PWR_GATE_NPU    (1U << 9)
#define ACE_STATUS_READY         (1U << 0)
#define ACE_STATUS_CONTEXT_ACTIVE(reg) (((reg) >> 4) & 0xF)

#define SEN_CTRL_ENABLE          (1U << 0)
#define SEN_CTRL_TIFA_ENABLE     (1U << 1)
#define SEN_CTRL_FACELOCK_ENABLE (1U << 2)
#define SEN_WAKE_SRC_TIMER       (1U << 0)
#define SEN_WAKE_SRC_TIFA        (1U << 1)
#define SEN_WAKE_SRC_FACELOCK    (1U << 2)
#define SEN_WAKE_SRC_NOTIF       (1U << 3)

#define GPU_CTRL_ENABLE          (1U << 0)
#define GPU_CTRL_RESET           (1U << 1)
#define GPU_CTRL_AUC_ENABLE      (1U << 8)
#define GPU_PWR_STATE_MASK       0x7

int  hc_init(unsigned tier, uint64_t mvram_base);
void hc_power_state(enum hc_p_state state);
int  hc_context_switch(uint32_t new_context_id);
int  hc_gpu_power_gate(int gated);
int  hc_sentinel_configure(uint32_t wake_sources);
int  hc_query_sku(struct hc_sku_cfg *cfg);
int  hc_query_stats(struct hc_perf_stats *stats);
int  hc_mvram_alloc(size_t size, unsigned context_id,
                     struct hc_mvram_alloc *alloc);
int  hc_mvram_free(struct hc_mvram_alloc *alloc);
int  hc_mvram_pin(uint64_t addr, size_t size);
int  hc_auc_create_context(unsigned *context_id);
int  hc_auc_destroy_context(unsigned context_id);
int  hc_auc_freeze_context(unsigned context_id);
int  hc_auc_freeze_all(void);
int  hc_gpu_submit_render(uint64_t cmd);
int  hc_gpu_wait_idle(uint64_t timeout_us);
int  hc_tifa_set_listener(struct hc_tifa_callback *cb);
int  hc_tifa_infer_async(const void *input, size_t input_len,
                          void *output, size_t output_len,
                          uint64_t timeout_us);
int  hc_bradlink_get_state(struct hc_bradlink_state *state);
int  hc_bradlink_tether(const char *remote_addr);
int  hc_bradlink_untether(void);

#endif
