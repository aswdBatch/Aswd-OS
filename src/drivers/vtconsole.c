#include "drivers/vtconsole.h"

#include <stdint.h>

#include "common/colors.h"
#include "drivers/gfx.h"
#include "drivers/font.h"
#include "lib/string.h"

static uint16_t g_shadow[VTC_COLS * VTC_ROWS];
static int g_cursor_row, g_cursor_col;
static int g_dirty;

void vtc_init(void) {
    mem_set(g_shadow, 0, sizeof(g_shadow));
    g_cursor_row = 0;
    g_cursor_col = 0;
    g_dirty = 1;
}

static void draw_cell(int row, int col) {
    uint16_t entry = g_shadow[row * VTC_COLS + col];
    char c = (char)(entry & 0xFF);
    uint8_t attr = (uint8_t)(entry >> 8);
    uint8_t fg_idx = attr & 0x0F;
    uint8_t bg_idx = (attr >> 4) & 0x0F;

    int px = col * FONT_WIDTH;
    int py = row * FONT_HEIGHT;

    gfx_draw_char(px, py, c, g_vga_palette[fg_idx], g_vga_palette[bg_idx]);
}

void vtc_put_char_at(char c, uint8_t color, int row, int col) {
    if (row < 0 || col < 0 || row >= VTC_ROWS || col >= VTC_COLS) return;
    uint16_t entry = (uint16_t)((unsigned char)c) | ((uint16_t)color << 8);
    g_shadow[row * VTC_COLS + col] = entry;
    draw_cell(row, col);
    g_dirty = 1;
}

void vtc_fill_row(int row, char c, uint8_t color) {
    if (row < 0 || row >= VTC_ROWS) return;
    uint16_t entry = (uint16_t)((unsigned char)c) | ((uint16_t)color << 8);
    for (int col = 0; col < VTC_COLS; col++) {
        g_shadow[row * VTC_COLS + col] = entry;
        draw_cell(row, col);
    }
    g_dirty = 1;
}

void vtc_clear(uint8_t color) {
    uint16_t entry = (uint16_t)' ' | ((uint16_t)color << 8);
    for (int i = 0; i < VTC_COLS * VTC_ROWS; i++) {
        g_shadow[i] = entry;
    }
    /* Redraw entire screen */
    for (int row = 0; row < VTC_ROWS; row++) {
        for (int col = 0; col < VTC_COLS; col++) {
            draw_cell(row, col);
        }
    }
    g_dirty = 1;
}

void vtc_set_cursor(int row, int col) {
    g_cursor_row = row;
    g_cursor_col = col;
    /* No hardware cursor in graphics mode — could draw a blinking block */
    (void)g_cursor_row;
    (void)g_cursor_col;
}

void vtc_auto_flush(void) {
    if (!g_dirty) return;
    gfx_swap();
    g_dirty = 0;
}

uint16_t *vtc_shadow(void) {
    return g_shadow;
}
