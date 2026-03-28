#include "gui/taskmgr.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "usb/usb.h"
#include "users/users.h"

#define COL_BG      gfx_rgb(247, 249, 252)
#define COL_TEXT    gfx_rgb(30, 41, 59)
#define COL_DIM     gfx_rgb(100, 116, 139)
#define COL_SEL_BG  gfx_rgb(37, 99, 235)
#define COL_SEL_TXT gfx_rgb(255, 255, 255)

static int g_win_id = -1;
static int g_selected = 0;
static int g_scroll = 0;

static gui_rect_t taskmgr_list_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(g_win_id);
    gui_rect_t list;
    int cards_three = (r.w >= 440);

    list.x = r.x + 14;
    list.y = r.y + tm->header_h + (cards_three ? 124 : 194);
    list.w = r.w - 28;
    list.h = r.h - (list.y - r.y) - tm->status_h - 14;
    if (list.h < tm->list_row_h + 8) list.h = tm->list_row_h + 8;
    return list;
}

static int taskmgr_visible_rows(void) {
    gui_rect_t list;
    int rows;
    const th_metrics_t *tm = th_metrics();

    if (g_win_id < 0 || !gui_window_active(g_win_id)) {
        return 8;
    }

    list = taskmgr_list_rect();
    rows = list.h / (tm->list_row_h + 2);
    if (rows < 1) rows = 1;
    return rows;
}

static void taskmgr_clamp_scroll(void) {
    int rows = taskmgr_visible_rows();
    int count = gui_window_count();
    int max_scroll = count - rows;

    if (g_selected >= count) g_selected = count - 1;
    if (g_selected < 0) g_selected = 0;
    if (count <= 0) {
        g_selected = 0;
        g_scroll = 0;
        return;
    }

    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + rows) g_scroll = g_selected - rows + 1;
    if (max_scroll < 0) max_scroll = 0;
    if (g_scroll > max_scroll) g_scroll = max_scroll;
    if (g_scroll < 0) g_scroll = 0;
}

static void taskmgr_on_paint(int win_id) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(win_id);
    gui_rect_t list = taskmgr_list_rect();
    char line[64];
    char tmp[16];
    char summary[96];
    int rows = gui_window_count();
    int visible = taskmgr_visible_rows();
    int card_w;
    int card_y;
    int header_y = r.y + 10;
    int cards_three = (r.w >= 440);

    taskmgr_clamp_scroll();

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    th_draw_page_header(r.x + 8, header_y, r.w - 16,
                        "Processes",
                        "Session overview",
                        "Focused window, active user, and open apps in one compact view.");
    th_draw_statusbar(r.x, r.y + r.h - tm->status_h, r.w, tm->status_h,
                      "Up/Down choose  Enter/Del close  Ctrl+Q exit");

    line[0] = '\0';
    str_copy(line, "Focused: ", sizeof(line));
    if (gui_window_focused() >= 0) str_cat(line, gui_window_title(gui_window_focused()), sizeof(line));
    else str_cat(line, "Desktop", sizeof(line));
    summary[0] = '\0';
    str_copy(summary, "User ", sizeof(summary));
    str_cat(summary, users_current(), sizeof(summary));
    str_cat(summary, "  |  ", sizeof(summary));
    u32_to_dec(timer_uptime_secs(), tmp, sizeof(tmp));
    str_cat(summary, tmp, sizeof(summary));
    str_cat(summary, "s uptime", sizeof(summary));
    th_draw_info_strip(r.x + 8, header_y + tm->header_h + tm->gap_md, r.w - 16,
                       line, summary, "Live view");

    card_w = cards_three ? (r.w - 36) / 3 : (r.w - 34) / 2;
    card_y = header_y + tm->header_h + tm->gap_md + tm->font_body + tm->gap_md + 18;

    th_draw_card(r.x + 14, card_y, card_w, 62, "Open Windows", gfx_rgb(255, 255, 255), 0);
    u32_to_dec((uint32_t)rows, tmp, sizeof(tmp));
    th_draw_text(r.x + 28, card_y + 28, tmp, TH_ACCENT, TH_BG_CARD, tm->font_title);
    th_draw_text(r.x + 28, card_y + 46, "active entries", COL_DIM, TH_BG_CARD, tm->font_small);

    th_draw_card(r.x + 20 + card_w, card_y, card_w, 62, "USB", gfx_rgb(255, 255, 255), 0);
    u32_to_dec((uint32_t)usb_controller_count(), tmp, sizeof(tmp));
    th_draw_text(r.x + 34 + card_w, card_y + 28, tmp, TH_ACCENT, TH_BG_CARD, tm->font_title);
    th_draw_text(r.x + 34 + card_w, card_y + 46, "controller(s)", COL_DIM, TH_BG_CARD, tm->font_small);

    if (cards_three) {
        th_draw_card(r.x + 26 + card_w * 2, card_y, card_w, 62, "Session", gfx_rgb(255, 255, 255), 0);
        th_draw_text(r.x + 40 + card_w * 2, card_y + 28, users_current(), COL_TEXT, TH_BG_CARD, tm->font_body);
        th_draw_text(r.x + 40 + card_w * 2, card_y + 46,
                     users_current_is_admin() ? "admin account" : "standard account",
                     COL_DIM, TH_BG_CARD, tm->font_small);
    } else {
        th_draw_card(r.x + 14, card_y + 70, r.w - 28, 56, "Session", gfx_rgb(255, 255, 255), 0);
        th_draw_text(r.x + 28, card_y + 92, users_current(), COL_TEXT, TH_BG_CARD, tm->font_body);
        th_draw_text(r.x + 28, card_y + 110,
                     users_current_is_admin() ? "admin account" : "standard account",
                     COL_DIM, TH_BG_CARD, tm->font_small);
    }

    th_draw_card(list.x, list.y - 26, list.w, list.h + 26, 0, gfx_rgb(255, 255, 255), 0);
    th_draw_text(list.x + 12, list.y - 16, "Active windows", COL_DIM, TH_BG_CARD, tm->font_small);

    for (int i = 0; i < visible && g_scroll + i < rows; i++) {
        int idx = g_scroll + i;
        int id = gui_window_id_at(idx);
        int y = list.y + i * (tm->list_row_h + 2);
        uint32_t bg = (idx == g_selected) ? COL_SEL_BG : COL_BG;
        uint32_t fg = (idx == g_selected) ? COL_SEL_TXT : COL_TEXT;
        th_draw_list_row(list.x + 8, y, list.w - 16, tm->list_row_h, "", idx == g_selected);
        gfx_fill_rect(list.x + 9, y + 1, list.w - 18, tm->list_row_h - 2, bg);
        line[0] = '\0';
        u32_to_dec((uint32_t)id, tmp, sizeof(tmp));
        str_copy(line, "#", sizeof(line));
        str_cat(line, tmp, sizeof(line));
        str_cat(line, " ", sizeof(line));
        str_cat(line, gui_window_title(id), sizeof(line));
        th_draw_text(list.x + 16, y + (tm->list_row_h - tm->font_body) / 2, line, fg, bg, tm->font_body);
    }
}

static void taskmgr_close_selected(void) {
    int id = gui_window_id_at(g_selected);
    if (id >= 0) {
        gui_window_close(id);
        taskmgr_clamp_scroll();
    }
}

static void taskmgr_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) {
        gui_window_close(g_win_id);
        return;
    }
    if (key == KEY_UP && g_selected > 0) {
        g_selected--;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_DOWN && g_selected + 1 < gui_window_count()) {
        g_selected++;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_PAGEUP) {
        g_selected -= taskmgr_visible_rows();
        if (g_selected < 0) g_selected = 0;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_PAGEDOWN) {
        g_selected += taskmgr_visible_rows();
        if (g_selected >= gui_window_count()) g_selected = gui_window_count() - 1;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_HOME) {
        g_selected = 0;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_END && gui_window_count() > 0) {
        g_selected = gui_window_count() - 1;
        taskmgr_clamp_scroll();
        return;
    }
    if (key == KEY_DELETE || key == '\r' || key == '\n') {
        taskmgr_close_selected();
    }
}

static void taskmgr_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void taskmgr_launch(void) {
    gui_window_t *w;
    gui_rect_t rect;
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(540, 360, &rect);
    g_win_id = gui_window_create("Task Manager", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;
    gui_window_set_min_size(g_win_id, 460, 320);
    g_selected = 0;
    g_scroll = 0;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_TASKMGR;
    w->on_paint = taskmgr_on_paint;
    w->on_key = taskmgr_on_key;
    w->on_close = taskmgr_on_close;
}
