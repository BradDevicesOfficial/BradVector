#include "brad/bradcore.h"
#include "brad/hypercore_atlas.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int tifa_callback_called = 0;
static void tifa_cb_fn(void *userdata)
{
    (void)userdata;
    tifa_callback_called = 1;
}

static void test_bradcore_sku_configs(void)
{
    struct brad_core_sku_cfg cfg;

    assert(brad_soc_init(BRAD_CORE_P1E4) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 1);
    assert(cfg.falcon_count == 4);
    assert(cfg.platform == BRAD_PLATFORM_MIND_LITE);
    assert(cfg.process == BRAD_PROCESS_4NM);
    assert(cfg.freq_max_mhz == 2400);
    assert(cfg.tdp_w == 10);

    assert(brad_soc_init(BRAD_CORE_P8E8) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 8);
    assert(cfg.falcon_count == 8);
    assert(cfg.platform == BRAD_PLATFORM_XON);
    assert(cfg.freq_max_mhz == 4200);
    assert(cfg.tdp_w == 95);
}

static void test_bradcore_all_skus(void)
{
    static const enum brad_core_sku all_skus[] = {
        BRAD_CORE_P1E4, BRAD_CORE_P2E4, BRAD_CORE_P4E4,
        BRAD_CORE_P6E4, BRAD_CORE_P4E8, BRAD_CORE_P8E4,
        BRAD_CORE_P8E8,
    };
    static const unsigned expected_p[] = { 1, 2, 4, 6, 4, 8, 8 };
    static const unsigned expected_f[] = { 4, 4, 4, 4, 8, 4, 8 };

    for (unsigned i = 0; i < 7; i++) {
        assert(brad_soc_init(all_skus[i]) == 0);
        struct brad_core_info info;
        assert(brad_core_query_info(&info) == 0);
        assert(info.total_phoenix == expected_p[i]);
        assert(info.total_falcon == expected_f[i]);
        assert(info.active_clusters > 0);
        assert(info.active_clusters <= 4);
    }
}

static void test_bradcore_cluster_management(void)
{
    assert(brad_soc_init(BRAD_CORE_P4E4) == 0);

    assert(brad_core_init(BRAD_CORE_PHOENIX, 0, 0) == 0);
    assert(brad_core_init(BRAD_CORE_FALCON, 0, 0) == 0);
    assert(brad_core_init(BRAD_CORE_PHOENIX, 1, 0) == -EINVAL);

    assert(brad_core_start(0, 0) == 0);
    assert(brad_core_start(0, 1) == 0);
    assert(brad_core_halt(0, 1) == 0);

    assert(brad_core_set_freq(0, 0, 3000) == 0);
    assert(brad_core_set_volt(0, 0, 1000) == 0);
    assert(brad_core_set_freq(0, 0, 6000) == -EINVAL);
    assert(brad_core_set_volt(0, 0, 200) == -EINVAL);

    assert(brad_core_set_power_state(0, 0, BRAD_PSTATE_TURBO) == 0);
    assert(brad_core_set_power_state(0, 0, BRAD_PSTATE_OFF) == 0);
    assert(brad_core_set_power_state(0, 0, 99) == -EINVAL);
}

static void test_bradcore_cluster_config(void)
{
    assert(brad_soc_init(BRAD_CORE_P8E8) == 0);

    struct brad_core_cluster_cfg cl_cfg = {
        .cluster_id = 0,
        .num_phoenix = 3,
        .num_falcon = 2,
        .l2_size_kb = 1024,
        .snoop_enabled = 1,
    };
    assert(brad_cluster_configure(&cl_cfg) == 0);

    assert(brad_cluster_set_l2_way(0, 0xF) == 0);
    assert(brad_core_set_l3_partition(0, 0xFF) == 0);

    unsigned temp;
    assert(brad_core_get_temp(0, 0, &temp) == 0);

    unsigned freqs[8];
    unsigned count = 8;
    assert(brad_core_query_freq_table(freqs, &count) == 0);
    assert(count == 5);
    assert(freqs[0] == 4200);
    assert(freqs[4] == 2400);
}

static void test_hypercore_sku_configs(void)
{
    struct hc_sku_cfg cfg;

    assert(hc_init(HC_100, 0x10000000ULL) == 0);
    assert(hc_query_sku(&cfg) == 0);
    assert(cfg.npu_cores == 16);
    assert(cfg.mvram_bytes == 128ULL * 1024 * 1024);
    assert(cfg.bandwidth_gbs == 64);
    assert(cfg.phoenix_count == 1);
    assert(cfg.gpu_vec_engines == 8);

    assert(hc_init(HC_ELITE, 0x20000000ULL) == 0);
    assert(hc_query_sku(&cfg) == 0);
    assert(cfg.npu_cores == 64);
    assert(cfg.mvram_bytes == 950ULL * 1024 * 1024);
    assert(cfg.bandwidth_gbs == 256);
    assert(cfg.phoenix_count == 6);
    assert(cfg.gpu_vec_engines == 32);
}

static void test_hypercore_power_and_context(void)
{
    assert(hc_init(HC_500, 0x30000000ULL) == 0);

    hc_power_state(HC_P0_TURBO);
    hc_power_state(HC_P3_RETENTION);

    assert(hc_context_switch(0) == -ENOENT);

    unsigned ctx_id;
    assert(hc_auc_create_context(&ctx_id) == 0);
    assert(ctx_id == 0);
    assert(hc_context_switch(ctx_id) == 0);

    assert(hc_auc_freeze_context(ctx_id) == 0);
    assert(hc_auc_destroy_context(ctx_id) == 0);
    assert(hc_context_switch(ctx_id) == -ENOENT);
}

static void test_hypercore_mvram(void)
{
    assert(hc_init(HC_300, 0x40000000ULL) == 0);

    struct hc_mvram_alloc alloc;
    assert(hc_mvram_alloc(65536, 1, &alloc) == 0);
    assert(alloc.size == 65536);
    assert(alloc.context_id == 1);

    assert(hc_mvram_pin(alloc.addr, alloc.size) == 0);
    assert(hc_mvram_free(&alloc) == 0);
}

static void test_hypercore_sentinel_tifa(void)
{
    assert(hc_init(HC_ELITE, 0x50000000ULL) == 0);

    assert(hc_sentinel_configure(SEN_WAKE_SRC_TIFA |
                                  SEN_WAKE_SRC_FACELOCK |
                                  SEN_WAKE_SRC_NOTIF) == 0);

    tifa_callback_called = 0;
    struct hc_tifa_callback cb = {
        .fn = tifa_cb_fn,
        .userdata = NULL,
    };
    assert(hc_tifa_set_listener(&cb) == 0);

    char input[64] = {0};
    char output[64] = {0};
    assert(hc_tifa_infer_async(input, sizeof(input),
                                output, sizeof(output),
                                1000) == 0);
    assert(tifa_callback_called == 1);
}

static void test_hypercore_bradlink(void)
{
    assert(hc_init(HC_ELITE, 0x60000000ULL) == 0);

    struct hc_bradlink_state bs;
    assert(hc_bradlink_get_state(&bs) == 0);
    assert(bs.tethered == 0);

    assert(hc_bradlink_tether("host:60000") == 0);
    assert(hc_bradlink_get_state(&bs) == 0);
    assert(bs.tethered == 1);
    assert(bs.bandwidth_mbps == 60000);

    assert(hc_bradlink_untether() == 0);
    assert(hc_bradlink_get_state(&bs) == 0);
    assert(bs.tethered == 0);
}

static void test_bradcore_naming(void)
{
    static const enum brad_core_sku skus[] = {
        BRAD_CORE_P1E4, BRAD_CORE_P2E4, BRAD_CORE_P4E4,
        BRAD_CORE_P6E4, BRAD_CORE_P4E8, BRAD_CORE_P8E4,
        BRAD_CORE_P8E8,
    };
    static const char *expected_names[] = {
        "BradCore K3-141", "BradCore K5-242", "BradCore K5-443",
        "BradCore K7-644", "BradCore K9-485", "BradCore Ultra 5-846",
        "BradCore Ultra 7-887",
    };
    static const unsigned expected_codes[] = { 141, 242, 443, 644, 485, 846, 887 };
    static const unsigned expected_phx[] = { 1, 2, 4, 6, 4, 8, 8 };
    static const unsigned expected_flc[] = { 4, 4, 4, 4, 8, 4, 8 };
    static const unsigned expected_plt[] = { 0, 1, 2, 3, 4, 5, 6 };

    for (unsigned i = 0; i < 7; i++) {
        assert(brad_soc_init(skus[i]) == 0);
        struct brad_core_naming n;
        assert(brad_core_query_naming(&n) == 0);
        assert(strcmp(n.consumer_name, expected_names[i]) == 0);
        assert(n.gen == BRAD_GEN_KINETIC);
        assert(n.hidden_sku_code == expected_codes[i]);
        assert(n.hidden_phoenix == expected_phx[i]);
        assert(n.hidden_falcon == expected_flc[i]);
        assert(n.hidden_platform == expected_plt[i]);
    }

    unsigned p, f;
    enum brad_platform_tier pl;
    assert(brad_core_decode_sku(443, &p, &f, &pl) == 0);
    assert(p == 4);
    assert(f == 4);
    assert(pl == BRAD_PLATFORM_MIND_PRO);

    assert(brad_core_decode_sku(887, &p, &f, &pl) == 0);
    assert(p == 8);
    assert(f == 8);
    assert(pl == BRAD_PLATFORM_XON);

    assert(brad_core_decode_sku(0, NULL, NULL, NULL) == -EINVAL);
    assert(brad_core_decode_sku(9999, NULL, NULL, NULL) == -EINVAL);
    assert(brad_core_decode_sku(999, NULL, NULL, NULL) == -EINVAL);
}

static void test_gaming_workstation_skus(void)
{
    struct brad_core_sku_cfg cfg;
    char model[16];

    assert(brad_soc_init(BRAD_CORE_G80) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 8);
    assert(cfg.falcon_count == 0);
    assert(cfg.freq_max_mhz == 5200);
    assert(cfg.tdp_w == 120);
    assert(cfg.series == BRAD_SERIES_G);
    assert(cfg.suffix == BRAD_SUFFIX_NONE);
    assert(brad_core_query_model_code(model, sizeof(model)) == 0);
    assert(strcmp(model, "G18-0") == 0);

    assert(brad_soc_init(BRAD_CORE_G60) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 6);
    assert(cfg.falcon_count == 0);
    assert(cfg.freq_max_mhz == 5000);

    assert(brad_soc_init(BRAD_CORE_W816) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 8);
    assert(cfg.falcon_count == 16);
    assert(cfg.freq_max_mhz == 3800);
    assert(cfg.tdp_w == 150);
    assert(cfg.l3_cache_kb == 98304);
    assert(cfg.series == BRAD_SERIES_W);
    assert(brad_core_query_model_code(model, sizeof(model)) == 0);
    assert(strcmp(model, "W18-16") == 0);

    assert(brad_soc_init(BRAD_CORE_W88) == 0);
    assert(brad_core_query_sku(&cfg) == 0);
    assert(cfg.phoenix_count == 8);
    assert(cfg.falcon_count == 8);
    assert(cfg.freq_max_mhz == 4000);
    assert(cfg.tdp_w == 125);
    assert(cfg.l3_cache_kb == 65536);
    assert(cfg.series == BRAD_SERIES_W);
    assert(brad_core_query_model_code(model, sizeof(model)) == 0);
    assert(strcmp(model, "W18-8") == 0);
}

static void test_model_code_and_series(void)
{
    char model[16];
    enum brad_core_series series;
    enum brad_core_suffix suffix;

    assert(brad_soc_init(BRAD_CORE_P8E8) == 0);
    assert(brad_core_query_model_code(model, sizeof(model)) == 0);
    assert(strcmp(model, "C18-8") == 0);
    assert(brad_core_query_series(&series, &suffix) == 0);
    assert(series == BRAD_SERIES_C);
    assert(suffix == BRAD_SUFFIX_NONE);

    struct brad_core_naming naming;
    assert(brad_core_query_naming(&naming) == 0);
    assert(strcmp(naming.model_code, "C18-8") == 0);
    assert(naming.series == BRAD_SERIES_C);
    assert(naming.suffix == BRAD_SUFFIX_NONE);

    assert(brad_soc_init(BRAD_CORE_P1E4) == 0);
    assert(brad_core_query_model_code(model, sizeof(model)) == 0);
    assert(strcmp(model, "C11-4") == 0);
}

int main(void)
{
    test_bradcore_sku_configs();
    test_bradcore_all_skus();
    test_bradcore_cluster_management();
    test_bradcore_cluster_config();
    test_bradcore_naming();
    test_gaming_workstation_skus();
    test_model_code_and_series();
    test_hypercore_sku_configs();
    test_hypercore_power_and_context();
    test_hypercore_mvram();
    test_hypercore_sentinel_tifa();
    test_hypercore_bradlink();

    printf("BradCore + HyperCore Atlas tests passed\n");
    return 0;
}
