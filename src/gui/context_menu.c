#include "gui/context_menu.h"

#include <stdint.h>

#include "drivers/gfx.h"
#include "drivers/icon.h"
#include "gui/theme.h"
#include "lib/string.h"

#define COL_BG         gfx_rgb(245, 248, 255)
#define COL_BG_ALT     gfx_rgb(237, 243, 251)
#define COL_BORDER     gfx_rgb(60,  77, 101)
#define COL_TXT        gfx_rgb(24,  35,  50)
#define COL_TXT_DIM    gfx_rgb(99,  115, 139)
#define COL_HOV        gfx_rgb(37,  99,  235)
#define COL_HOV_DANGER gfx_rgb(180, 52,  72)
#define COL_DANGER_TXT gfx_rgb(153, 27,  27)

static int    g_active = 0;
static int    g_x      = 0;
static int    g_y      = 0;
static int    g_w      = 0;
static int    g_h      = 0;
static int    g_count  = 0;
static int    g_hover  = -1;
static context_menu_item_t g_items[CONTEXT_MENU_MAX_ITEMS];

static int cm_row_h(void) {
    const th_metrics_t *m = th_metrics();
    int row_h = m->font_body + m->gap_md;
    if (row_h < m->min_hit) row_h = m->min_hit;
    return row_h;
}

static int cm_pad_x(void) {
    return th_metrics()->gap_md;
}

static int cm_pad_y(void) {
    return th_metrics()->gap_sm;
}

static int cm_icon_slot(void) {
    return th_metrics()->font_body + th_metrics()->gap_sm;
}

static int cm_has_icons(const context_menu_item_t *items, int count) {
    for (int i = 0; i < count; i++) {
        if (items[i].icon_id != ICON_NONE) return 1;
    }
    return 0;
}

void context_menu_measure(const context_menu_item_t *items, int count,
                          int *out_w, int *out_h) {
    const th_metrics_t *m = th_metrics();
    int row_h = cm_row_h();
    int pad_x = cm_pad_x();
    int pad_y = cm_pad_y();
    int width = 0;
    int has_icons = cm_has_icons(items, count);

    for (int i = 0; i < count; i++) {
        int tw = gfx_measure_text(FONT_ROLE_UI, m->font_body, items[i].label);
        int row_w = pad_x * 2 + tw;
        if (has_icons) row_w += cm_icon_slot();
        if (row_w > width) width = row_w;
    }

    if (width < 140) width = 140;
    if (out_w) *out_w = width;
    if (out_h) *out_h = count * row_h + pad_y * 2;
}

void context_menu_show(int x, int y,
                       const context_menu_item_t *items, int count) {
    int i;
    int menu_h;
    int sw;
    int sh;

    if (count > CONTEXT_MENU_MAX_ITEMS) count = CONTEXT_MENU_MAX_ITEMS;
    g_x      = x;
    g_y      = y;
    g_count  = count;
    g_hover  = -1;
    g_active = 1;
    for (i = 0; i < count; i++) g_items[i] = items[i];
    context_menu_measure(g_items, g_count, &g_w, &g_h);

    /* Clamp so menu stays on screen */
    menu_h = g_h;
    sw = (int)gfx_width();
    sh = (int)gfx_height();
    if (g_x + g_w > sw) g_x = sw - g_w;
    if (g_y + menu_h > sh) g_y = sh - menu_h;
    if (g_x < 0) g_x = 0;
    if (g_y < 0) g_y = 0;
}

void context_menu_dismiss(void) {
    g_active = 0;
    g_hover  = -1;
}

int context_menu_active(void) {
    return g_active;
}

void context_menu_paint(void) {
    const th_metrics_t *m = th_metrics();
    int row_h = cm_row_h();
    int pad_x = cm_pad_x();
    int pad_y = cm_pad_y();
    int icon_slot = cm_icon_slot();
    int has_icons = cm_has_icons(g_items, g_count);

    if (!g_active) return;

    /* Shadow */
    gfx_fill_rect_alpha(g_x + 4, g_y + 6, g_w, g_h, gfx_rgb(5, 8, 14), 38);
    gfx_fill_rect_alpha(g_x + 1, g_y + 2, g_w, g_h, gfx_rgb(5, 8, 14), 18);
    /* Background + border */
    gfx_fill_rect(g_x, g_y, g_w, g_h, COL_BORDER);
    gfx_fill_rect_gradient_v(g_x + 1, g_y + 1, g_w - 2, g_h - 2, gfx_rgb(252, 254, 255), COL_BG);
    gfx_fill_rect_alpha(g_x + 1, g_y + 1, g_w - 2, 1, gfx_rgb(255, 255, 255), 110);

    for (int i = 0; i < g_count; i++) {
        int iy = g_y + pad_y + i * row_h;
        uint32_t row_bg = (i & 1) ? COL_BG_ALT : COL_BG;
        uint32_t hover_bg = (g_items[i].style == CONTEXT_MENU_STYLE_DANGER) ? COL_HOV_DANGER : COL_HOV;
        uint32_t fg = (g_items[i].style == CONTEXT_MENU_STYLE_DANGER) ? COL_DANGER_TXT : COL_TXT;
        uint32_t icon_fg = (g_items[i].style == CONTEXT_MENU_STYLE_DANGER) ? COL_DANGER_TXT : COL_TXT_DIM;
        int tx = g_x + pad_x;
        int icon_y;

        if (i == g_hover) {
            row_bg = hover_bg;
            fg = gfx_rgb(255, 255, 255);
            icon_fg = gfx_rgb(255, 255, 255);
        }

        gfx_fill_rect(g_x + 1, iy, g_w - 2, row_h, row_bg);
        if (has_icons) {
            icon_y = iy + (row_h - m->font_body) / 2;
            if (g_items[i].icon_id != ICON_NONE) {
                icon_draw(g_x + pad_x, icon_y, m->font_body, g_items[i].icon_id, icon_fg);
            }
            tx += icon_slot;
        }
        gfx_draw_string_role_transparent(tx, iy + (row_h - m->font_body) / 2,
                                         g_items[i].label, FONT_ROLE_UI, m->font_body, fg);
    }
}

int context_menu_handle_pointer(int mx, int my, uint8_t pressed,
                                uint8_t released) {
    int row_h = cm_row_h();
    int pad_y = cm_pad_y();
    if (!g_active) return 0;

    int inside = (mx >= g_x && mx < g_x + g_w &&
                  my >= g_y && my < g_y + g_h);

    /* Update hover */
    if (inside) {
        g_hover = (my - g_y - pad_y) / row_h;
        if (g_hover < 0 || g_hover >= g_count) g_hover = -1;
    } else {
        g_hover = -1;
    }

    /* Left-click outside → dismiss */
    if ((pressed & 0x01u) && !inside) {
        context_menu_dismiss();
        return -1;
    }

    /* Left-click on item → activate */
    if ((released & 0x01u) && inside && g_hover >= 0) {
        int idx = g_hover;
        context_menu_dismiss();
        if (g_items[idx].action) {
            g_items[idx].action(g_items[idx].userdata);
        }
        return 1;
    }

    return 0;
}
