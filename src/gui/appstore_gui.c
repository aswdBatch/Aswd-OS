#include "gui/appstore_gui.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "gui/gui.h"
#include "lib/string.h"
#include "users/users.h"

/* Colors */
#define COL_HEADER    gfx_rgb(22,  34,  60)
#define COL_HDR_TXT   gfx_rgb(255, 255, 255)
#define COL_BG        gfx_rgb(245, 248, 255)
#define COL_ROW_ODD   gfx_rgb(255, 255, 255)
#define COL_ROW_EVN   gfx_rgb(238, 244, 254)
#define COL_TXT       gfx_rgb(28,  40,  66)
#define COL_TXT_DIM   gfx_rgb(100, 120, 155)
#define COL_LAUNCH    gfx_rgb(38,  99, 235)
#define COL_LAUNCH_TXT gfx_rgb(255, 255, 255)
#define COL_BORDER    gfx_rgb(200, 212, 235)
#define COL_TITLE     gfx_rgb(22,  34,  60)

#define HEADER_H   40
#define ROW_H      54
#define BTN_W      64
#define BTN_H      24
#define PAD        16

static int g_win_id = -1;
static int g_scroll = 0;

static void on_paint(int win_id) {
    gui_rect_t cr = gui_window_content(win_id);
    int cw = cr.w, ch = cr.h;

    gfx_fill_rect(cr.x, cr.y, cw, ch, COL_BG);

    /* Header */
    gfx_fill_rect(cr.x, cr.y, cw, HEADER_H, COL_HEADER);
    gfx_draw_string(cr.x + PAD, cr.y + (HEADER_H - FONT_HEIGHT) / 2,
                    "Local App Store", COL_HDR_TXT, COL_HEADER);

    /* App list — only apps flagged in_store */
    int n = gui_app_count();
    int y = cr.y + HEADER_H - g_scroll;
    int row = 0;

    for (int i = 0; i < n; i++) {
        const gui_app_t *app = gui_app_at(i);
        if (!app->in_store) continue;
        if (app->dev_only && !users_current_is_admin()) continue;

        uint32_t bg = (row % 2 == 0) ? COL_ROW_ODD : COL_ROW_EVN;
        if (y >= cr.y + HEADER_H && y + ROW_H <= cr.y + ch) {
            gfx_fill_rect(cr.x, y, cw, ROW_H, bg);
            gfx_fill_rect(cr.x, y + ROW_H - 1, cw, 1, COL_BORDER);
            /* App name */
            gfx_draw_string(cr.x + PAD, y + 10, app->label, COL_TXT, bg);
            /* Description line */
            char desc[64];
            str_copy(desc, "Optional app - ", sizeof(desc));
            str_cat(desc, app->id, sizeof(desc));
            gfx_draw_string(cr.x + PAD, y + 10 + FONT_HEIGHT + 4, desc, COL_TXT_DIM, bg);
            /* Launch button */
            int bx = cr.x + cw - BTN_W - PAD;
            int by = y + (ROW_H - BTN_H) / 2;
            gfx_fill_rect(bx, by, BTN_W, BTN_H, COL_LAUNCH);
            int tx = bx + (BTN_W - (int)str_len("Launch") * FONT_WIDTH) / 2;
            gfx_draw_string(tx, by + (BTN_H - FONT_HEIGHT) / 2,
                            "Launch", COL_LAUNCH_TXT, COL_LAUNCH);
        }
        y += ROW_H;
        row++;
    }

    if (row == 0) {
        gfx_draw_string(cr.x + PAD, cr.y + HEADER_H + 20,
                        "No optional apps available.", COL_TXT_DIM, COL_BG);
    }
}

static void on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    if (!(buttons & 0x01u)) return;

    gui_rect_t cr = gui_window_content(win_id);
    int cw = cr.w;
    /* mx, my are content-relative (0,0 = top-left of content area) */
    int y = HEADER_H - g_scroll;
    int n = gui_app_count();

    for (int i = 0; i < n; i++) {
        const gui_app_t *app = gui_app_at(i);
        if (!app->in_store) continue;
        if (app->dev_only && !users_current_is_admin()) continue;
        int row_y = y;
        y += ROW_H;
        if (my < row_y || my >= row_y + ROW_H) continue;
        /* Check launch button — content-relative coords */
        int bx = cw - BTN_W - PAD;
        int by = row_y + (ROW_H - BTN_H) / 2;
        if (mx >= bx && mx < bx + BTN_W &&
            my >= by && my < by + BTN_H) {
            if (app->launch) app->launch();
        }
    }
}

static void on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void appstore_gui_launch(void) {
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_rect_t r;
    gui_window_suggest_rect(480, 420, &r);
    g_win_id = gui_window_create("App Store", r.x, r.y, r.w, r.h);
    if (g_win_id < 0) return;
    gui_window_t *w = gui_get_window(g_win_id);
    w->on_paint = on_paint;
    w->on_mouse = on_mouse;
    w->on_close = on_close;
    g_scroll = 0;
    gui_repaint();
}
