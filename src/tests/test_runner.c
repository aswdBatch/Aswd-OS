#include "tests/test_runner.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/serial.h"
#include "gui/calc_gui.h"
#include "gui/work_gui.h"
#include "lib/string.h"

static int g_pass = 0;
static int g_fail = 0;

void test_assert(int condition, const char *name) {
    if (condition) {
        serial_write("[PASS] ");
        g_pass++;
    } else {
        serial_write("[FAIL] ");
        g_fail++;
    }
    serial_write(name);
    serial_write("\r\n");
}

static void print_int(int v) {
    char buf[16];
    if (v < 0) {
        serial_write("-");
        u32_to_dec((uint32_t)(-v), buf, sizeof(buf));
    } else {
        u32_to_dec((uint32_t)v, buf, sizeof(buf));
    }
    serial_write(buf);
}

void tests_run_all(void) {
    serial_write("\r\n[ASWD CI] Running tests...\r\n");

    tests_string();
    tests_shell();
    tests_fs();
    tests_ax();
    calc_run_tests();
    work_run_tests();

    serial_write("[ASWD CI] TEST COMPLETE: ");
    print_int(g_pass);
    serial_write(" pass, ");
    print_int(g_fail);
    serial_write(" fail\r\n");

    /* Exit QEMU via ISA debug-exit device at port 0xf4.
     * QEMU exit code = (value << 1) | 1:
     *   outb(0) => exit 1  (pass)
     *   outb(1) => exit 3  (fail) */
    outb(0xf4, (uint8_t)(g_fail > 0 ? 1 : 0));

    /* Should not reach here, but halt just in case */
    for (;;) { __asm__ volatile("hlt"); }
}
