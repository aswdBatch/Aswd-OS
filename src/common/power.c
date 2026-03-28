#include "common/power.h"

#include <stdint.h>

#include "common/colors.h"
#include "cpu/ports.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/vga.h"
#include "lib/string.h"

static void power_spin_wait(void) {
    for (volatile uint32_t i = 0; i < 200000000u; i++) {}
}

static void power_text_center(int row, const char *text, uint8_t color) {
    int len;
    int col;

    if (!text || !text[0]) return;

    len = (int)str_len(text);
    col = (80 - len) / 2;
    if (col < 0) col = 0;

    for (int i = 0; text[i] && col + i < 80; i++) {
        vga_put_char_at(text[i], color, row, col + i);
    }
}

static void power_show_text_status(const char *title, const char *detail) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
    power_text_center(10, title, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    if (detail && detail[0]) {
        power_text_center(12, detail, vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

static void power_show_graphics_status(const char *title, const char *detail) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int panel_w;
    int panel_h;
    int panel_x;
    int panel_y;

    if (sw < 1 || sh < 1) return;

    panel_w = sw * 2 / 3;
    panel_h = sh / 5;
    if (panel_w < 320) panel_w = 320;
    if (panel_h < 96) panel_h = 96;
    if (panel_w > sw - 32) panel_w = sw - 32;
    if (panel_h > sh - 32) panel_h = sh - 32;
    panel_x = (sw - panel_w) / 2;
    panel_y = (sh - panel_h) / 2;

    gfx_fill_rect_gradient_v(0, 0, sw, sh, gfx_rgb(22, 40, 68), gfx_rgb(8, 14, 26));
    gfx_fill_rect_alpha(panel_x + 8, panel_y + 10, panel_w, panel_h, gfx_rgb(7, 10, 18), 72);
    gfx_fill_rect_alpha(panel_x + 3, panel_y + 4, panel_w, panel_h, gfx_rgb(7, 10, 18), 32);
    gfx_fill_rect(panel_x, panel_y, panel_w, panel_h, gfx_rgb(60, 82, 117));
    gfx_fill_rect_gradient_v(panel_x + 1, panel_y + 1, panel_w - 2, panel_h - 2,
                             gfx_rgb(30, 62, 108), gfx_rgb(16, 31, 55));
    gfx_fill_rect_alpha(panel_x + 1, panel_y + 1, panel_w - 2, 1, gfx_rgb(255, 255, 255), 110);
    gfx_draw_string_transparent(panel_x + (panel_w - gfx_text_width(title, FONT_HEIGHT)) / 2,
                                panel_y + panel_h / 2 - FONT_HEIGHT - 6,
                                title, gfx_rgb(241, 245, 255));
    if (detail && detail[0]) {
        gfx_draw_string_transparent(panel_x + (panel_w - gfx_text_width(detail, FONT_HEIGHT)) / 2,
                                    panel_y + panel_h / 2 + 8,
                                    detail, gfx_rgb(194, 209, 232));
    }
    gfx_swap();
}

static void power_show_status(const char *title, const char *detail) {
    if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
        power_show_graphics_status(title, detail);
    } else {
        power_show_text_status(title, detail);
    }
}

void power_shutdown(void) {
    power_show_status("Shutting down...", "Requesting ACPI power off.");
    cpu_cli();

    outw(0x604, 0x2000);
    power_spin_wait();

    outw(0xB004, 0x2000);
    power_spin_wait();

    power_show_status("Power off required", "Automatic shutdown failed. Power off the machine manually.");
    for (;;) cpu_hlt();
}

void power_reboot(void) {
    power_show_status("Restarting...", "Trying controller reset.");
    cpu_cli();

    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02u)) break;
    }
    outb(0x64, 0xFE);
    power_spin_wait();

    outb(0xCF9, 0x00);
    outb(0xCF9, 0x06);
    power_spin_wait();

    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) null_idt = { 0, 0 };
        __asm__ volatile("lidt %0; int $0" : : "m"(null_idt));
    }

    power_spin_wait();
    power_show_status("Restart failed", "Automatic reboot failed. Restart manually.");
    for (;;) cpu_hlt();
}
