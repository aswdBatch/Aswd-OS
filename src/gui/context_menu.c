#include "gui/context_menu.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "lib/string.h"

#define ITEM_H   22
#define MENU_W   180
#define PAD_X    10
#define PAD_Y    4

#define COL_BG     gfx_rgb(30,  34,  48)
#define COL_BORDER gfx_rgb(60,  68,  90)
#define COL_TXT    gfx_rgb(230, 235, 245)
#define COL_HOV    gfx_rgb(50,  110, 160)

static int    g_active = 0;
static int    g_x      = 0;
static int    g_y      = 0;
static int    g_count  = 0;
static int    g_hover  = -1;
static context_menu_item_t g_items[CONTEXT_MENU_MAX_ITEMS];

void context_menu_show(int x, int y,
                       const context_menu_item_t *items, int count) {
    int i;
    if (count > CONTEXT_MENU_MAX_ITEMS) count = CONTEXT_MENU_MAX_ITEMS;
    g_x      = x;
    g_y      = y;
    g_count  = count;
    g_hover  = -1;
    g_active = 1;
    for (i = 0; i < count; i++) g_items[i] = items[i];

    /* Clamp so menu stays on screen */
    int menu_h = g_count * ITEM_H + PAD_Y * 2;
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    if (g_x + MENU_W > sw) g_x = sw - MENU_W;
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
    int i;
    if (!g_active) return;

    int menu_h = g_count * ITEM_H + PAD_Y * 2;
    /* Shadow */
    gfx_fill_rect(g_x + 3, g_y + 3, MENU_W, menu_h, gfx_rgb(0, 0, 0));
    /* Background + border */
    gfx_fill_rect(g_x, g_y, MENU_W, menu_h, COL_BG);
    gfx_fill_rect(g_x, g_y, MENU_W, 1,       COL_BORDER);
    gfx_fill_rect(g_x, g_y + menu_h - 1, MENU_W, 1, COL_BORDER);
    gfx_fill_rect(g_x, g_y, 1, menu_h, COL_BORDER);
    gfx_fill_rect(g_x + MENU_W - 1, g_y, 1, menu_h, COL_BORDER);

    for (i = 0; i < g_count; i++) {
        int iy = g_y + PAD_Y + i * ITEM_H;
        if (i == g_hover) {
            gfx_fill_rect(g_x + 1, iy, MENU_W - 2, ITEM_H, COL_HOV);
        }
        uint32_t row_bg = (i == g_hover) ? COL_HOV : COL_BG;
        gfx_draw_string(g_x + PAD_X, iy + (ITEM_H - FONT_HEIGHT) / 2,
                        g_items[i].label, COL_TXT, row_bg);
    }
}

int context_menu_handle_pointer(int mx, int my, uint8_t pressed,
                                uint8_t released) {
    if (!g_active) return 0;

    int menu_h = g_count * ITEM_H + PAD_Y * 2;
    int inside = (mx >= g_x && mx < g_x + MENU_W &&
                  my >= g_y && my < g_y + menu_h);

    /* Update hover */
    if (inside) {
        g_hover = (my - g_y - PAD_Y) / ITEM_H;
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
