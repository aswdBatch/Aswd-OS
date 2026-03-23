#include "gui/taskmgr.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
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

static void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    gfx_draw_string(x, y, text, fg, bg);
}

static int taskmgr_visible_rows(void) {
    gui_rect_t r;
    int rows;

    if (g_win_id < 0 || !gui_window_active(g_win_id)) {
        return 8;
    }

    r = gui_window_content(g_win_id);
    rows = (r.h - 84) / 20;
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
    gui_rect_t r = gui_window_content(win_id);
    char line[64];
    char tmp[16];
    int rows = gui_window_count();
    int visible = taskmgr_visible_rows();

    taskmgr_clamp_scroll();

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);

    line[0] = '\0';
    str_copy(line, "Focused: ", sizeof(line));
    if (gui_window_focused() >= 0) str_cat(line, gui_window_title(gui_window_focused()), sizeof(line));
    else str_cat(line, "(desktop)", sizeof(line));
    draw_text(r.x + 10, r.y + 8, line, COL_TEXT, COL_BG);

    line[0] = '\0';
    str_copy(line, "USB controllers: ", sizeof(line));
    u32_to_dec((uint32_t)usb_controller_count(), tmp, sizeof(tmp));
    str_cat(line, tmp, sizeof(line));
    draw_text(r.x + 10, r.y + 24, line, COL_DIM, COL_BG);

    line[0] = '\0';
    str_copy(line, "User: ", sizeof(line));
    str_cat(line, users_current(), sizeof(line));
    draw_text(r.x + 180, r.y + 24, line, COL_DIM, COL_BG);

    line[0] = '\0';
    str_copy(line, "Uptime: ", sizeof(line));
    u32_to_dec(timer_uptime_secs(), tmp, sizeof(tmp));
    str_cat(line, tmp, sizeof(line));
    str_cat(line, "s", sizeof(line));
    draw_text(r.x + 10, r.y + 40, line, COL_DIM, COL_BG);

    for (int i = 0; i < visible && g_scroll + i < rows; i++) {
        int idx = g_scroll + i;
        int id = gui_window_id_at(idx);
        int y = r.y + 66 + i * 20;
        uint32_t bg = (idx == g_selected) ? COL_SEL_BG : COL_BG;
        uint32_t fg = (idx == g_selected) ? COL_SEL_TXT : COL_TEXT;
        gfx_fill_rect(r.x + 8, y - 2, r.w - 16, 18, bg);
        line[0] = '\0';
        u32_to_dec((uint32_t)id, tmp, sizeof(tmp));
        str_copy(line, "#", sizeof(line));
        str_cat(line, tmp, sizeof(line));
        str_cat(line, " ", sizeof(line));
        str_cat(line, gui_window_title(id), sizeof(line));
        draw_text(r.x + 14, y, line, fg, bg);
    }

    draw_text(r.x + 10, r.y + r.h - 20,
              "Up/Down choose  Enter/Del close  Ctrl+Q exit", COL_DIM, COL_BG);
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
    g_selected = 0;
    g_scroll = 0;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_TASKMGR;
    w->on_paint = taskmgr_on_paint;
    w->on_key = taskmgr_on_key;
    w->on_close = taskmgr_on_close;
}
