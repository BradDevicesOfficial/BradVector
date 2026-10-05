#include "brad/bradcore.h"
#include <string.h>
#include <errno.h>

static const struct brad_core_sku_cfg sku_configs[] = {
    {
        .sku            = BRAD_CORE_P1E4,
        .name           = "BradCore P1+E4",
        .model_code     = "C11-4",
        .phoenix_count  = 1,
        .falcon_count   = 4,
        .platform       = BRAD_PLATFORM_MIND_LITE,
        .process        = BRAD_PROCESS_4NM,
        .freq_max_mhz   = 2400,
        .freq_falcon_mhz = 1800,
        .tdp_w          = 10,
        .l3_cache_kb    = 4096,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P2E4,
        .name           = "BradCore P2+E4",
        .model_code     = "C12-4",
        .phoenix_count  = 2,
        .falcon_count   = 4,
        .platform       = BRAD_PLATFORM_MIND_STANDARD,
        .process        = BRAD_PROCESS_4NM,
        .freq_max_mhz   = 2800,
        .freq_falcon_mhz = 1900,
        .tdp_w          = 15,
        .l3_cache_kb    = 8192,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P4E4,
        .name           = "BradCore P4+E4",
        .model_code     = "C14-4",
        .phoenix_count  = 4,
        .falcon_count   = 4,
        .platform       = BRAD_PLATFORM_MIND_PRO,
        .process        = BRAD_PROCESS_3NM,
        .freq_max_mhz   = 3200,
        .freq_falcon_mhz = 2000,
        .tdp_w          = 25,
        .l3_cache_kb    = 12288,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P6E4,
        .name           = "BradCore P6+E4",
        .model_code     = "C16-4",
        .phoenix_count  = 6,
        .falcon_count   = 4,
        .platform       = BRAD_PLATFORM_MIND_ELITE,
        .process        = BRAD_PROCESS_3NM,
        .freq_max_mhz   = 3600,
        .freq_falcon_mhz = 2000,
        .tdp_w          = 40,
        .l3_cache_kb    = 16384,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P4E8,
        .name           = "BradCore P4+E8",
        .model_code     = "C14-8",
        .phoenix_count  = 4,
        .falcon_count   = 8,
        .platform       = BRAD_PLATFORM_NEURO_ULTRA,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 3800,
        .freq_falcon_mhz = 2200,
        .tdp_w          = 55,
        .l3_cache_kb    = 24576,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P8E4,
        .name           = "BradCore P8+E4",
        .model_code     = "C18-4",
        .phoenix_count  = 8,
        .falcon_count   = 4,
        .platform       = BRAD_PLATFORM_NEURO_ELITE,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 4000,
        .freq_falcon_mhz = 2200,
        .tdp_w          = 65,
        .l3_cache_kb    = 32768,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_P8E8,
        .name           = "BradCore P8+E8",
        .model_code     = "C18-8",
        .phoenix_count  = 8,
        .falcon_count   = 8,
        .platform       = BRAD_PLATFORM_XON,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 4200,
        .freq_falcon_mhz = 2400,
        .tdp_w          = 95,
        .l3_cache_kb    = 49152,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_G60,
        .name           = "BradCore G6+E0",
        .model_code     = "G16-0",
        .phoenix_count  = 6,
        .falcon_count   = 0,
        .platform       = BRAD_PLATFORM_XON,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 5000,
        .freq_falcon_mhz = 0,
        .tdp_w          = 85,
        .l3_cache_kb    = 24576,
        .series         = BRAD_SERIES_G,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_G80,
        .name           = "BradCore G8+E0",
        .model_code     = "G18-0",
        .phoenix_count  = 8,
        .falcon_count   = 0,
        .platform       = BRAD_PLATFORM_XON,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 5200,
        .freq_falcon_mhz = 0,
        .tdp_w          = 120,
        .l3_cache_kb    = 32768,
        .series         = BRAD_SERIES_G,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_W88,
        .name           = "BradCore W8+E8",
        .model_code     = "W18-8",
        .phoenix_count  = 8,
        .falcon_count   = 8,
        .platform       = BRAD_PLATFORM_XON,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 4000,
        .freq_falcon_mhz = 2600,
        .tdp_w          = 125,
        .l3_cache_kb    = 65536,
        .series         = BRAD_SERIES_W,
        .suffix         = BRAD_SUFFIX_NONE,
    },
    {
        .sku            = BRAD_CORE_W816,
        .name           = "BradCore W8+E16",
        .model_code     = "W18-16",
        .phoenix_count  = 8,
        .falcon_count   = 16,
        .platform       = BRAD_PLATFORM_XON,
        .process        = BRAD_PROCESS_2NM,
        .freq_max_mhz   = 3800,
        .freq_falcon_mhz = 2600,
        .tdp_w          = 150,
        .l3_cache_kb    = 98304,
        .series         = BRAD_SERIES_W,
        .suffix         = BRAD_SUFFIX_NONE,
    },
};

static const unsigned num_skus = sizeof(sku_configs) / sizeof(sku_configs[0]);

struct sku_naming {
    enum brad_core_sku          sku;
    const char                  consumer_name[BRAD_CORE_CONSUMER_NAME];
    const char                  model_code[BRAD_CORE_MODEL_CODE_LEN];
    enum brad_gen               gen;
    enum brad_core_consumer_tier consumer_tier;
    enum brad_core_series       series;
    enum brad_core_suffix       suffix;
    unsigned                    hidden_sku_code;
};

static const struct sku_naming sku_namings[] = {
    {
        .sku            = BRAD_CORE_P1E4,
        .consumer_name  = "BradCore K3-141",
        .model_code     = "C11-4",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_K3,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 141,
    },
    {
        .sku            = BRAD_CORE_P2E4,
        .consumer_name  = "BradCore K5-242",
        .model_code     = "C12-4",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_K5,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 242,
    },
    {
        .sku            = BRAD_CORE_P4E4,
        .consumer_name  = "BradCore K5-443",
        .model_code     = "C14-4",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_K5,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 443,
    },
    {
        .sku            = BRAD_CORE_P6E4,
        .consumer_name  = "BradCore K7-644",
        .model_code     = "C16-4",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_K7,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 644,
    },
    {
        .sku            = BRAD_CORE_P4E8,
        .consumer_name  = "BradCore K9-485",
        .model_code     = "C14-8",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_K9,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 485,
    },
    {
        .sku            = BRAD_CORE_P8E4,
        .consumer_name  = "BradCore Ultra 5-846",
        .model_code     = "C18-4",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA5,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 846,
    },
    {
        .sku            = BRAD_CORE_P8E8,
        .consumer_name  = "BradCore Ultra 7-887",
        .model_code     = "C18-8",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA7,
        .series         = BRAD_SERIES_C,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 887,
    },
    {
        .sku            = BRAD_CORE_G60,
        .consumer_name  = "BradCore GameForce G1-60",
        .model_code     = "G16-0",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA7,
        .series         = BRAD_SERIES_G,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 960,
    },
    {
        .sku            = BRAD_CORE_G80,
        .consumer_name  = "BradCore GameForce G1-80",
        .model_code     = "G18-0",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA9,
        .series         = BRAD_SERIES_G,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 980,
    },
    {
        .sku            = BRAD_CORE_W88,
        .consumer_name  = "BradCore ProForge W1-88",
        .model_code     = "W18-8",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA9,
        .series         = BRAD_SERIES_W,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 188,
    },
    {
        .sku            = BRAD_CORE_W816,
        .consumer_name  = "BradCore ProForge W1-816",
        .model_code     = "W18-16",
        .gen            = BRAD_GEN_KINETIC,
        .consumer_tier  = BRAD_CONSUMER_ULTRA9,
        .series         = BRAD_SERIES_W,
        .suffix         = BRAD_SUFFIX_NONE,
        .hidden_sku_code = 816,
    },
};

struct core_state {
    enum brad_core_type type;
    unsigned freq_mhz;
    unsigned volt_mv;
    enum brad_core_power_state p_state;
    int initialized;
    int running;
    uint64_t cycles;
};

struct cluster_state {
    unsigned num_phoenix;
    unsigned num_falcon;
    unsigned l2_size_kb;
    unsigned l2_way_mask;
    int snoop_enabled;
    int initialized;
};

static struct {
    enum brad_core_sku sku;
    int initialized;
    struct core_state cores[BRAD_CORE_MAX_CLUSTERS][BRAD_CORE_PHX_PER_CLUSTER];
    struct cluster_state clusters[BRAD_CORE_MAX_CLUSTERS];
    unsigned active_clusters;
    unsigned freq_current;
    enum brad_core_power_state p_state;
    unsigned temp_c;
} bc_state;

static const struct brad_core_sku_cfg *get_sku_cfg(enum brad_core_sku sku)
{
    for (unsigned i = 0; i < num_skus; i++)
        if (sku_configs[i].sku == sku)
            return &sku_configs[i];
    return NULL;
}

static int cluster_has_core(unsigned cluster, unsigned core_id, enum brad_core_type type)
{
    if (cluster >= bc_state.active_clusters)
        return 0;
    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);
    if (!cfg) return 0;
    if (type == BRAD_CORE_PHOENIX)
        return core_id < bc_state.clusters[cluster].num_phoenix;
    return core_id < bc_state.clusters[cluster].num_falcon;
}

int brad_soc_init(enum brad_core_sku sku)
{
    if (sku > BRAD_CORE_W816)
        return -EINVAL;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(sku);
    if (!cfg)
        return -EINVAL;

    memset(&bc_state, 0, sizeof(bc_state));
    bc_state.sku = sku;
    bc_state.freq_current = cfg->freq_max_mhz;
    bc_state.p_state = BRAD_PSTATE_BALANCED;
    bc_state.temp_c = 35;

    unsigned total_p = cfg->phoenix_count;
    unsigned total_f = cfg->falcon_count;
    bc_state.active_clusters = 0;

    for (unsigned c = 0; c < BRAD_CORE_MAX_CLUSTERS && (total_p > 0 || total_f > 0); c++) {
        unsigned np = (total_p > BRAD_CORE_PHX_PER_CLUSTER) ? BRAD_CORE_PHX_PER_CLUSTER : total_p;
        unsigned nf = (total_f > BRAD_CORE_FAL_PER_CLUSTER) ? BRAD_CORE_FAL_PER_CLUSTER : total_f;
        bc_state.clusters[c].num_phoenix = np;
        bc_state.clusters[c].num_falcon = nf;
        bc_state.clusters[c].l2_size_kb = 512;
        bc_state.clusters[c].l2_way_mask = 0xFF;
        bc_state.clusters[c].snoop_enabled = 1;
        bc_state.clusters[c].initialized = 1;

        for (unsigned i = 0; i < np; i++) {
            bc_state.cores[c][i].type = BRAD_CORE_PHOENIX;
            bc_state.cores[c][i].freq_mhz = cfg->freq_max_mhz;
            bc_state.cores[c][i].volt_mv = 950;
            bc_state.cores[c][i].p_state = BRAD_PSTATE_BALANCED;
            bc_state.cores[c][i].initialized = 1;
        }
        for (unsigned i = 0; i < nf; i++) {
            bc_state.cores[c][np + i].type = BRAD_CORE_FALCON;
            bc_state.cores[c][np + i].freq_mhz = cfg->freq_falcon_mhz;
            bc_state.cores[c][np + i].volt_mv = 700;
            bc_state.cores[c][np + i].p_state = BRAD_PSTATE_EFFICIENT;
            bc_state.cores[c][np + i].initialized = 1;
        }
        for (unsigned i = np + nf; i < BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER; i++)
            bc_state.cores[c][i].initialized = 0;

        bc_state.active_clusters++;
        total_p -= np;
        total_f -= nf;
    }

    bc_state.initialized = 1;
    return 0;
}

int brad_core_init(enum brad_core_type type,
                    unsigned cluster, unsigned core_in_cluster)
{
    if (!bc_state.initialized)
        return -ENODEV;

    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (!cluster_has_core(cluster, core_in_cluster, type))
        return -EINVAL;

    unsigned idx = core_in_cluster;
    if (type == BRAD_CORE_FALCON)
        idx += bc_state.clusters[cluster].num_phoenix;

    struct core_state *cs = &bc_state.cores[cluster][idx];
    cs->initialized = 1;
    cs->running = 0;
    cs->cycles = 0;
    cs->p_state = (type == BRAD_CORE_PHOENIX)
                  ? BRAD_PSTATE_BALANCED
                  : BRAD_PSTATE_EFFICIENT;
    return 0;
}

int brad_core_start(unsigned cluster, unsigned core)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;

    struct core_state *cs = &bc_state.cores[cluster][core];
    if (!cs->initialized)
        return -ENODEV;
    cs->running = 1;
    return 0;
}

int brad_core_halt(unsigned cluster, unsigned core)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;

    struct core_state *cs = &bc_state.cores[cluster][core];
    if (!cs->initialized)
        return -ENODEV;
    cs->running = 0;
    return 0;
}

int brad_core_set_freq(unsigned cluster, unsigned core, unsigned freq_mhz)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;
    if (freq_mhz < 100 || freq_mhz > 5000)
        return -EINVAL;

    bc_state.cores[cluster][core].freq_mhz = freq_mhz;
    return 0;
}

int brad_core_set_volt(unsigned cluster, unsigned core, unsigned volt_mv)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;
    if (volt_mv < 500 || volt_mv > 1500)
        return -EINVAL;

    bc_state.cores[cluster][core].volt_mv = volt_mv;
    return 0;
}

int brad_core_set_power_state(unsigned cluster, unsigned core, unsigned state)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;
    if (state > BRAD_PSTATE_OFF)
        return -EINVAL;

    bc_state.cores[cluster][core].p_state = (enum brad_core_power_state)state;
    return 0;
}

int brad_core_flush_cache(unsigned cluster, unsigned core,
                           int dcache, int icache)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;
    (void)dcache;
    (void)icache;
    return 0;
}

int brad_cluster_init(unsigned cluster)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= BRAD_CORE_MAX_CLUSTERS)
        return -EINVAL;

    bc_state.clusters[cluster].initialized = 1;
    bc_state.clusters[cluster].l2_way_mask = 0xFF;
    bc_state.clusters[cluster].snoop_enabled = 1;
    return 0;
}

int brad_cluster_set_l2_way(unsigned cluster, unsigned way_mask)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;

    bc_state.clusters[cluster].l2_way_mask = way_mask;
    return 0;
}

int brad_core_query_sku(struct brad_core_sku_cfg *cfg)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;

    const struct brad_core_sku_cfg *sc = get_sku_cfg(bc_state.sku);
    if (!sc)
        return -ENODEV;
    memcpy(cfg, sc, sizeof(*cfg));
    return 0;
}

int brad_core_query_info(struct brad_core_info *info)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!info)
        return -EINVAL;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);
    if (!cfg)
        return -ENODEV;

    memset(info, 0, sizeof(*info));
    info->sku = bc_state.sku;
    memcpy(&info->cfg, cfg, sizeof(info->cfg));
    info->active_clusters = bc_state.active_clusters;

    for (unsigned c = 0; c < bc_state.active_clusters; c++) {
        info->total_phoenix += bc_state.clusters[c].num_phoenix;
        info->total_falcon  += bc_state.clusters[c].num_falcon;
    }

    info->freq_current_mhz = bc_state.freq_current;
    info->p_state = bc_state.p_state;
    info->temp_c = bc_state.temp_c;

    uint64_t total_cycles = 0;
    for (unsigned c = 0; c < bc_state.active_clusters; c++)
        for (unsigned i = 0; i < BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER; i++)
            total_cycles += bc_state.cores[c][i].cycles;
    info->cycles_total = total_cycles;
    return 0;
}

int brad_core_query_freq_table(unsigned *freqs, unsigned *count)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!freqs || !count)
        return -EINVAL;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);
    if (!cfg)
        return -ENODEV;

    unsigned tbl[5] = {0};
    tbl[0] = cfg->freq_max_mhz;
    tbl[1] = (cfg->freq_max_mhz * 85 + 50) / 100;
    tbl[2] = (cfg->freq_max_mhz * 70 + 50) / 100;
    tbl[3] = (cfg->freq_max_mhz * 50 + 50) / 100;
    tbl[4] = cfg->freq_falcon_mhz;
    unsigned n = cfg->falcon_count > 0 ? 5 : 4;
    n = (*count < n) ? *count : n;
    for (unsigned i = 0; i < n; i++)
        freqs[i] = tbl[i];
    *count = n;
    return 0;
}

int brad_cluster_configure(struct brad_core_cluster_cfg *cfg)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;
    if (cfg->cluster_id >= BRAD_CORE_MAX_CLUSTERS)
        return -EINVAL;
    if (cfg->num_phoenix > BRAD_CORE_PHX_PER_CLUSTER ||
        cfg->num_falcon > BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;

    bc_state.clusters[cfg->cluster_id].num_phoenix = cfg->num_phoenix;
    bc_state.clusters[cfg->cluster_id].num_falcon = cfg->num_falcon;
    bc_state.clusters[cfg->cluster_id].l2_size_kb = cfg->l2_size_kb;
    bc_state.clusters[cfg->cluster_id].snoop_enabled = cfg->snoop_enabled;
    bc_state.clusters[cfg->cluster_id].initialized = 1;
    return 0;
}

int brad_core_set_l3_partition(unsigned cluster, unsigned way_mask)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    (void)way_mask;
    return 0;
}

int brad_core_get_temp(unsigned cluster, unsigned core, unsigned *temp_c)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (cluster >= bc_state.active_clusters)
        return -EINVAL;
    if (core >= BRAD_CORE_PHX_PER_CLUSTER + BRAD_CORE_FAL_PER_CLUSTER)
        return -EINVAL;
    if (!temp_c)
        return -EINVAL;
    *temp_c = bc_state.temp_c;
    return 0;
}

static const struct sku_naming *get_naming(enum brad_core_sku sku)
{
    for (unsigned i = 0; i < num_skus; i++)
        if (sku_namings[i].sku == sku)
            return &sku_namings[i];
    return NULL;
}

int brad_core_query_naming(struct brad_core_naming *naming)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!naming)
        return -EINVAL;

    const struct sku_naming *n = get_naming(bc_state.sku);
    if (!n)
        return -ENODEV;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);

    memset(naming, 0, sizeof(*naming));
    strncpy(naming->consumer_name, n->consumer_name,
            sizeof(naming->consumer_name) - 1);
    strncpy(naming->model_code, n->model_code,
            sizeof(naming->model_code) - 1);
    naming->gen = n->gen;
    naming->consumer_tier = n->consumer_tier;
    naming->series = n->series;
    naming->suffix = cfg->suffix;
    naming->hidden_sku_code = n->hidden_sku_code;
    naming->hidden_phoenix = cfg ? cfg->phoenix_count : 0;
    naming->hidden_falcon = cfg ? cfg->falcon_count : 0;
    naming->hidden_platform = cfg ? (unsigned)cfg->platform : 0;
    return 0;
}

int brad_core_decode_sku(unsigned hidden_code,
                          unsigned *phoenix, unsigned *falcon,
                          enum brad_platform_tier *platform)
{
    if (hidden_code < 100 || hidden_code > 999)
        return -EINVAL;

    unsigned p = hidden_code / 100;
    unsigned f = (hidden_code / 10) % 10;
    unsigned pl = hidden_code % 10;

    if (p < 1 || p > 8)  return -EINVAL;
    if (f < 4 || f > 8)  return -EINVAL;
    if (pl < 1 || pl > 7) return -EINVAL;
    pl -= 1;

    if (phoenix)  *phoenix  = p;
    if (falcon)   *falcon   = f;
    if (platform) *platform = (enum brad_platform_tier)pl;
    return 0;
}

int brad_core_query_model_code(char *buf, size_t buf_size)
{
    if (!bc_state.initialized)
        return -ENODEV;
    if (!buf || buf_size < 2)
        return -EINVAL;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);
    if (!cfg)
        return -ENODEV;

    strncpy(buf, cfg->model_code, buf_size - 1);
    buf[buf_size - 1] = '\0';

    char suffix[4] = "";
    switch (cfg->suffix) {
    case BRAD_SUFFIX_E:  strcpy(suffix, "E");  break;
    case BRAD_SUFFIX_U:  strcpy(suffix, "U");  break;
    case BRAD_SUFFIX_FP: strcpy(suffix, "FP"); break;
    default: break;
    }

    size_t cur = strlen(buf);
    size_t slen = strlen(suffix);
    if (cur + slen < buf_size)
        memcpy(buf + cur, suffix, slen);

    return 0;
}

int brad_core_query_series(enum brad_core_series *series,
                             enum brad_core_suffix *suffix)
{
    if (!bc_state.initialized)
        return -ENODEV;

    const struct brad_core_sku_cfg *cfg = get_sku_cfg(bc_state.sku);
    if (!cfg)
        return -ENODEV;

    if (series) *series = cfg->series;
    if (suffix) *suffix = cfg->suffix;
    return 0;
}
