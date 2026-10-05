#include "gpio.h"

static volatile uint32_t * const gpio_data     = (uint32_t *)(uintptr_t)(GPIO_BASE + GPIO_DATA);
static volatile uint32_t * const gpio_dir      = (uint32_t *)(uintptr_t)(GPIO_BASE + GPIO_DIR);
static volatile uint32_t * const gpio_pull     = (uint32_t *)(uintptr_t)(GPIO_BASE + GPIO_PULL);
static volatile uint32_t * const gpio_irq_en   = (uint32_t *)(uintptr_t)(GPIO_BASE + GPIO_IRQ_EN);
static volatile uint32_t * const gpio_irq_stat = (uint32_t *)(uintptr_t)(GPIO_BASE + GPIO_IRQ_STAT);

int gpio_init(void) {
    *gpio_dir  = 0;
    *gpio_pull = 0;
    *gpio_irq_en = 0;
    return 0;
}

int gpio_set_direction(int pin, int output) {
    if (pin < 0 || pin > 31) return -1;
    uint32_t dir = *gpio_dir;
    if (output)
        dir |= (1U << pin);
    else
        dir &= ~(1U << pin);
    *gpio_dir = dir;
    return 0;
}

int gpio_write(int pin, int val) {
    if (pin < 0 || pin > 31) return -1;
    uint32_t data = *gpio_data;
    if (val)
        data |= (1U << pin);
    else
        data &= ~(1U << pin);
    *gpio_data = data;
    return 0;
}

int gpio_read(int pin) {
    if (pin < 0 || pin > 31) return -1;
    return (*gpio_data >> pin) & 1;
}

int gpio_set_pull(int pin, int enable) {
    if (pin < 0 || pin > 31) return -1;
    uint32_t pull = *gpio_pull;
    if (enable)
        pull |= (1U << pin);
    else
        pull &= ~(1U << pin);
    *gpio_pull = pull;
    return 0;
}

int gpio_enable_irq(int pin) {
    if (pin < 0 || pin > 31) return -1;
    uint32_t en = *gpio_irq_en;
    en |= (1U << pin);
    *gpio_irq_en = en;
    return 0;
}

int gpio_disable_irq(int pin) {
    if (pin < 0 || pin > 31) return -1;
    uint32_t en = *gpio_irq_en;
    en &= ~(1U << pin);
    *gpio_irq_en = en;
    return 0;
}

int gpio_get_irq_status(void) {
    return (int)(*gpio_irq_stat);
}
