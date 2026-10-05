#include "uart.h"

static volatile uint8_t * const uart_data  = (uint8_t *)(uintptr_t)(UART_BASE + UART_DATA);
static volatile uint8_t * const uart_status = (uint8_t *)(uintptr_t)(UART_BASE + UART_STATUS);
static volatile uint8_t * const uart_ctrl  = (uint8_t *)(uintptr_t)(UART_BASE + UART_CTRL);
static volatile uint16_t * const uart_div  = (uint16_t *)(uintptr_t)(UART_BASE + UART_DIV);

#define SYS_CLK_HZ 50000000UL

int uart_init(uint32_t baud) {
    if (baud == 0) baud = 115200;
    uint32_t div = (SYS_CLK_HZ / (baud * 16UL)) - 1UL;
    if (div > 0xFFFF) div = 0xFFFF;
    *uart_div = (uint16_t)div;
    *uart_ctrl = UART_CTRL_ENABLE;
    return 0;
}

int uart_putc(char c) {
    uint32_t timeout = 1000000UL;
    while (!(*uart_status & UART_STATUS_TX_READY)) {
        if (--timeout == 0) return -1;
    }
    *uart_data = (uint8_t)c;
    if (c == '\n') {
        timeout = 1000000UL;
        while (!(*uart_status & UART_STATUS_TX_READY)) {
            if (--timeout == 0) return -1;
        }
        *uart_data = (uint8_t)'\r';
    }
    return 0;
}

int uart_getc(void) {
    uint32_t timeout = 1000000UL;
    while (!(*uart_status & UART_STATUS_RX_READY)) {
        if (--timeout == 0) return -1;
    }
    return *uart_data;
}

int uart_puts(const char *s) {
    while (*s) {
        if (uart_putc(*s++) != 0) return -1;
    }
    return 0;
}

int uart_puthex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;
    buf[0] = '0'; buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[9 - i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
    return uart_puts(buf);
}

int uart_putdec(uint32_t val) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (val == 0) {
        buf[10] = '0';
        return uart_puts(&buf[10]);
    }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    return uart_puts(&buf[i + 1]);
}

int uart_rx_ready(void) {
    return (*uart_status & UART_STATUS_RX_READY) ? 1 : 0;
}
