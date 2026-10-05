#include "bradgfx.h"
#include "bradxonc.h"
#include "uart.h"
#include "gpio.h"
#include "wdt.h"
#include "timer.h"

#define TEST_PASS 0
#define TEST_FAIL 1

uint32_t __brad_heap_start __attribute__((used));

static volatile uint8_t gfx_mmio_region[0x5000] __attribute__((aligned(4096)));

static int test_xic(void) {
    xic_init();
    xic_enable_irq(IRQ_TIMER0, 0);
    xic_enable_irq(IRQ_UART0, 0);
    xic_set_priority_threshold(7);
    xic_send_soft_irq(0, IRQ_TIMER0);
    xic_ack_irq(IRQ_TIMER0);
    xic_disable_irq(IRQ_TIMER0);
    return TEST_PASS;
}

static int test_pmu(void) {
    pmu_init();
    pmu_set_cstate(CSTATE_C1);
    pmu_set_voltage(800);
    pmu_set_freq(1800);
    uint32_t temp = pmu_read_temp();
    uint32_t power = pmu_read_power();
    if (temp > 150) return TEST_FAIL;
    if (power == 0) return TEST_FAIL;
    pmu_set_pstate(0, 2);
    return TEST_PASS;
}

static int test_cru(void) {
    cru_configure_pll(0, 20, 1000, 1);
    cru_wait_pll_lock(0, 1000);
    cru_gate_clock(CLK_IO, 1);
    cru_gate_clock(CLK_GPU, 1);
    cru_assert_reset(RST_WARM);
    cru_deassert_reset(RST_WARM);
    cru_set_clk_source(CLK_CPU, 0);
    cru_set_clk_divider(CLK_CPU, 1);
    return TEST_PASS;
}

static int test_uart(void) {
    uart_init(115200);
    uart_puts("UART: Hello from BradXon\r\n");
    uart_puthex(0xDEADBEEF);
    uart_puts("\r\n");
    uart_putdec(123456789);
    uart_puts("\r\n");
    return TEST_PASS;
}

static int test_gpio(void) {
    gpio_init();
    gpio_set_direction(0, 1);
    gpio_write(0, 1);
    gpio_set_direction(1, 0);
    int val = gpio_read(1);
    (void)val;
    gpio_set_pull(1, 1);
    return TEST_PASS;
}

static int test_wdt(void) {
    wdt_init(1000000, 1);
    wdt_pet();
    wdt_pet();
    wdt_disable();
    return TEST_PASS;
}

static int test_timer(void) {
    timer_init();
    uint64_t start = timer_get_us();
    timer_delay_us(100);
    uint64_t end = timer_get_us();
    if (end < start) return TEST_FAIL;
    return TEST_PASS;
}

static int test_gfx(void) {
    struct gfx_driver drv;
    int ret;

    ret = gfx_driver_init(&drv, gfx_mmio_region);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_set_power_state(&drv, GFX_PSTATE_TURBO);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_set_freq(&drv, 2200);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_set_voltage(&drv, 950);
    if (ret != GFX_OK) return TEST_FAIL;

    struct gfx_buffer *vbuf = gfx_alloc_buffer(&drv, 1048576, GFX_MEM_FRAME);
    if (!vbuf) return TEST_FAIL;

    int test_shader_code[] = {0x00000053, 0x00000001, 0x00000000, 0x00000020};
    struct gfx_shader *sh = gfx_create_shader(&drv, test_shader_code, sizeof(test_shader_code));
    if (!sh) return TEST_FAIL;

    ret = gfx_bind_shader(&drv, sh);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_submit_draw(&drv, 1024, 1, sh, vbuf);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_submit_dispatch(&drv, 32, 32, 1, sh);
    if (ret != GFX_OK) return TEST_FAIL;

    ret = gfx_submit_barrier(&drv, 0x1);
    if (ret != GFX_OK) return TEST_FAIL;

    gfx_kick(&drv);
    gfx_irq_handler(&drv);

    ret = gfx_set_bradsense(&drv, 1, 1);
    if (ret != GFX_OK) return TEST_FAIL;

    gfx_reset_perf_counters(&drv);
    uint64_t val;
    gfx_read_perf_counter(&drv, 0, &val);

    gfx_dump_status(&drv);

    ret = gfx_self_test(&drv);
    return (ret == GFX_OK) ? TEST_PASS : TEST_FAIL;
}

int main(void) {
    int passed = 0, failed = 0;

#define RUN_TEST(name, func) do { \
    uart_puts("TEST " name ": "); \
    if (func() == TEST_PASS) { uart_puts("PASS\r\n"); passed++; } \
    else { uart_puts("FAIL\r\n"); failed++; } \
} while (0)

    uart_puts("BradXon Platform Test Suite\r\n");
    uart_puts("===========================\r\n");

    RUN_TEST("XIC",  test_xic);
    RUN_TEST("PMU",  test_pmu);
    RUN_TEST("CRU",  test_cru);
    RUN_TEST("UART", test_uart);
    RUN_TEST("GPIO", test_gpio);
    RUN_TEST("WDT",  test_wdt);
    RUN_TEST("TIMER", test_timer);
    RUN_TEST("GFX",  test_gfx);

    uart_puts("===========================\r\n");
    uart_puts("Results: ");
    uart_putdec(passed);
    uart_puts(" passed, ");
    uart_putdec(failed);
    uart_puts(" failed\r\n");

    return failed ? TEST_FAIL : TEST_PASS;
}
