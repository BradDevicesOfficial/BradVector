#include "wdt.h"

static volatile uint8_t  * const wdt_ctrl    = (uint8_t  *)(uintptr_t)(WDT_BASE + WDT_CTRL);
static volatile uint32_t * const wdt_timeout = (uint32_t *)(uintptr_t)(WDT_BASE + WDT_TIMEOUT);
static volatile uint8_t  * const wdt_restart = (uint8_t  *)(uintptr_t)(WDT_BASE + WDT_RESTART);
static volatile uint8_t  * const wdt_status  = (uint8_t  *)(uintptr_t)(WDT_BASE + WDT_STATUS);

int wdt_init(uint32_t timeout_cycles, int enable_reset) {
    *wdt_timeout = timeout_cycles;
    uint8_t ctrl = WDT_CTRL_ENABLE;
    if (enable_reset) ctrl |= WDT_CTRL_RESET_EN;
    ctrl |= WDT_CTRL_IRQ_EN;
    *wdt_ctrl = ctrl;
    *wdt_restart = 0x5A;
    return 0;
}

int wdt_pet(void) {
    *wdt_restart = 0x5A;
    uint8_t status = *wdt_status;
    if (status & WDT_STATUS_EXPIRED) {
        return -1;
    }
    return 0;
}

int wdt_disable(void) {
    *wdt_ctrl = 0;
    return 0;
}

int wdt_has_expired(void) {
    return (*wdt_status & WDT_STATUS_EXPIRED) ? 1 : 0;
}
