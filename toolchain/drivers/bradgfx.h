#ifndef BRAD_GFX_DRV_H
#define BRAD_GFX_DRV_H

#include <stdint.h>
#include "brad/bradgfx.h"

#define GFX_OK           0
#define GFX_ERR_INIT     -1
#define GFX_ERR_TIMEOUT  -2
#define GFX_ERR_MMU      -3
#define GFX_ERR_OOM      -4

enum brad_gfx_sku {
    GFX_SKU_LITE     = 0,
    GFX_SKU_STANDARD = 1,
    GFX_SKU_ULTRA    = 2,
    GFX_SKU_MAX      = 3,
    GFX_SKU_ANF      = 4,
};

enum gfx_power_state {
    GFX_PSTATE_TURBO     = 0,
    GFX_PSTATE_BALANCED  = 1,
    GFX_PSTATE_EFFICIENT = 2,
    GFX_PSTATE_RETENTION = 3,
    GFX_PSTATE_OFF       = 4,
};

#define GFX_MEM_READBACK   (1U << 0)
#define GFX_MEM_PERSISTENT (1U << 1)
#define GFX_MEM_FRAME      (1U << 2)

#define GFX_CMD_MEM        0x20000000UL
#define GFX_SHADER_MEM     0x20000000UL

#define NSU_BASE          0x40300000UL

#define GFX_CTRL_FLUSH           (1U << 1)
#define GFX_CTRL_RT_ENABLE       (1U << 4)

#define CMD_DRAW      1
#define CMD_DISPATCH  2
#define CMD_COPY      3
#define CMD_BARRIER   4
#define CMD_TIMESTAMP 5
#define CMD_WRITE_REG 6

struct gfx_cmd_queue {
    uint64_t    base_addr;
    uint32_t    size;
    volatile struct gfx_cmd_packet *ring;
    volatile uint32_t head;
    volatile uint32_t tail;
};

struct gfx_cmd_packet {
    uint32_t    type;
    uint32_t    flags;
    uint64_t    arg0;
    uint64_t    arg1;
    uint64_t    arg2;
    uint64_t    arg3;
    uint64_t    timestamp;
} __attribute__((packed));

struct gfx_buffer {
    uint64_t    addr;
    uint64_t    size;
    uint32_t    flags;
    uint32_t    refcount;
};

struct gfx_shader {
    uint64_t    handle;
    uint64_t    code_addr;
    uint32_t    code_size;
    uint32_t    num_warps;
};

struct gfx_driver {
    enum brad_gfx_sku   sku;
    uint32_t            num_vec;
    uint32_t            num_rt;
    uint32_t            num_nsu;
    uint32_t            freq_mhz;
    uint32_t            vram_mb;

    struct gfx_cmd_queue    cmdq;
    struct gfx_buffer       buffers[256];
    uint32_t                num_buffers;

    volatile uint8_t    *mmio_base;
    volatile uint8_t    *nsu_base;
};

int gfx_driver_init(struct gfx_driver *drv, volatile uint8_t *mmio_base);
int gfx_set_power_state(struct gfx_driver *drv, enum gfx_power_state state);
int gfx_get_power_state(struct gfx_driver *drv);
int gfx_set_freq(struct gfx_driver *drv, uint32_t freq_mhz);
int gfx_set_voltage(struct gfx_driver *drv, uint32_t voltage_mv);
struct gfx_buffer *gfx_alloc_buffer(struct gfx_driver *drv, uint64_t size, uint32_t flags);
int gfx_free_buffer(struct gfx_driver *drv, struct gfx_buffer *buf);
struct gfx_shader *gfx_create_shader(struct gfx_driver *drv, const void *code, uint32_t size);
int gfx_destroy_shader(struct gfx_driver *drv, struct gfx_shader *sh);
int gfx_bind_shader(struct gfx_driver *drv, struct gfx_shader *sh);
int gfx_submit_draw(struct gfx_driver *drv, uint32_t vertex_count, uint32_t instance_count,
                    struct gfx_shader *sh, struct gfx_buffer *vbuf);
int gfx_submit_dispatch(struct gfx_driver *drv, uint32_t gx, uint32_t gy, uint32_t gz,
                        struct gfx_shader *sh);
int gfx_submit_copy(struct gfx_driver *drv, struct gfx_buffer *src, struct gfx_buffer *dst,
                    uint64_t size);
int gfx_submit_barrier(struct gfx_driver *drv, uint32_t barrier_mask);
int gfx_kick(struct gfx_driver *drv);
int gfx_wait_idle(struct gfx_driver *drv, uint64_t timeout_us);
int gfx_query_timestamp(struct gfx_driver *drv, uint64_t *ts);
void gfx_irq_handler(struct gfx_driver *drv);
int gfx_set_bradsense(struct gfx_driver *drv, int enable, uint32_t quality_mode);
int gfx_read_perf_counter(struct gfx_driver *drv, int counter, uint64_t *value);
int gfx_reset_perf_counters(struct gfx_driver *drv);
void gfx_dump_status(struct gfx_driver *drv);
int gfx_self_test(struct gfx_driver *drv);

#endif
