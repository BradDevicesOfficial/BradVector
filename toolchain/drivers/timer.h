#ifndef BRAD_TIMER_H
#define BRAD_TIMER_H

#include <stdint.h>

#define TIMER_BASE    0x10020000UL
#define TIMER_FREQ    0x0000
#define TIMER_CNT_LO  0x0004
#define TIMER_CNT_HI  0x0008
#define TIMER_CMP0    0x0010
#define TIMER_CMP1    0x0014
#define TIMER_CMP2    0x0018
#define TIMER_CMP3    0x001C
#define TIMER_CTRL    0x0020

int timer_init(void);
uint64_t timer_get_cycles(void);
uint64_t timer_get_us(void);
int timer_set_compare(int cmp_id, uint64_t value);
int timer_enable_irq(int cmp_id);
int timer_disable_irq(int cmp_id);
void timer_delay_us(uint64_t us);

#endif
