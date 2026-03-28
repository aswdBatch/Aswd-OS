#include "gui/appstore_gui.h"

#include <stdint.h>

#include "drivers/icon.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "gui/gui.h"
#include "gui/permission_gui.h"
#include "gui/theme.h"
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

#define BTN_W      86

static int g_win_id = -1;
static int g_scroll = 0;

static icon_asset_id_t appstore_icon_for(const gui_app_t *app) {
    if (!app) return ICON_NONE;
    switch (app->icon_kind) {
        case GUI_ICON_TERMINAL: return ICON_APP_TERMINAL;
        case GUI_ICON_FILES:    return ICON_APP_FILES;
        case GUI_ICON_EDITOR:   return ICON_APP_EDITOR;
        case GUI_ICON_OSINFO:   return ICON_APP_OSINFO;
        case GUI_ICON_SETTINGS: return ICON_APP_SETTINGS;
        case GUI_ICON_TASKMGR:  return ICON_APP_TASKMGR;
        case GUI_ICON_SNAKE:    return ICON_APP_SNAKE;
        case GUI_ICON_NOTES:    return ICON_APP_NOTES;
        case GUI_ICON_STORE:    return ICON_APP_STORE;
        case GUI_ICON_CALC:     return ICON_APP_CALC;
        case GUI_ICON_BROWSER:  return ICON_APP_BROWSER;
        case GUI_ICON_AXDOCS:   return ICON_APP_AXDOCS;
        case GUI_ICON_AXSTUDIO: return ICON_APP_AXSTUDIO;
        case GUI_ICON_WORK180:  return ICON_APP_WORK180;
        default: return ICON_NONE;
    }
}

static void on_paint(int win_id) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t cr = gui_window_content(win_id);
    th_layout_bucket_t bucket = th_layout_bucket_for_width(cr.w);
    gui_rect_t list;
    int row_h = tm->list_row_h + (bucket == TH_LAYOUT_COMPACT ? 28 : 34);
    int icon_sz = (bucket == TH_LAYOUT_COMPACT) ? 24 : 32;

    gfx_fill_rect(cr.x, cr.y, cr.w, cr.h, COL_BG);
    th_draw_page_header(cr.x + 8, cr.y + 8, cr.w - 16,
                        "Apps",
                        "App Store",
                        "Launch optional desktop apps from one place.");
    th_draw_info_strip(cr.x + 8, cr.y + 8 + th_page_header_height(), cr.w - 16,
                       "Optional apps", users_current_is_admin() ? "Admin can open all entries" : "Developer apps request approval",
                       "Local catalog");
    th_draw_statusbar(cr.x, cr.y + cr.h - tm->status_h, cr.w, tm->status_h,
                      "Optional apps live here. Developer apps require admin approval.");
    list.x = cr.x + 12;
    list.y = cr.y + 8 + th_page_header_height() + th_info_strip_height() + tm->gap_md;
    list.w = cr.w - 24;
    list.h = cr.h - (list.y - cr.y) - tm->status_h - 12;
    th_draw_card(list.x, list.y, list.w, list.h, 0, TH_BG_CARD, 0);

    /* App list - only apps flagged in_store */
    int n = gui_app_count();
    int y = list.y + 10 - g_scroll;
    int row = 0;

    for (int i = 0; i < n; i++) {
        const gui_app_t *app = gui_app_at(i);
        if (!app->in_store) continue;

        uint32_t bg = (row % 2 == 0) ? COL_ROW_ODD : COL_ROW_EVN;
        if (y >= list.y + 4 && y + row_h <= list.y + list.h - 4) {
            int bx = list.x + list.w - BTN_W - 14;
            int by = y + (row_h - tm->button_h) / 2;
            char desc[64];
            th_draw_list_row(list.x + 8, y, list.w - 16, row_h, "", 0);
            gfx_fill_rect(list.x + 9, y + 1, list.w - 18, row_h - 2, bg);
            if (appstore_icon_for(app) != ICON_NONE) {
                icon_draw(list.x + 18, y + (row_h - icon_sz) / 2, icon_sz, appstore_icon_for(app), 0);
            }
            th_draw_text(list.x + 28 + icon_sz, y + tm->gap_sm,
                         app->label, COL_TXT, bg, tm->font_title);
            str_copy(desc, app->dev_only ? "[Admin] " : "", sizeof(desc));
            str_cat(desc, "Optional app - ", sizeof(desc));
            str_cat(desc, app->id, sizeof(desc));
            th_draw_text(list.x + 28 + icon_sz, y + tm->gap_sm + tm->font_title + 2, desc,
                         COL_TXT_DIM, bg, tm->font_small);
            if (app->dev_only) {
                th_draw_badge(bx - 74, y + tm->gap_sm + 2, "Admin", TH_ACCENT_DARK, TH_TEXT_INVERT);
            }
            th_draw_button(bx, by, BTN_W, tm->button_h, "Launch", 0);
        }
        y += row_h;
        row++;
    }

    if (row == 0) {
        th_draw_empty_state(list.x + 18, list.y + 18, list.w - 36, list.h - 36,
                            "No optional apps available",
                            "The local app catalog is empty right now.");
    }
}

static void on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t cr = gui_window_content(win_id);
    gui_rect_t list;
    th_layout_bucket_t bucket = th_layout_bucket_for_width(cr.w);
    int row_h = tm->list_row_h + (bucket == TH_LAYOUT_COMPACT ? 28 : 34);
    if (!(buttons & 0x01u)) return;

    list.x = 12;
    list.y = 8 + th_page_header_height() + th_info_strip_height() + tm->gap_md;
    list.w = cr.w - 24;
    list.h = cr.h - list.y - tm->status_h - 12;
    /* mx, my are content-relative (0,0 = top-left of content area) */
    int y = list.y + 10 - g_scroll;
    int n = gui_app_count();

    for (int i = 0; i < n; i++) {
        const gui_app_t *app = gui_app_at(i);
        if (!app->in_store) continue;
        int row_y = y;
        y += row_h;
        if (my < row_y || my >= row_y + row_h) continue;
        /* Check launch button - content-relative coords */
        int bx = list.x + list.w - BTN_W - 14;
        int by = row_y + (row_h - tm->button_h) / 2;
        if (mx >= bx && mx < bx + BTN_W &&
            my >= by && my < by + tm->button_h) {
            if (app->dev_only && !users_current_is_admin()) {
                if (!permission_prompt_run("install a developer app")) {
                    gui_repaint();
                    return;
                }
                gui_repaint();
            }
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
    gui_window_set_min_size(g_win_id, 420, 320);
    gui_window_t *w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_STORE;
    w->on_paint = on_paint;
    w->on_mouse = on_mouse;
    w->on_close = on_close;
    g_scroll = 0;
    gui_repaint();
}
