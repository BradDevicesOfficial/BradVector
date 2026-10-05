#include "brad/eroe.h"
#include <string.h>
#include <errno.h>

/* ─── EROE — Eco-Render Optimization Engine (reference implementation) ───
 *
 * System-wide power/thermal governor.  Manages per-unit CPU/GPU/NPU/MEM
 * budgets from active profiles and declared workloads, with a simulated
 * thermal feedback loop, fan curves, and power/temp limit enforcement.
 *
 * Reference model, not a claim of shipped hardware behavior.
 */

#define EROE_AMBIENT_C          25
#define EROE_THERMAL_SCALE      120    /* junction delta at 100% load */
#define EROE_FAN_SILENT_RPM     800
#define EROE_FAN_BALANCED_RPM   2000
#define EROE_FAN_PERFORMANCE_RPM 3500
#define EROE_FAN_AGGRESSIVE_RPM 5000
#define EROE_DEFAULT_TDP        65
#define EROE_DEFAULT_TEMP_LIMIT 95

struct workload_entry {
    char        name[32];
    enum eroe_unit unit;
    float       load_factor;  /* 0.0–1.0 */
    int         active;       /* nonzero when slot is in use */
};

static const struct eroe_power_budget default_budgets[] = {
    [EROE_PROFILE_COMPUTE]  = { 80, 10, 10, 60 },
    [EROE_PROFILE_GAMING]   = { 30, 70, 20, 90 },
    [EROE_PROFILE_AI_INF]   = { 10, 10, 80, 70 },
    [EROE_PROFILE_VIDEO]    = { 10, 30,  0, 40 },
    [EROE_PROFILE_IDLE]     = {  5,  0,  0, 10 },
    [EROE_PROFILE_BATTERY]  = { 30, 20, 20, 30 },
    [EROE_PROFILE_CUSTOM]   = { 25, 25, 25, 50 },
};

static struct {
    enum eroe_profile         active_profile;
    struct eroe_power_budget base_budget;
    struct eroe_power_budget eff_budget;       /* final (with workload blending) */
    struct workload_entry    workloads[EROE_MAX_WORKLOADS];
    unsigned                 num_workloads;
    int                      initialized;

    /* Thermal / fan */
    unsigned                 fan_rpm;
    unsigned                 fan_curve_id;
    unsigned                 junction_c;
    unsigned                 hotspot_c;
    unsigned                 board_c;
    unsigned                 battery_pct;
    unsigned                 power_cap_w;
    unsigned                 temp_limit_c;
} eroe_state;

/* ─── profile preset lookup ─── */

static const struct eroe_power_budget *preset(enum eroe_profile p)
{
    if (p <= EROE_PROFILE_BATTERY)
        return &default_budgets[p];
    return &default_budgets[EROE_PROFILE_CUSTOM];
}

/* ─── init / profile switching ─── */

int eroe_init(void)
{
    memset(&eroe_state, 0, sizeof(eroe_state));
    eroe_state.active_profile = EROE_PROFILE_IDLE;
    eroe_state.base_budget    = *preset(EROE_PROFILE_IDLE);
    eroe_state.eff_budget     = eroe_state.base_budget;
    eroe_state.fan_rpm        = EROE_FAN_BALANCED_RPM;
    eroe_state.fan_curve_id   = EROE_FAN_BALANCED;
    eroe_state.junction_c     = EROE_AMBIENT_C + 10;
    eroe_state.hotspot_c      = EROE_AMBIENT_C + 15;
    eroe_state.board_c        = EROE_AMBIENT_C + 5;
    eroe_state.battery_pct    = 85;
    eroe_state.power_cap_w    = 0;
    eroe_state.temp_limit_c   = EROE_DEFAULT_TEMP_LIMIT;
    eroe_state.initialized    = 1;
    return 0;
}

int eroe_set_profile(enum eroe_profile profile)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (profile > EROE_PROFILE_CUSTOM)
        return -EINVAL;

    eroe_state.active_profile = profile;
    eroe_state.base_budget    = *preset(profile);
    /* Reset dynamic workloads on profile change (honest reset). */
    memset(eroe_state.workloads, 0, sizeof(eroe_state.workloads));
    eroe_state.num_workloads = 0;
    eroe_state.eff_budget    = eroe_state.base_budget;
    return 0;
}

int eroe_set_custom_profile(struct eroe_profile_config *cfg)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (!cfg)
        return -EINVAL;

    eroe_state.active_profile = EROE_PROFILE_CUSTOM;
    eroe_state.base_budget    = cfg->budget;
    memset(eroe_state.workloads, 0, sizeof(eroe_state.workloads));
    eroe_state.num_workloads = 0;
    eroe_state.eff_budget    = eroe_state.base_budget;
    return 0;
}

/* ─── workload declaration ─── */

int eroe_declare_workload(const char *name,
                          enum eroe_unit unit,
                          float load_factor)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (!name || load_factor < 0.0f || load_factor > 1.0f)
        return -EINVAL;

    /* Find existing workload by name or first free slot. */
    int slot = -1;
    for (unsigned i = 0; i < EROE_MAX_WORKLOADS; i++) {
        struct workload_entry *w = &eroe_state.workloads[i];
        if (w->active && strcmp(w->name, name) == 0) {
            slot = (int)i;
            break;
        }
        if (!w->active && slot < 0)
            slot = (int)i;
    }
    if (slot < 0)
        return -ENOSPC;

    struct workload_entry *w = &eroe_state.workloads[slot];
    strncpy(w->name, name, sizeof(w->name) - 1);
    w->name[sizeof(w->name) - 1] = '\0';
    w->unit        = unit;
    w->load_factor = load_factor;
    w->active      = (load_factor > 0.0f) ? 1 : 0;

    if (w->active) {
        /* Re-count. */
        unsigned n = 0;
        for (unsigned i = 0; i < EROE_MAX_WORKLOADS; i++)
            if (eroe_state.workloads[i].active)
                n++;
        eroe_state.num_workloads = n;
    }
    return 0;
}

int eroe_get_workloads(struct eroe_workload_info *out, unsigned *count)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (!count)
        return -EINVAL;

    unsigned n = 0;
    if (out) {
        for (unsigned i = 0; i < EROE_MAX_WORKLOADS && n < *count; i++) {
            struct workload_entry *w = &eroe_state.workloads[i];
            if (!w->active)
                continue;
            strncpy(out[n].name, w->name, sizeof(out[n].name) - 1);
            out[n].name[sizeof(out[n].name) - 1] = '\0';
            out[n].unit        = w->unit;
            out[n].load_factor = w->load_factor;
            n++;
        }
    }
    *count = n;
    return 0;
}

/* ─── thermal simulation ─── */

static void thermal_update(void)
{
    /* Compute effective load per unit from declared workloads. */
    float unit_load[4] = {0.0f};
    unsigned unit_count[4] = {0, 0, 0, 0};
    for (unsigned i = 0; i < EROE_MAX_WORKLOADS; i++) {
        struct workload_entry *w = &eroe_state.workloads[i];
        if (!w->active)
            continue;
        unit_load[w->unit] += w->load_factor;
        unit_count[w->unit]++;
    }

    /* Normalize load to [0,1] and compute weighted contribution to heat. */
    float heat_frac = 0.0f;
    for (unsigned u = 0; u < 4; u++) {
        if (unit_count[u] == 0)
            continue;
        float avg = unit_load[u];
        /* Weight CPU/NPU hottest, GPU medium, mem cooler. */
        float weights[4] = {1.0f, 0.85f, 0.95f, 0.35f};
        heat_frac += avg * weights[u];
    }
    if (heat_frac > 1.0f)
        heat_frac = 1.0f;

    /* Fan cooling effect. */
    float fan_factor = 0.0f;
    switch (eroe_state.fan_curve_id) {
    case EROE_FAN_SILENT:       fan_factor = 0.15f; break;
    case EROE_FAN_BALANCED:     fan_factor = 0.30f; break;
    case EROE_FAN_PERFORMANCE:  fan_factor = 0.50f; break;
    case EROE_FAN_AGGRESSIVE:   fan_factor = 0.70f; break;
    default:                    fan_factor = 0.30f; break;
    }

    unsigned delta = (unsigned)(EROE_THERMAL_SCALE * heat_frac *
                                (1.0f - fan_factor));
    unsigned limit_delta = 0;
    if (eroe_state.power_cap_w > 0 &&
        eroe_state.power_cap_w < EROE_DEFAULT_TDP)
        limit_delta = (EROE_DEFAULT_TDP - eroe_state.power_cap_w) / 2;

    eroe_state.junction_c = EROE_AMBIENT_C + delta;
    if (eroe_state.junction_c > EROE_AMBIENT_C + delta)
        eroe_state.junction_c = EROE_AMBIENT_C + delta;
    if (eroe_state.junction_c > limit_delta)
        eroe_state.junction_c -= limit_delta;
    else
        eroe_state.junction_c = EROE_AMBIENT_C;

    eroe_state.hotspot_c = eroe_state.junction_c + 5;
    eroe_state.board_c   = EROE_AMBIENT_C + delta / 4;
}

static void rebalance_budget(void)
{
    struct eroe_power_budget base = eroe_state.base_budget;

    if (eroe_state.num_workloads == 0) {
        /* No declared workloads: budget is the pure profile preset. */
        eroe_state.eff_budget = base;
        return;
    }

    /* Blend workload demand into the base budget.  Compute average load
     * factor per unit, scaled 0–50% adjustment, clamped to keep all
     * fields valid. */
    float avg_load[4] = {0.0f};
    unsigned cnt[4]   = {0, 0, 0, 0};
    for (unsigned i = 0; i < EROE_MAX_WORKLOADS; i++) {
        struct workload_entry *w = &eroe_state.workloads[i];
        if (!w->active)
            continue;
        avg_load[w->unit] += w->load_factor;
        cnt[w->unit]++;
    }
    for (unsigned u = 0; u < 4; u++)
        if (cnt[u] > 0)
            avg_load[u] /= (float)cnt[u];

    float adj[4];
    adj[0] = avg_load[0] * 50.0f;
    adj[1] = avg_load[1] * 50.0f;
    adj[2] = avg_load[2] * 50.0f;
    adj[3] = avg_load[3] * 15.0f;

    int new_cpu  = (int)base.cpu_pct + (int)adj[0];
    int new_gpu  = (int)base.gpu_pct + (int)adj[1];
    int new_npu  = (int)base.npu_pct + (int)adj[2];
    int new_mem  = (int)base.mem_pct + (int)adj[3];

    if (new_cpu < 0)
        new_cpu = 0;
    if (new_cpu > 100)
        new_cpu = 100;
    if (new_gpu < 0)
        new_gpu = 0;
    if (new_gpu > 100)
        new_gpu = 100;
    if (new_npu < 0)
        new_npu = 0;
    if (new_npu > 100)
        new_npu = 100;
    if (new_mem < 0)
        new_mem = 0;
    if (new_mem > 100)
        new_mem = 100;

    eroe_state.eff_budget.cpu_pct = (unsigned)new_cpu;
    eroe_state.eff_budget.gpu_pct = (unsigned)new_gpu;
    eroe_state.eff_budget.npu_pct = (unsigned)new_npu;
    eroe_state.eff_budget.mem_pct = (unsigned)new_mem;

    /* Enforce power cap by scaling proportionally. */
    if (eroe_state.power_cap_w > 0 &&
        eroe_state.power_cap_w < EROE_DEFAULT_TDP) {
        float scale = (float)eroe_state.power_cap_w /
                      (float)EROE_DEFAULT_TDP;
        eroe_state.eff_budget.cpu_pct =
            (unsigned)(eroe_state.eff_budget.cpu_pct * scale);
        eroe_state.eff_budget.gpu_pct =
            (unsigned)(eroe_state.eff_budget.gpu_pct * scale);
        eroe_state.eff_budget.npu_pct =
            (unsigned)(eroe_state.eff_budget.npu_pct * scale);
        eroe_state.eff_budget.mem_pct =
            (unsigned)(eroe_state.eff_budget.mem_pct * scale);
    }
}

/* ─── query / control ─── */

struct eroe_thermal_state eroe_get_thermal(void)
{
    thermal_update();
    struct eroe_thermal_state s = {
        .junction_c  = eroe_state.junction_c,
        .hotspot_c   = eroe_state.hotspot_c,
        .board_c     = eroe_state.board_c,
        .battery_pct = eroe_state.battery_pct,
    };
    return s;
}

struct eroe_power_budget eroe_get_budget(void)
{
    rebalance_budget();
    return eroe_state.eff_budget;
}

int eroe_set_fan_curve(unsigned curve_id)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (curve_id > EROE_FAN_AGGRESSIVE)
        return -EINVAL;
    eroe_state.fan_curve_id = curve_id;
    switch (curve_id) {
    case EROE_FAN_SILENT:       eroe_state.fan_rpm = EROE_FAN_SILENT_RPM;       break;
    case EROE_FAN_PERFORMANCE:  eroe_state.fan_rpm = EROE_FAN_PERFORMANCE_RPM;  break;
    case EROE_FAN_AGGRESSIVE:   eroe_state.fan_rpm = EROE_FAN_AGGRESSIVE_RPM;   break;
    default:                    eroe_state.fan_rpm = EROE_FAN_BALANCED_RPM;      break;
    }
    return 0;
}

int eroe_set_power_cap(unsigned watts)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    eroe_state.power_cap_w = watts;
    return 0;
}

int eroe_set_temp_limit(unsigned celsius)
{
    if (!eroe_state.initialized)
        return -ENODEV;
    if (celsius < 60 || celsius > 115)
        return -EINVAL;
    eroe_state.temp_limit_c = celsius;
    return 0;
}