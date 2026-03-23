#include "gui/theme.h"

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "lib/string.h"

void th_draw_panel(int x, int y, int w, int h, const char *header) {
    /* outer border */
    gfx_fill_rect(x, y, w, h, TH_BORDER);
    /* inner background */
    gfx_fill_rect(x + 2, y + 2, w - 4, h - 4, TH_BG_PANEL);
    /* header bar */
    gfx_fill_rect(x + 2, y + 2, w - 4, 28, TH_ACCENT);
    if (header && header[0])
        gfx_draw_string(x + 12, y + 7, header, TH_TEXT_INVERT, TH_ACCENT);
}

void th_draw_list_row(int x, int y, int w, int h, const char *text, int selected) {
    uint32_t bg = selected ? TH_SEL_BG  : TH_BG_CONTENT;
    uint32_t fg = selected ? TH_SEL_TXT : TH_TEXT;
    int ty = y + (h - FONT_HEIGHT) / 2;
    gfx_fill_rect(x, y, w, h, bg);
    if (text && text[0])
        gfx_draw_string(x + 8, ty, text, fg, bg);
}

void th_draw_button(int x, int y, int w, int h, const char *label, int hot) {
    uint32_t bg = hot ? TH_ACCENT_HOT : TH_ACCENT;
    int llen = label ? (int)str_len(label) : 0;
    int lx = x + (w - llen * FONT_WIDTH) / 2;
    int ly = y + (h - FONT_HEIGHT) / 2;
    gfx_fill_rect(x, y, w, h, bg);
    if (label && label[0])
        gfx_draw_string(lx, ly, label, TH_TEXT_INVERT, bg);
}

void th_draw_section_header(int x, int y, int w, const char *label, uint32_t bg) {
    gfx_fill_rect(x, y, w, 20, bg);
    if (label && label[0])
        gfx_draw_string(x + 8, y + 2, label, TH_TEXT_INVERT, bg);
}

void th_draw_separator(int x, int y, int w) {
    gfx_fill_rect(x, y, w, 1, TH_RULE);
}

void th_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg) {
    int bw = (text ? (int)str_len(text) * FONT_WIDTH : 0) + 8;
    int bh = FONT_HEIGHT + 4;
    gfx_fill_rect(x, y, bw, bh, bg);
    if (text && text[0])
        gfx_draw_string(x + 4, y + 2, text, fg, bg);
}

void th_draw_field(int x, int y, int w, const char *text, int focused, int masked) {
    uint32_t edge = focused ? TH_FIELD_FOCUS : TH_FIELD_EDGE;
    int fh = FONT_HEIGHT + 10;
    int cy = y + (fh - FONT_HEIGHT) / 2;
    int cx = x + 6;
    int len = text ? (int)str_len(text) : 0;
    int i;

    gfx_fill_rect(x, y, w, fh, edge);
    gfx_fill_rect(x + 1, y + 1, w - 2, fh - 2, TH_BG_FIELD);

    for (i = 0; i < len; i++) {
        char c = masked ? '*' : text[i];
        gfx_draw_char(cx + i * FONT_WIDTH, cy, c, TH_TEXT, TH_BG_FIELD);
    }
    gfx_draw_char(cx + len * FONT_WIDTH, cy, '_', TH_TEXT, TH_BG_FIELD);
}
