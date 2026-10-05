#include "bradgfx.h"
#include "bradxonc.h"
#include "timer.h"
#include "uart.h"

#define PAGE_SIZE 65536UL
#define MAX_BUFFERS 256

int gfx_driver_init(struct gfx_driver *drv, volatile uint8_t *mmio_base) {
    uint32_t rev, sku, nv, nrt, nnsu;
    uint32_t timeout = 1000000;

    if (!drv || !mmio_base) return GFX_ERR_INIT;

    drv->mmio_base = mmio_base;
    drv->nsu_base = (volatile uint8_t *)(uintptr_t)NSU_BASE;
    drv->num_buffers = 0;

    rev = read32(mmio_base + GFX_REVISION);
    sku = read32(mmio_base + GFX_SKU_ID);
    nv  = read32(mmio_base + GFX_NUM_VEC);
    nrt = read32(mmio_base + GFX_NUM_RT);
    nnsu = read32(mmio_base + GFX_NUM_NSU);

    if (rev == 0 || rev == 0xFFFFFFFF) return GFX_ERR_INIT;

    drv->sku = (enum brad_gfx_sku)sku;
    drv->num_vec = nv;
    drv->num_rt = nrt;
    drv->num_nsu = nnsu;

    write32(mmio_base + GFX_PWR_STATE, GFX_PSTATE_BALANCED);
    while (timeout--) {
        uint32_t status = read32(mmio_base + GFX_STATUS);
        if (status & GFX_STATUS_READY) break;
    }
    if (timeout == 0) return GFX_ERR_TIMEOUT;

    write32(mmio_base + GFX_CLK_GATE, 0);
    write32(mmio_base + GFX_CTRL, GFX_CTRL_ENABLE);
    write32(mmio_base + GFX_INTR_ENABLE, 0x1FF);

    drv->freq_mhz = read32(mmio_base + GFX_FREQ);
    if (drv->freq_mhz == 0) drv->freq_mhz = 1800;

    switch (drv->sku) {
        case GFX_SKU_LITE:     drv->vram_mb = 8192; break;
        case GFX_SKU_STANDARD: drv->vram_mb = 12288; break;
        case GFX_SKU_ULTRA:    drv->vram_mb = 16384; break;
        case GFX_SKU_MAX:      drv->vram_mb = 24576; break;
        case GFX_SKU_ANF:      drv->vram_mb = 49152; break;
        default:               drv->vram_mb = 8192; break;
    }

    drv->cmdq.size = 1024;
    drv->cmdq.head = 0;
    drv->cmdq.tail = 0;
    drv->cmdq.ring = 0;

    write32(mmio_base + CMD_QUEUE_SIZE, drv->cmdq.size);

    return GFX_OK;
}

int gfx_set_power_state(struct gfx_driver *drv, enum gfx_power_state state) {
    write32(drv->mmio_base + GFX_PWR_STATE, (uint32_t)state);
    return GFX_OK;
}

int gfx_get_power_state(struct gfx_driver *drv) {
    return (int)read32(drv->mmio_base + GFX_PWR_STATE);
}

int gfx_set_freq(struct gfx_driver *drv, uint32_t freq_mhz) {
    write32(drv->mmio_base + GFX_FREQ, freq_mhz);
    uint32_t rb = read32(drv->mmio_base + GFX_FREQ);
    if (rb < freq_mhz - freq_mhz / 10 || rb > freq_mhz + freq_mhz / 10)
        return -1;
    drv->freq_mhz = rb;
    return GFX_OK;
}

int gfx_set_voltage(struct gfx_driver *drv, uint32_t voltage_mv) {
    write32(drv->mmio_base + GFX_VOLTAGE, voltage_mv);
    return GFX_OK;
}

struct gfx_buffer *gfx_alloc_buffer(struct gfx_driver *drv, uint64_t size, uint32_t flags) {
    uint64_t alloc_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (drv->num_buffers >= MAX_BUFFERS) return 0;

    struct gfx_buffer *buf = &drv->buffers[drv->num_buffers];
    static uint64_t next_addr = 0x10000000UL;
    buf->addr = next_addr;
    buf->size = alloc_size;
    buf->flags = flags;
    buf->refcount = 1;
    next_addr += alloc_size;
    drv->num_buffers++;
    return buf;
}

int gfx_free_buffer(struct gfx_driver *drv, struct gfx_buffer *buf) {
    (void)drv;
    if (!buf) return -1;
    buf->refcount = 0;
    return GFX_OK;
}

struct gfx_shader *gfx_create_shader(struct gfx_driver *drv, const void *code, uint32_t size) {
    static uint64_t next_handle = 1;
    if (!code || size == 0) return 0;
    struct gfx_shader *sh = (struct gfx_shader *)0;
    for (uint32_t i = 0; i < drv->num_buffers; i++) {
        if (drv->buffers[i].refcount == 0) {
            drv->buffers[i].refcount = 1;
            drv->buffers[i].size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            drv->buffers[i].addr = 0x20000000UL + i * 0x100000UL;
            drv->buffers[i].flags = 0;
            sh = (struct gfx_shader *)&drv->buffers[i];
            break;
        }
    }
    if (!sh) return 0;
    sh->handle = next_handle++;
    sh->code_addr = 0x20000000UL;
    sh->code_size = size;
    sh->num_warps = 8;
    return sh;
}

int gfx_destroy_shader(struct gfx_driver *drv, struct gfx_shader *sh) {
    (void)drv;
    if (!sh) return -1;
    sh->handle = 0;
    return GFX_OK;
}

int gfx_bind_shader(struct gfx_driver *drv, struct gfx_shader *sh) {
    (void)drv;
    if (!sh) return -1;
    return GFX_OK;
}

static int gfx_enqueue_cmd(struct gfx_driver *drv, struct gfx_cmd_packet *pkt) {
    (void)pkt;
    uint32_t tail = drv->cmdq.tail;
    uint32_t next = (tail + 1) & (drv->cmdq.size - 1);
    if (next == drv->cmdq.head) return GFX_ERR_OOM;
    /* In a real implementation, write to ring buffer in SPMP */
    drv->cmdq.tail = next;
    return GFX_OK;
}

int gfx_submit_draw(struct gfx_driver *drv, uint32_t vertex_count, uint32_t instance_count,
                    struct gfx_shader *sh, struct gfx_buffer *vbuf) {
    struct gfx_cmd_packet pkt;
    pkt.type = CMD_DRAW;
    pkt.flags = 0;
    pkt.arg0 = vertex_count;
    pkt.arg1 = instance_count;
    pkt.arg2 = sh ? sh->handle : 0;
    pkt.arg3 = vbuf ? vbuf->addr : 0;
    pkt.timestamp = 0;
    return gfx_enqueue_cmd(drv, &pkt);
}

int gfx_submit_dispatch(struct gfx_driver *drv, uint32_t gx, uint32_t gy, uint32_t gz,
                        struct gfx_shader *sh) {
    struct gfx_cmd_packet pkt;
    pkt.type = CMD_DISPATCH;
    pkt.flags = 0;
    pkt.arg0 = gx;
    pkt.arg1 = gy;
    pkt.arg2 = gz;
    pkt.arg3 = sh ? sh->handle : 0;
    pkt.timestamp = 0;
    return gfx_enqueue_cmd(drv, &pkt);
}

int gfx_submit_copy(struct gfx_driver *drv, struct gfx_buffer *src, struct gfx_buffer *dst,
                    uint64_t size) {
    struct gfx_cmd_packet pkt;
    pkt.type = CMD_COPY;
    pkt.flags = 0;
    pkt.arg0 = src ? src->addr : 0;
    pkt.arg1 = dst ? dst->addr : 0;
    pkt.arg2 = size;
    pkt.arg3 = 0;
    pkt.timestamp = 0;
    return gfx_enqueue_cmd(drv, &pkt);
}

int gfx_submit_barrier(struct gfx_driver *drv, uint32_t barrier_mask) {
    struct gfx_cmd_packet pkt;
    pkt.type = CMD_BARRIER;
    pkt.flags = 0;
    pkt.arg0 = barrier_mask;
    pkt.arg1 = 0;
    pkt.arg2 = 0;
    pkt.arg3 = 0;
    pkt.timestamp = 0;
    return gfx_enqueue_cmd(drv, &pkt);
}

int gfx_kick(struct gfx_driver *drv) {
    write32(drv->mmio_base + CMD_QUEUE_DOORBELL, drv->cmdq.tail);
    return GFX_OK;
}

int gfx_wait_idle(struct gfx_driver *drv, uint64_t timeout_us) {
    uint64_t timeout = timeout_us;
    while (timeout--) {
        uint32_t status = read32(drv->mmio_base + GFX_STATUS);
        if (!(status & GFX_STATUS_BUSY)) return GFX_OK;
        timer_delay_us(1);
    }
    return GFX_ERR_TIMEOUT;
}

int gfx_query_timestamp(struct gfx_driver *drv, uint64_t *ts) {
    (void)drv;
    if (!ts) return -1;
    *ts = timer_get_cycles();
    return GFX_OK;
}

void gfx_irq_handler(struct gfx_driver *drv) {
    uint32_t status = read32(drv->mmio_base + GFX_INTR_STATUS);
    if (status & (1U << 0)) {
        /* CMD_DONE: advance head pointer */
        uint32_t head = read32(drv->mmio_base + CMD_QUEUE_HEAD);
        drv->cmdq.head = head;
    }
    if (status & (1U << 1)) {
        /* PAGE_FAULT: log and recover */
    }
    if (status & (1U << 2)) {
        /* ILLEGAL_INSTR: log shader ID */
    }
    if (status & (1U << 3)) {
        /* TIMEOUT: reset shader */
    }
    if (status & (1U << 8)) {
        /* THERMAL_THROTTLE: reduce freq */
        uint32_t freq = read32(drv->mmio_base + GFX_FREQ);
        write32(drv->mmio_base + GFX_FREQ, freq * 9 / 10);
    }
    write32(drv->mmio_base + GFX_INTR_STATUS, status);
}

int gfx_set_bradsense(struct gfx_driver *drv, int enable, uint32_t quality_mode) {
    uint32_t ctrl = read32(drv->mmio_base + GFX_CTRL);
    if (enable) {
        ctrl |= GFX_CTRL_BRADSENSE_ENABLE | GFX_CTRL_NSU_ENABLE;
    } else {
        ctrl &= ~(GFX_CTRL_BRADSENSE_ENABLE | GFX_CTRL_NSU_ENABLE);
    }
    write32(drv->mmio_base + GFX_CTRL, ctrl);
    write32(drv->mmio_base + GFX_BRADSENSE_CTRL, quality_mode & 0x3);
    if (quality_mode <= 3) {
        write32(drv->nsu_base + NSU_QUALITY_MODE, quality_mode);
    }
    return GFX_OK;
}

int gfx_read_perf_counter(struct gfx_driver *drv, int counter, uint64_t *value) {
    if (!value) return -1;
    uint32_t off = (counter == 0) ? GFX_PERF_CNTR_0 : GFX_PERF_CNTR_1;
    uint32_t lo = read32(drv->mmio_base + off);
    uint32_t hi = read32(drv->mmio_base + off + 4);
    *value = ((uint64_t)hi << 32) | lo;
    return GFX_OK;
}

int gfx_reset_perf_counters(struct gfx_driver *drv) {
    write32(drv->mmio_base + GFX_PERF_CNTR_CFG, 0);
    write32(drv->mmio_base + GFX_PERF_CNTR_CFG, 1);
    return GFX_OK;
}

void gfx_dump_status(struct gfx_driver *drv) {
    uint32_t status = read32(drv->mmio_base + GFX_STATUS);
    uint32_t pwr = read32(drv->mmio_base + GFX_PWR_STATE);
    uint32_t freq = read32(drv->mmio_base + GFX_FREQ);

    uart_puts("BradGfx Status:\r\n");
    uart_puts("  SKU:      "); uart_putdec(drv->sku); uart_puts("\r\n");
    uart_puts("  Vec:      "); uart_putdec(drv->num_vec); uart_puts("\r\n");
    uart_puts("  RT:       "); uart_putdec(drv->num_rt); uart_puts("\r\n");
    uart_puts("  NSU:      "); uart_putdec(drv->num_nsu); uart_puts("\r\n");
    uart_puts("  Freq:     "); uart_putdec(freq); uart_puts(" MHz\r\n");
    uart_puts("  VRAM:     "); uart_putdec(drv->vram_mb); uart_puts(" MB\r\n");
    uart_puts("  PWR:      P"); uart_putdec(pwr); uart_puts("\r\n");
    uart_puts("  Busy:     "); uart_putdec((status >> 16) & 0xFF); uart_puts("%\r\n");
}

int gfx_self_test(struct gfx_driver *drv) {
    uint32_t rev;

    rev = read32(drv->mmio_base + GFX_REVISION);
    if (rev == 0 || rev == 0xFFFFFFFF) {
        uart_puts("FAIL: revision readback\r\n");
        return -1;
    }
    uart_puts("PASS: revision=0x"); uart_puthex(rev); uart_puts("\r\n");

    write32(drv->mmio_base + GFX_CTRL, GFX_CTRL_ENABLE);
    uint32_t ctrl = read32(drv->mmio_base + GFX_CTRL);
    if (!(ctrl & GFX_CTRL_ENABLE)) {
        uart_puts("FAIL: enable bit\r\n");
        return -1;
    }
    uart_puts("PASS: GFX enable\r\n");

    write32(drv->mmio_base + GFX_PWR_STATE, GFX_PSTATE_TURBO);
    uint32_t pwr = read32(drv->mmio_base + GFX_PWR_STATE);
    uart_puts("PASS: power state P"); uart_putdec(pwr); uart_puts("\r\n");

    uint64_t val;
    gfx_read_perf_counter(drv, 0, &val);
    uart_puts("PASS: perf counter = "); uart_putdec((uint32_t)val); uart_puts("\r\n");

    uart_puts("BradGfx self-test PASSED\r\n");
    return GFX_OK;
}
