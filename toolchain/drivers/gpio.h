#ifndef BRAD_GPIO_H
#define BRAD_GPIO_H

#include <stdint.h>

#define GPIO_BASE     0x10040000UL
#define GPIO_DATA     0x0000
#define GPIO_DIR      0x0004
#define GPIO_PULL     0x0008
#define GPIO_IRQ_EN   0x000C
#define GPIO_IRQ_STAT 0x0010

int gpio_init(void);
int gpio_set_direction(int pin, int output);
int gpio_write(int pin, int val);
int gpio_read(int pin);
int gpio_set_pull(int pin, int enable);
int gpio_enable_irq(int pin);
int gpio_disable_irq(int pin);
int gpio_get_irq_status(void);

#endif
