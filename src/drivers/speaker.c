#include "drivers/speaker.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "cpu/timer.h"

/* PIT channel 2 base frequency */
#define PIT_FREQ 1193182u

static void speaker_on(uint32_t freq_hz) {
    uint32_t divisor;
    uint8_t  tmp;

    if (freq_hz == 0) freq_hz = 1;
    divisor = PIT_FREQ / freq_hz;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    if (divisor < 1)      divisor = 1;

    /* Configure PIT channel 2, mode 3 (square wave) */
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    /* Enable speaker via port 0x61 bits 0+1 */
    tmp = inb(0x61);
    outb(0x61, tmp | 0x03);
}

static void speaker_off(void) {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & ~0x03);
}

static void busy_wait_ms(uint32_t ms) {
    uint32_t start = timer_get_ticks();
    uint32_t ticks = ms / 10u;   /* 100 Hz timer → 10 ms per tick */
    if (ticks == 0) ticks = 1;
    while (timer_get_ticks() - start < ticks) {
        __asm__ volatile("hlt");
    }
}

void speaker_beep(uint32_t freq_hz, uint32_t ms) {
    speaker_on(freq_hz);
    busy_wait_ms(ms);
    speaker_off();
}

void speaker_boot_chime(void) {
    speaker_beep(880, 80);
    busy_wait_ms(20);
    speaker_beep(1100, 60);
}
