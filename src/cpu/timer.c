#include "cpu/timer.h"

#include <stdint.h>

#include "cpu/pic.h"
#include "cpu/ports.h"
#include "drivers/vtconsole.h"

#define PIT_CHANNEL0 0x40u
#define PIT_COMMAND  0x43u
#define PIT_BASE_HZ  1193182u

static volatile uint32_t g_ticks = 0;
static uint32_t g_hz = 100;

void timer_init(uint32_t hz) {
    if (hz == 0) hz = 100;
    g_hz = hz;

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor > 0xFFFFu) divisor = 0xFFFFu;
    if (divisor == 0)      divisor = 1;

    /* Channel 0, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_COMMAND,  0x36u);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFFu));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFFu));

    pic_clear_mask(0);  /* unmask IRQ0 */
}

void timer_tick(void) {
    g_ticks++;
    if (g_ticks % 2 == 0) vtc_auto_flush();
    pic_send_eoi(0);
}

uint32_t timer_get_ticks(void) {
    return g_ticks;
}

uint32_t timer_uptime_secs(void) {
    return g_hz ? g_ticks / g_hz : 0;
}
