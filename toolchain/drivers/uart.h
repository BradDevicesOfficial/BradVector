#ifndef BRAD_UART_H
#define BRAD_UART_H

#include <stdint.h>

#define UART_BASE    0x10030000UL
#define UART_DATA    0x0000
#define UART_STATUS  0x0004
#define UART_CTRL    0x0008
#define UART_DIV     0x000C

#define UART_STATUS_TX_READY  (1U << 0)
#define UART_STATUS_RX_READY  (1U << 1)
#define UART_CTRL_ENABLE      (1U << 0)

int uart_init(uint32_t baud);
int uart_putc(char c);
int uart_getc(void);
int uart_puts(const char *s);
int uart_puthex(uint32_t val);
int uart_putdec(uint32_t val);
int uart_rx_ready(void);

#endif
