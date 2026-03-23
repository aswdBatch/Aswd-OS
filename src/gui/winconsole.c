#include "gui/winconsole.h"

#include "common/colors.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "lib/string.h"

#define WC_POOL_SIZE 8

static winconsole_t g_pool[WC_POOL_SIZE];
static int g_pool_used[WC_POOL_SIZE];

winconsole_t *wc_alloc(void) {
    for (int i = 0; i < WC_POOL_SIZE; i++) {
        if (!g_pool_used[i]) {
            g_pool_used[i] = 1;
            return &g_pool[i];
        }
    }
    return 0;
}

void wc_free(winconsole_t *wc) {
    if (!wc) return;
    for (int i = 0; i < WC_POOL_SIZE; i++) {
        if (&g_pool[i] == wc) {
            g_pool_used[i] = 0;
            return;
        }
    }
}

void wc_init(winconsole_t *wc, int win_id) {
    mem_set(wc->cells, ' ', sizeof(wc->cells));
    mem_set(wc->attrs, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK),
            sizeof(wc->attrs));
    wc->rows = WC_MAX_ROWS;
    wc->cols = WC_MAX_COLS;
    wc->cursor_row = 0;
    wc->cursor_col = 0;
    wc->scroll_top = 0;
    wc->scroll_bottom = wc->rows - 1;
    wc->fg = VGA_COLOR_WHITE;
    wc->bg = VGA_COLOR_BLACK;
    wc->dirty = 1;
    wc->win_id = win_id;
}

void wc_resize(winconsole_t *wc, int content_w, int content_h) {
    int cols = content_w / FONT_WIDTH;
    int rows = content_h / FONT_HEIGHT;
    if (cols > WC_MAX_COLS) cols = WC_MAX_COLS;
    if (rows > WC_MAX_ROWS) rows = WC_MAX_ROWS;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    wc->cols = cols;
    wc->rows = rows;
    if (wc->scroll_bottom >= rows) wc->scroll_bottom = rows - 1;
    if (wc->cursor_row >= rows) wc->cursor_row = rows - 1;
    if (wc->cursor_col >= cols) wc->cursor_col = cols - 1;
}

void wc_set_color(winconsole_t *wc, uint8_t fg, uint8_t bg) {
    wc->fg = fg;
    wc->bg = bg;
}

void wc_put_char_at(winconsole_t *wc, char c, uint8_t attr, int row, int col) {
    if (row < 0 || col < 0 || row >= wc->rows || col >= wc->cols) return;
    wc->cells[row * WC_MAX_COLS + col] = c;
    wc->attrs[row * WC_MAX_COLS + col] = attr;
    wc->dirty = 1;
}

void wc_fill_row(winconsole_t *wc, int row, char c, uint8_t attr) {
    if (row < 0 || row >= wc->rows) return;
    for (int col = 0; col < wc->cols; col++) {
        wc->cells[row * WC_MAX_COLS + col] = c;
        wc->attrs[row * WC_MAX_COLS + col] = attr;
    }
    wc->dirty = 1;
}

void wc_set_cursor(winconsole_t *wc, int row, int col) {
    wc->cursor_row = row;
    wc->cursor_col = col;
}

void wc_set_scroll_region(winconsole_t *wc, int top, int bottom) {
    if (top < 0 || bottom < 0 || top >= wc->rows || bottom >= wc->rows || top > bottom) {
        wc->scroll_top = 0;
        wc->scroll_bottom = wc->rows - 1;
        return;
    }
    wc->scroll_top = top;
    wc->scroll_bottom = bottom;
}

void wc_clear(winconsole_t *wc) {
    uint8_t attr = vga_make_color(wc->fg, wc->bg);
    mem_set(wc->cells, ' ', sizeof(wc->cells));
    mem_set(wc->attrs, attr, sizeof(wc->attrs));
    wc->cursor_row = 0;
    wc->cursor_col = 0;
    wc->dirty = 1;
}

static void wc_scroll_up(winconsole_t *wc) {
    for (int row = wc->scroll_top + 1; row <= wc->scroll_bottom; row++) {
        for (int col = 0; col < wc->cols; col++) {
            int dst = (row - 1) * WC_MAX_COLS + col;
            int src = row * WC_MAX_COLS + col;
            wc->cells[dst] = wc->cells[src];
            wc->attrs[dst] = wc->attrs[src];
        }
    }
    uint8_t attr = vga_make_color(wc->fg, wc->bg);
    for (int col = 0; col < wc->cols; col++) {
        wc->cells[wc->scroll_bottom * WC_MAX_COLS + col] = ' ';
        wc->attrs[wc->scroll_bottom * WC_MAX_COLS + col] = attr;
    }
    wc->dirty = 1;
}

void wc_putc(winconsole_t *wc, char c) {
    if (c == '\n') {
        wc->cursor_col = 0;
        wc->cursor_row++;
        if (wc->cursor_row > wc->scroll_bottom) {
            wc_scroll_up(wc);
            wc->cursor_row = wc->scroll_bottom;
        }
        wc->dirty = 1;
        return;
    }
    if (c == '\r') {
        wc->cursor_col = 0;
        return;
    }
    if (c == '\b') {
        if (wc->cursor_col > 0) {
            wc->cursor_col--;
        } else if (wc->cursor_row > wc->scroll_top) {
            wc->cursor_row--;
            wc->cursor_col = wc->cols - 1;
        } else {
            return;
        }
        uint8_t attr = vga_make_color(wc->fg, wc->bg);
        wc->cells[wc->cursor_row * WC_MAX_COLS + wc->cursor_col] = ' ';
        wc->attrs[wc->cursor_row * WC_MAX_COLS + wc->cursor_col] = attr;
        wc->dirty = 1;
        return;
    }

    uint8_t attr = vga_make_color(wc->fg, wc->bg);
    wc->cells[wc->cursor_row * WC_MAX_COLS + wc->cursor_col] = c;
    wc->attrs[wc->cursor_row * WC_MAX_COLS + wc->cursor_col] = attr;
    wc->cursor_col++;
    if (wc->cursor_col >= wc->cols) {
        wc->cursor_col = 0;
        wc->cursor_row++;
        if (wc->cursor_row > wc->scroll_bottom) {
            wc_scroll_up(wc);
            wc->cursor_row = wc->scroll_bottom;
        }
    }
    wc->dirty = 1;
}

void wc_write(winconsole_t *wc, const char *s) {
    for (int i = 0; s && s[i]; i++) {
        wc_putc(wc, s[i]);
    }
}

void wc_paint(winconsole_t *wc, int px, int py) {
    for (int row = 0; row < wc->rows; row++) {
        for (int col = 0; col < wc->cols; col++) {
            int idx = row * WC_MAX_COLS + col;
            char c = wc->cells[idx];
            uint8_t attr = wc->attrs[idx];
            uint8_t fg_idx = attr & 0x0F;
            uint8_t bg_idx = (attr >> 4) & 0x0F;
            gfx_draw_char(px + col * FONT_WIDTH,
                          py + row * FONT_HEIGHT,
                          c,
                          g_vga_palette[fg_idx],
                          g_vga_palette[bg_idx]);
        }
    }
    wc->dirty = 0;
}
