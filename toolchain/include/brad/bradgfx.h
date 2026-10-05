#ifndef BRAD_GFX_H
#define BRAD_GFX_H

#include <stdint.h>

#define BRAD_GFX_MAX_VEC     512
#define BRAD_GFX_MAX_RT      256
#define BRAD_GFX_MAX_NSU     192
#define BRAD_GFX_MAX_ANF     1024

#define GFX_MMIO_BASE        0x40000000UL
#define NSU_BASE             0x40300000UL

#define GFX_CTRL             0x0000
#define GFX_STATUS           0x0004
#define GFX_REVISION         0x0008
#define GFX_SKU_ID           0x000C
#define GFX_NUM_VEC          0x0010
#define GFX_NUM_RT           0x0014
#define GFX_NUM_NSU          0x0018
#define GFX_FABRIC_BW        0x001C
#define GFX_CLK_GATE         0x0020
#define GFX_PWR_STATE        0x0024
#define GFX_PWR_LIMIT        0x0028
#define GFX_TEMP_LIMIT       0x002C
#define GFX_VOLTAGE          0x0030
#define GFX_FREQ             0x0034
#define GFX_RESET            0x0038
#define GFX_INTR_ENABLE      0x003C
#define GFX_INTR_STATUS      0x0040
#define GFX_PERF_CNTR_0      0x0044
#define GFX_PERF_CNTR_1      0x004C
#define GFX_PERF_CNTR_CFG    0x0054
#define GFX_BRADSENSE_CTRL   0x0058
#define CMD_QUEUE_DOORBELL   0x1000
#define CMD_QUEUE_HEAD       0x1004
#define CMD_QUEUE_TAIL       0x1008
#define CMD_QUEUE_BASE_LO    0x100C
#define CMD_QUEUE_BASE_HI    0x1010
#define CMD_QUEUE_SIZE       0x1014

#define NSU_QUALITY_MODE     0x0024

#define GFX_CTRL_ENABLE             (1U << 0)
#define GFX_CTRL_FLUSH              (1U << 1)
#define GFX_CTRL_VRS_ENABLE         (1U << 8)
#define GFX_CTRL_NSU_ENABLE         (1U << 9)
#define GFX_CTRL_RAY_ENABLE         (1U << 10)
#define GFX_CTRL_BRADSENSE_ENABLE   (1U << 11)
#define GFX_STATUS_BUSY             (1U << 0)
#define GFX_STATUS_READY            (1U << 1)

struct brad_gfx_tier {
    unsigned num_vec;
    unsigned num_rt;
    unsigned num_nsu;
    unsigned freq_mhz;
    unsigned tdp_w;
    unsigned vram_gb;
};

static const struct brad_gfx_tier brad_gfx_tiers[] = {
    {  64,  32,  16, 1800, 120,  8 },
    { 128,  64,  32, 2000, 200, 16 },
    { 256, 128,  64, 2200, 350, 24 },
    { 512, 256, 192, 2500, 600, 48 },
};

enum brad_gfx_tier_id {
    BRAD_GFX_LITE     = 0,
    BRAD_GFX_STANDARD = 1,
    BRAD_GFX_ULTRA    = 2,
    BRAD_GFX_MAX      = 3,
};

int brad_gfx_init(enum brad_gfx_tier_id tier);
int brad_gfx_power_state(unsigned state);
int brad_gfx_submit_draw(unsigned count);
int brad_gfx_set_bradsense(int enable);

#endif
