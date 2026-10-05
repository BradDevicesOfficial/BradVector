#include "timer.h"

static volatile uint32_t * const timer_freq   = (uint32_t *)(uintptr_t)(TIMER_BASE + TIMER_FREQ);
static volatile uint32_t * const timer_cnt_lo = (uint32_t *)(uintptr_t)(TIMER_BASE + TIMER_CNT_LO);
static volatile uint32_t * const timer_cnt_hi = (uint32_t *)(uintptr_t)(TIMER_BASE + TIMER_CNT_HI);
static volatile uint32_t * const timer_cmp    = (uint32_t *)(uintptr_t)(TIMER_BASE + TIMER_CMP0);
static volatile uint32_t * const timer_ctrl   = (uint32_t *)(uintptr_t)(TIMER_BASE + TIMER_CTRL);

static uint32_t tmr_freq_hz;

int timer_init(void) {
    tmr_freq_hz = *timer_freq;
    if (tmr_freq_hz == 0) tmr_freq_hz = 50000000;
    *timer_ctrl = 1;
    return 0;
}

uint64_t timer_get_cycles(void) {
    uint32_t lo, hi;
    lo = *timer_cnt_lo;
    hi = *timer_cnt_hi;
    if (lo > *timer_cnt_lo) {
        hi = *timer_cnt_hi;
        lo = *timer_cnt_lo;
    }
    return ((uint64_t)hi << 32) | lo;
}

uint64_t timer_get_us(void) {
    return timer_get_cycles() / (tmr_freq_hz / 1000000UL);
}

int timer_set_compare(int cmp_id, uint64_t value) {
    if (cmp_id < 0 || cmp_id > 3) return -1;
    timer_cmp[cmp_id] = (uint32_t)value;
    return 0;
}

int timer_enable_irq(int cmp_id) {
    if (cmp_id < 0 || cmp_id > 3) return -1;
    uint32_t ctrl = *timer_ctrl;
    ctrl |= (1U << (4 + cmp_id));
    *timer_ctrl = ctrl;
    return 0;
}

int timer_disable_irq(int cmp_id) {
    if (cmp_id < 0 || cmp_id > 3) return -1;
    uint32_t ctrl = *timer_ctrl;
    ctrl &= ~(1U << (4 + cmp_id));
    *timer_ctrl = ctrl;
    return 0;
}

void timer_delay_us(uint64_t us) {
    uint64_t start = timer_get_cycles();
    uint64_t target = start + us * (tmr_freq_hz / 1000000UL);
    while (timer_get_cycles() < target);
}
