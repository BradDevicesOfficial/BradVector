#include "bradxonc.h"

#define TIMEOUT_LOOPS 1000000UL
#define PMU_TRANSITION_DELAY 8

int xic_init(void) {
    uint32_t cfg;

    cfg = read32(XIC_REG(XIC_CTRL));
    cfg |= XIC_CTRL_ENABLE;
    write32(XIC_REG(XIC_CTRL), cfg);

    write32(XIC_REG(XIC_PRIO_THRESH), 0);
    write32(XIC_REG(XIC_CFG), 0x1F);

    cfg = read32(XIC_REG(XIC_CTRL));
    return (cfg & XIC_CTRL_ENABLE) ? 0 : -1;
}

int xic_enable_irq(int irq_num, int priority) {
    if (irq_num < 0 || irq_num > 31)
        return -1;
    write32(XIC_REG(XIC_SPI_BASE + irq_num * 4), (priority & 0x7) | (1U << 3));
    write32(XIC_REG(XIC_IRQ_ENABLE), (1U << irq_num));
    return 0;
}

int xic_disable_irq(int irq_num) {
    if (irq_num < 0 || irq_num > 31)
        return -1;
    write32(XIC_REG(XIC_IRQ_DISABLE), (1U << irq_num));
    return 0;
}

int xic_set_priority_threshold(int threshold) {
    write32(XIC_REG(XIC_PRIO_THRESH), threshold & 0x7);
    return 0;
}

uint32_t xic_get_pending(void) {
    return read32(XIC_REG(XIC_STATUS));
}

void xic_send_soft_irq(int target_core, int irq_num) {
    write32(XIC_REG(XIC_SOFT_IRQ), (target_core & 0xF) | ((irq_num & 0x1F) << 8));
}

int xic_ack_irq(int irq_num) {
    if (irq_num < 0 || irq_num > 31)
        return -1;
    write32(XIC_REG(XIC_SPI_BASE + irq_num * 4), (1U << 4));
    return 0;
}

int pmu_init(void) {
    write32(PMU_REG(PMU_CSTATE), CSTATE_C0);
    write32(PMU_REG(PMU_PWR_CAP), 250000);
    return 0;
}

int pmu_set_cstate(enum cpu_cstate state) {
    uint32_t timeout = TIMEOUT_LOOPS;
    write32(PMU_REG(PMU_CSTATE), (uint32_t)state);
    while (timeout--) {
        uint32_t status = read32(PMU_REG(PMU_STATUS));
        uint32_t cstate = status & 0x7;
        if (cstate == (uint32_t)state)
            return 0;
    }
    return -1;
}

int pmu_set_pstate(int domain, int pstate_idx) {
    write32(PMU_REG(PMU_PSTATE), (domain & 0xF) | ((pstate_idx & 0x7) << 4));
    return 0;
}

int pmu_set_voltage(uint32_t mv) {
    write32(PMU_REG(PMU_VOLTAGE), mv);
    uint32_t readback = read32(PMU_REG(PMU_VOLTAGE));
    if (readback < mv - mv / 20 || readback > mv + mv / 20)
        return -1;
    return 0;
}

int pmu_set_freq(uint32_t mhz) {
    write32(PMU_REG(PMU_FREQ), mhz);
    uint32_t readback = read32(PMU_REG(PMU_FREQ));
    if (readback < mhz - mhz / 20 || readback > mhz + mhz / 20)
        return -1;
    return 0;
}

int pmu_set_power_cap(uint32_t mw) {
    write32(PMU_REG(PMU_PWR_CAP), mw);
    uint32_t readback = read32(PMU_REG(PMU_PWR_CAP));
    return (readback == mw) ? 0 : -1;
}

int pmu_set_temp_limit(uint32_t celsius) {
    write32(PMU_REG(PMU_TEMP_LIMIT), celsius);
    return 0;
}

int pmu_set_temp_halt(uint32_t celsius) {
    write32(PMU_REG(PMU_TEMP_HALT), celsius);
    return 0;
}

uint32_t pmu_read_temp(void) {
    return read32(PMU_REG(PMU_TEMP));
}

uint64_t pmu_read_energy(void) {
    uint32_t lo, hi;
    lo = read32(PMU_REG(PMU_ENERGY));
    hi = read32(PMU_REG(PMU_ENERGY + 4));
    return ((uint64_t)hi << 32) | lo;
}

uint32_t pmu_read_power(void) {
    return read32(PMU_REG(PMU_POWER));
}

int pmu_gate_clock(int domain, int gate) {
    uint32_t mask = read32(PMU_REG(PMU_CLK_GATE));
    if (gate)
        mask |= (1U << domain);
    else
        mask &= ~(1U << domain);
    write32(PMU_REG(PMU_CLK_GATE), mask);
    return 0;
}

int pmu_power_domain(int domain, int on) {
    uint32_t mask = read32(PMU_REG(PMU_PWR_DOMAIN));
    if (on)
        mask |= (1U << domain);
    else
        mask &= ~(1U << domain);
    write32(PMU_REG(PMU_PWR_DOMAIN), mask);
    return 0;
}

int pmu_get_status(uint32_t *cstate, uint32_t *voltage, uint32_t *freq) {
    uint32_t status = read32(PMU_REG(PMU_STATUS));
    if (cstate) *cstate = status & 0x7;
    if (voltage) *voltage = read32(PMU_REG(PMU_VOLTAGE));
    if (freq) *freq = read32(PMU_REG(PMU_FREQ));
    return 0;
}

int cru_init(void) {
    for (int pll = 0; pll < 7; pll++) {
        if (cru_wait_pll_lock(pll, 100000) != 0)
            return -1;
    }
    return 0;
}

int cru_configure_pll(int pll_id, uint32_t n, uint32_t m, uint32_t d) {
    uint32_t cfg = (n & 0x3FF) | ((m & 0xFF) << 10) | ((d & 0x3F) << 18) | (1U << 24);
    write32(CRU_REG(CRU_PLL_CTRL + pll_id * 8), cfg);
    return 0;
}

int cru_wait_pll_lock(int pll_id, uint64_t timeout_us) {
    uint32_t timeout = (uint32_t)(timeout_us > 0xFFFFFFFFUL ? 0xFFFFFFFFUL : timeout_us);
    while (timeout--) {
        uint32_t status = read32(CRU_REG(CRU_PLL_STATUS));
        if (status & (1U << pll_id))
            return 0;
    }
    return -1;
}

int cru_set_clk_source(enum clk_domain domain, int pll_id) {
    uint32_t sel = read32(CRU_REG(CRU_CLK_SEL));
    sel &= ~(0x7 << (domain * 3));
    sel |= (pll_id & 0x7) << (domain * 3);
    write32(CRU_REG(CRU_CLK_SEL), sel);
    return 0;
}

int cru_set_clk_divider(enum clk_domain domain, uint32_t divider) {
    if (divider < 1) divider = 1;
    uint32_t div_reg = read32(CRU_REG(CRU_CLK_DIV));
    div_reg &= ~(0xFF << (domain * 4));
    div_reg |= (divider & 0xFF) << (domain * 4);
    write32(CRU_REG(CRU_CLK_DIV), div_reg);
    return 0;
}

int cru_gate_clock(enum clk_domain domain, int gate) {
    uint32_t mask = read32(CRU_REG(CRU_CLK_GATE));
    if (gate)
        mask |= (1U << domain);
    else
        mask &= ~(1U << domain);
    write32(CRU_REG(CRU_CLK_GATE), mask);
    return 0;
}

int cru_assert_reset(enum reset_domain domain) {
    uint32_t rst = read32(CRU_REG(CRU_RESET_CTRL));
    rst |= (1U << domain);
    write32(CRU_REG(CRU_RESET_CTRL), rst);
    return 0;
}

int cru_deassert_reset(enum reset_domain domain) {
    uint32_t rst = read32(CRU_REG(CRU_RESET_CTRL));
    rst &= ~(1U << domain);
    write32(CRU_REG(CRU_RESET_CTRL), rst);
    return 0;
}

int cru_is_in_reset(enum reset_domain domain) {
    uint32_t stat = read32(CRU_REG(CRU_RESET_STAT));
    return (stat >> domain) & 1;
}
