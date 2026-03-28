#pragma once

#include <stdint.h>

#define WC_MAX_COLS 192
#define WC_MAX_ROWS 64

typedef struct winconsole_t {
    char     cells[WC_MAX_ROWS * WC_MAX_COLS];
    uint8_t  attrs[WC_MAX_ROWS * WC_MAX_COLS];
    int      rows, cols;        /* actual usable size (from window content area) */
    int      cursor_row, cursor_col;
    int      scroll_top, scroll_bottom;
    uint8_t  fg, bg;            /* current VGA color indices */
    int      dirty;
    int      win_id;            /* owning gui_window_t id */
} winconsole_t;

void wc_init(winconsole_t *wc, int win_id);
void wc_resize(winconsole_t *wc, int content_w, int content_h);

void wc_putc(winconsole_t *wc, char c);
void wc_write(winconsole_t *wc, const char *s);
void wc_set_color(winconsole_t *wc, uint8_t fg, uint8_t bg);
void wc_clear(winconsole_t *wc);
void wc_put_char_at(winconsole_t *wc, char c, uint8_t attr, int row, int col);
void wc_fill_row(winconsole_t *wc, int row, char c, uint8_t attr);
void wc_set_cursor(winconsole_t *wc, int row, int col);
void wc_set_scroll_region(winconsole_t *wc, int top, int bottom);

/* Render the text buffer into the backbuffer at the given pixel position */
void wc_paint(winconsole_t *wc, int px, int py);

/* Pool management */
winconsole_t *wc_alloc(void);
void wc_free(winconsole_t *wc);
