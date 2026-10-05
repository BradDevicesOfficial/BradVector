#ifndef BRAD_WDT_H
#define BRAD_WDT_H

#include <stdint.h>

#define WDT_BASE      0x10050000UL
#define WDT_CTRL      0x0000
#define WDT_TIMEOUT   0x0004
#define WDT_RESTART   0x0008
#define WDT_STATUS    0x000C

#define WDT_CTRL_ENABLE      (1U << 0)
#define WDT_CTRL_RESET_EN    (1U << 1)
#define WDT_CTRL_IRQ_EN      (1U << 2)
#define WDT_STATUS_EXPIRED   (1U << 0)

int wdt_init(uint32_t timeout_cycles, int enable_reset);
int wdt_pet(void);
int wdt_disable(void);
int wdt_has_expired(void);

#endif
