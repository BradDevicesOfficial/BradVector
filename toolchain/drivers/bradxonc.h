#ifndef BRAD_XONC_DRV_H
#define BRAD_XONC_DRV_H

#include <stdint.h>

#define XIC_BASE        0x10010000UL
#define XIC_CTRL        0x0000
#define XIC_STATUS      0x0004
#define XIC_CFG         0x0008
#define XIC_IRQ_ENABLE  0x0010
#define XIC_IRQ_DISABLE 0x0014
#define XIC_PRIO_THRESH 0x0020
#define XIC_SPI_BASE    0x0100
#define XIC_PPI_BASE    0x0200
#define XIC_SOFT_IRQ    0x0300
#define XIC_TIMER_IRQ   0x0400

#define XIC_CTRL_ENABLE (1U << 0)

#define IRQ_TIMER0   0
#define IRQ_TIMER1   1
#define IRQ_UART0    2
#define IRQ_UART1    3
#define IRQ_GPIO     4
#define IRQ_WDT      5
#define IRQ_DMA      6
#define IRQ_I2C      7
#define IRQ_SPI      8
#define IRQ_RTC      9
#define IRQ_PMU      10
#define IRQ_GPU      16
#define IRQ_NPU      17
#define IRQ_HSM      18
#define IRQ_HBM      19

#define PMU_BASE        0x100A0000UL
#define PMU_CTRL        0x0000
#define PMU_STATUS      0x0004
#define PMU_CSTATE      0x0008
#define PMU_PSTATE      0x0010
#define PMU_VOLTAGE     0x0014
#define PMU_FREQ        0x0018
#define PMU_TEMP        0x0020
#define PMU_POWER       0x0024
#define PMU_ENERGY      0x0028
#define PMU_PWR_CAP     0x0030
#define PMU_TEMP_LIMIT  0x0034
#define PMU_TEMP_HALT   0x0038
#define PMU_PWR_DOMAIN  0x0040
#define PMU_CLK_GATE    0x0044
#define PMU_FUSE        0x0050

enum cpu_cstate {
    CSTATE_C0 = 0,
    CSTATE_C1 = 1,
    CSTATE_C2 = 2,
    CSTATE_C3 = 3,
    CSTATE_C4 = 4,
};

struct pstate_entry {
    uint32_t freq_mhz;
    uint32_t voltage_mv;
    uint32_t power_mw;
};

#define CRU_BASE        0x100B0000UL
#define CRU_PLL_CTRL    0x0000
#define CRU_PLL_STATUS  0x0004
#define CRU_CLK_SEL     0x0010
#define CRU_CLK_DIV     0x0014
#define CRU_CLK_GATE    0x0018
#define CRU_RESET_CTRL  0x0020
#define CRU_RESET_STAT  0x0024
#define CRU_WDT_RESET   0x0030

enum clk_domain {
    CLK_CPU  = 0,
    CLK_FAB  = 1,
    CLK_MEM  = 2,
    CLK_GPU  = 3,
    CLK_NPU  = 4,
    CLK_IO   = 5,
    CLK_AON  = 6,
};

enum reset_domain {
    RST_POR   = 0,
    RST_WARM  = 1,
    RST_DEBUG = 2,
    RST_WDT   = 3,
};

int xic_init(void);
int xic_enable_irq(int irq_num, int priority);
int xic_disable_irq(int irq_num);
int xic_set_priority_threshold(int threshold);
uint32_t xic_get_pending(void);
void xic_send_soft_irq(int target_core, int irq_num);
int xic_ack_irq(int irq_num);

int pmu_init(void);
int pmu_set_cstate(enum cpu_cstate state);
int pmu_set_pstate(int domain, int pstate_idx);
int pmu_set_voltage(uint32_t mv);
int pmu_set_freq(uint32_t mhz);
int pmu_set_power_cap(uint32_t mw);
int pmu_set_temp_limit(uint32_t celsius);
int pmu_set_temp_halt(uint32_t celsius);
uint32_t pmu_read_temp(void);
uint64_t pmu_read_energy(void);
uint32_t pmu_read_power(void);
int pmu_gate_clock(int domain, int gate);
int pmu_power_domain(int domain, int on);
int pmu_get_status(uint32_t *cstate, uint32_t *voltage, uint32_t *freq);

int cru_init(void);
int cru_configure_pll(int pll_id, uint32_t n, uint32_t m, uint32_t d);
int cru_wait_pll_lock(int pll_id, uint64_t timeout_us);
int cru_set_clk_source(enum clk_domain domain, int pll_id);
int cru_set_clk_divider(enum clk_domain domain, uint32_t divider);
int cru_gate_clock(enum clk_domain domain, int gate);
int cru_assert_reset(enum reset_domain domain);
int cru_deassert_reset(enum reset_domain domain);
int cru_is_in_reset(enum reset_domain domain);

static inline void write32(volatile void *addr, uint32_t val) {
    __sync_synchronize();
    *(volatile uint32_t *)addr = val;
    __sync_synchronize();
}

static inline uint32_t read32(volatile void *addr) {
    uint32_t val = *(volatile uint32_t *)addr;
    __sync_synchronize();
    return val;
}

#define XIC_REG(off)    ((volatile void *)((uintptr_t)XIC_BASE + (off)))
#define PMU_REG(off)    ((volatile void *)((uintptr_t)PMU_BASE + (off)))
#define CRU_REG(off)    ((volatile void *)((uintptr_t)CRU_BASE + (off)))

#endif
