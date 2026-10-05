#include "brad/hypercore_atlas.h"
#include <string.h>
#include <errno.h>

static const struct hc_sku_cfg hc_sku_configs[] = {
    {
        .sku             = HC_SKU_NOVA,
        .name            = "HC-100 Nova",
        .tier_id         = HC_100,
        .npu_cores       = 16,
        .mvram_bytes     = 128ULL * 1024 * 1024,
        .bandwidth_gbs   = 64,
        .phoenix_count   = 1,
        .sentinel_count  = 1,
        .ace_freq_mhz    = 1600,
        .gpu_vec_engines = 8,
        .gpu_nsu_units   = 0,
    },
    {
        .sku             = HC_SKU_PHONE,
        .name            = "HC-300 Phone",
        .tier_id         = HC_300,
        .npu_cores       = 32,
        .mvram_bytes     = 256ULL * 1024 * 1024,
        .bandwidth_gbs   = 128,
        .phoenix_count   = 2,
        .sentinel_count  = 1,
        .ace_freq_mhz    = 2000,
        .gpu_vec_engines = 16,
        .gpu_nsu_units   = 4,
    },
    {
        .sku             = HC_SKU_ULTRA,
        .name            = "HC-500 Ultra",
        .tier_id         = HC_500,
        .npu_cores       = 48,
        .mvram_bytes     = 450ULL * 1024 * 1024,
        .bandwidth_gbs   = 192,
        .phoenix_count   = 4,
        .sentinel_count  = 2,
        .ace_freq_mhz    = 2400,
        .gpu_vec_engines = 24,
        .gpu_nsu_units   = 8,
    },
    {
        .sku             = HC_SKU_ELITE,
        .name            = "HC-Elite",
        .tier_id         = HC_ELITE,
        .npu_cores       = 64,
        .mvram_bytes     = 950ULL * 1024 * 1024,
        .bandwidth_gbs   = 256,
        .phoenix_count   = 6,
        .sentinel_count  = 2,
        .ace_freq_mhz    = 2800,
        .gpu_vec_engines = 32,
        .gpu_nsu_units   = 12,
    },
};

static struct {
    unsigned tier;
    uint64_t mvram_base;
    int initialized;
    enum hc_p_state p_state;
    struct hc_sku_cfg sku;
    struct hc_auc_context contexts[HC_MAX_CONTEXTS];
    unsigned num_contexts;
    uint64_t context_switch_count;
    uint64_t mvram_alloc_total;
    unsigned gpu_power_gate_count;
    unsigned sentinel_wake_events;
    struct hc_tifa_callback tifa_cb;
    int tifa_registered;
    struct hc_bradlink_state bradlink;
    unsigned ace_clk_mhz;
    unsigned temp_c;
    uint64_t mvram_next_addr;
} hc_state;

static const struct hc_sku_cfg *get_sku_for_tier(unsigned tier)
{
    for (unsigned i = 0; i < HC_MAX_TIERS; i++)
        if (hc_sku_configs[i].tier_id == tier)
            return &hc_sku_configs[i];
    return NULL;
}

int hc_init(unsigned tier, uint64_t mvram_base)
{
    if (tier > HC_ELITE)
        return -EINVAL;
    if (!mvram_base)
        return -EINVAL;

    const struct hc_sku_cfg *cfg = get_sku_for_tier(tier);
    if (!cfg)
        return -EINVAL;

    memset(&hc_state, 0, sizeof(hc_state));
    hc_state.tier = tier;
    hc_state.mvram_base = mvram_base;
    hc_state.mvram_next_addr = mvram_base;
    hc_state.p_state = HC_P1_BALANCED;
    hc_state.ace_clk_mhz = cfg->ace_freq_mhz;
    hc_state.temp_c = 32;
    memcpy(&hc_state.sku, cfg, sizeof(hc_state.sku));
    hc_state.initialized = 1;
    return 0;
}

void hc_power_state(enum hc_p_state state)
{
    if (!hc_state.initialized)
        return;
    hc_state.p_state = state;
}

int hc_context_switch(uint32_t new_context_id)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (new_context_id >= HC_MAX_CONTEXTS)
        return -EINVAL;
    if (!hc_state.contexts[new_context_id].active)
        return -ENOENT;

    hc_state.context_switch_count++;
    return 0;
}

int hc_gpu_power_gate(int gated)
{
    if (!hc_state.initialized)
        return -ENODEV;

    (void)gated;
    hc_state.gpu_power_gate_count++;
    return 0;
}

int hc_sentinel_configure(uint32_t wake_sources)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (wake_sources == 0)
        return -EINVAL;
    (void)wake_sources;
    hc_state.sentinel_wake_events = 0;
    return 0;
}

int hc_query_sku(struct hc_sku_cfg *cfg)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;
    memcpy(cfg, &hc_state.sku, sizeof(*cfg));
    return 0;
}

int hc_query_stats(struct hc_perf_stats *stats)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!stats)
        return -EINVAL;

    unsigned active = 0;
    for (unsigned i = 0; i < HC_MAX_CONTEXTS; i++)
        if (hc_state.contexts[i].active)
            active++;

    memset(stats, 0, sizeof(*stats));
    stats->context_switches = hc_state.context_switch_count;
    stats->mvram_alloc_bytes = hc_state.mvram_alloc_total;
    stats->active_contexts = active;
    stats->sentinel_wake_events = hc_state.sentinel_wake_events;
    stats->gpu_power_gate_count = hc_state.gpu_power_gate_count;
    stats->ace_clk_mhz = hc_state.ace_clk_mhz;
    stats->temp_c = hc_state.temp_c;
    return 0;
}

int hc_mvram_alloc(size_t size, unsigned context_id,
                    struct hc_mvram_alloc *alloc)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!alloc || size == 0)
        return -EINVAL;
    if (context_id >= HC_MAX_CONTEXTS)
        return -EINVAL;

    uint64_t next = hc_state.mvram_next_addr + size;
    uint64_t limit = hc_state.mvram_base + hc_state.sku.mvram_bytes;
    if (next > limit)
        return -ENOMEM;

    alloc->addr = hc_state.mvram_next_addr;
    alloc->size = size;
    alloc->context_id = context_id;
    alloc->pinned = 0;
    hc_state.mvram_next_addr = next;
    hc_state.mvram_alloc_total += size;

    if (context_id > 0 && hc_state.contexts[context_id].active)
        hc_state.contexts[context_id].mvram_used += size;

    return 0;
}

int hc_mvram_free(struct hc_mvram_alloc *alloc)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!alloc)
        return -EINVAL;
    memset(alloc, 0, sizeof(*alloc));
    return 0;
}

int hc_mvram_pin(uint64_t addr, size_t size)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!addr || !size)
        return -EINVAL;
    (void)addr;
    (void)size;
    return 0;
}

int hc_auc_create_context(unsigned *context_id)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!context_id)
        return -EINVAL;

    for (unsigned i = 0; i < HC_MAX_CONTEXTS; i++) {
        if (!hc_state.contexts[i].active) {
            memset(&hc_state.contexts[i], 0, sizeof(hc_state.contexts[i]));
            hc_state.contexts[i].active = 1;
            hc_state.num_contexts++;
            *context_id = i;
            return 0;
        }
    }
    return -ENOSPC;
}

int hc_auc_destroy_context(unsigned context_id)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (context_id >= HC_MAX_CONTEXTS)
        return -EINVAL;
    if (!hc_state.contexts[context_id].active)
        return -ENOENT;

    memset(&hc_state.contexts[context_id], 0,
           sizeof(hc_state.contexts[context_id]));
    hc_state.num_contexts--;
    return 0;
}

int hc_auc_freeze_context(unsigned context_id)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (context_id >= HC_MAX_CONTEXTS)
        return -EINVAL;
    if (!hc_state.contexts[context_id].active)
        return -ENOENT;

    hc_state.contexts[context_id].frozen = 1;
    return 0;
}

int hc_auc_freeze_all(void)
{
    if (!hc_state.initialized)
        return -ENODEV;

    for (unsigned i = 0; i < HC_MAX_CONTEXTS; i++)
        if (hc_state.contexts[i].active)
            hc_state.contexts[i].frozen = 1;
    return 0;
}

int hc_gpu_submit_render(uint64_t cmd)
{
    if (!hc_state.initialized)
        return -ENODEV;
    (void)cmd;
    return 0;
}

int hc_gpu_wait_idle(uint64_t timeout_us)
{
    if (!hc_state.initialized)
        return -ENODEV;
    (void)timeout_us;
    return 0;
}

int hc_tifa_set_listener(struct hc_tifa_callback *cb)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!cb || !cb->fn)
        return -EINVAL;

    hc_state.tifa_cb = *cb;
    hc_state.tifa_registered = 1;
    return 0;
}

int hc_tifa_infer_async(const void *input, size_t input_len,
                         void *output, size_t output_len,
                         uint64_t timeout_us)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!input || !output)
        return -EINVAL;
    if (input_len == 0 || output_len == 0)
        return -EINVAL;
    (void)timeout_us;

    if (hc_state.tifa_registered) {
        hc_state.tifa_cb.fn(hc_state.tifa_cb.userdata);
        hc_state.sentinel_wake_events++;
    }

    memset(output, 0, output_len);
    return 0;
}

int hc_bradlink_get_state(struct hc_bradlink_state *state)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!state)
        return -EINVAL;
    memcpy(state, &hc_state.bradlink, sizeof(*state));
    return 0;
}

int hc_bradlink_tether(const char *remote_addr)
{
    if (!hc_state.initialized)
        return -ENODEV;
    if (!remote_addr)
        return -EINVAL;

    hc_state.bradlink.tethered = 1;
    hc_state.bradlink.bandwidth_mbps = 60000;
    hc_state.bradlink.latency_us = 100;
    (void)remote_addr;
    return 0;
}

int hc_bradlink_untether(void)
{
    if (!hc_state.initialized)
        return -ENODEV;

    memset(&hc_state.bradlink, 0, sizeof(hc_state.bradlink));
    return 0;
}
