#ifndef BRAD_FX_H
#define BRAD_FX_H

#include <stdint.h>

#define BRAD_FX_MAX_VEC 128

struct brad_fx_tier {
    unsigned vec_engines;
    unsigned rt_cores;
    unsigned nsu_units;
    unsigned freq_mhz;
    unsigned tdp_w;
};

static const struct brad_fx_tier brad_fx_tiers[] = {
    {  8,  0,  0,  800,  5 },   
    { 16,  4,  2, 1000, 12 },   
    { 24,  8,  4, 1200, 20 },   
    { 32, 16,  8, 1400, 35 },   
    { 48, 24, 12, 1600, 55 },   
};

enum brad_fx_tier_id {
    BRAD_FX_LITE     = 0,
    BRAD_FX_STANDARD = 1,
    BRAD_FX_ULTRA    = 2,
    BRAD_FX_MAX      = 3,
    BRAD_FX_MAX_I    = 4,
};

struct brad_fx_regs {
    volatile uint32_t FX_CTRL;
    volatile uint32_t FX_STATUS;
    volatile uint32_t FX_NUM_VEC;
    volatile uint32_t FX_NUM_RT;
    volatile uint32_t FX_NUM_NSU;
    volatile uint32_t FX_PWR_STATE;
    volatile uint32_t FX_CLK_GATE;
    volatile uint32_t FX_VRS_CTRL;
    volatile uint32_t FX_DRAW_COUNT;
    volatile uint32_t FX_PERF_CNTR;
    volatile uint64_t FX_AUC_BASE;
};

#define FX_CTRL_ENABLE         (1U << 0)
#define FX_CTRL_RESET          (1U << 1)
#define FX_CTRL_VRS_ENABLE     (1U << 8)
#define FX_CTRL_NSU_ENABLE     (1U << 9)
#define FX_STATUS_BUSY         (1U << 0)
#define FX_STATUS_READY        (1U << 1)

int brad_fx_init(enum brad_fx_tier_id tier);
int brad_fx_power_state(unsigned state);
int brad_fx_submit_draw(unsigned count);

#endif
