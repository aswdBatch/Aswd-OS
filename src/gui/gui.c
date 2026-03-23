#include "gui/gui.h"

#include <stdint.h>

#include "common/config.h"
#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "gui/appstore_gui.h"
#include "gui/axdocs_gui.h"
#include "gui/axstudio_gui.h"
#include "gui/browser_gui.h"
#include "gui/calc_gui.h"
#include "gui/context_menu.h"
#include "gui/dev_tools.h"
#include "gui/editor_gui.h"
#include "gui/files_gui.h"
#include "gui/notes_gui.h"
#include "gui/osinfo_gui.h"
#include "gui/settings_gui.h"
#include "gui/theme.h"
#include "gui/shell_gui.h"
#include "gui/snake_gui.h"
#include "gui/taskmgr.h"
#include "net/net.h"
#include "input/input.h"
#include "usb/usb.h"
#include "lib/string.h"
#include "auth/auth_store.h"
#include "users/users.h"
#include "cpu/ports.h"

static uint32_t g_desktop_color = 0; /* initialized in gui_init */
#define COL_DESKTOP g_desktop_color
#define COL_DESKTOP_BAND     gfx_rgb(20, 102, 130)
#define COL_DESKTOP_TXT      gfx_rgb(241, 245, 249)
#define COL_ICON_SEL         gfx_rgb(14, 116, 144)
#define COL_ICON_SEL_ACTIVE  gfx_rgb(37, 99, 235)
#define COL_ICON_TILE        gfx_rgb(229, 238, 248)
#define COL_ICON_TILE_ACTIVE gfx_rgb(255, 255, 255)
#define COL_ICON_STROKE      gfx_rgb(34, 55, 78)
#define COL_ICON_PANEL       gfx_rgb(24, 35, 50)
#define COL_ICON_ACCENT      gfx_rgb(59, 130, 246)
#define COL_ICON_APPLE       gfx_rgb(220, 58, 70)
#define COL_ICON_APPLE_GLOW  gfx_rgb(255, 132, 132)
#define COL_ICON_STEM        gfx_rgb(101, 67, 33)
#define COL_ICON_LEAF        gfx_rgb(67, 176, 88)
#define COL_TASKBAR          gfx_rgb(19, 23, 34)
#define COL_TASKBAR_TOP      gfx_rgb(63, 76, 98)
#define COL_TASKBAR_TXT      gfx_rgb(236, 242, 255)
#define COL_TASKBAR_DIM      gfx_rgb(131, 149, 179)
#define COL_START_BG         gfx_rgb(28, 116, 188)
#define COL_START_OPEN       gfx_rgb(38, 144, 216)
#define COL_WIN_BG           gfx_rgb(244, 247, 251)
#define COL_WIN_BORDER       gfx_rgb(50, 62, 82)
#define COL_TITLE_FOCUS      gfx_rgb(27, 104, 188)
#define COL_TITLE_BLUR       gfx_rgb(107, 119, 142)
#define COL_TITLE_TXT        gfx_rgb(255, 255, 255)
#define COL_CLOSE_BG         gfx_rgb(193, 46, 66)
#define COL_CLOSE_TXT        gfx_rgb(255, 255, 255)
#define COL_MENU_BG          gfx_rgb(245, 248, 255)
#define COL_MENU_BORDER      gfx_rgb(52, 65, 86)
#define COL_MENU_ITEM        gfx_rgb(59, 130, 246)
#define COL_MENU_ITEM_TXT    gfx_rgb(255, 255, 255)
#define COL_MENU_TXT         gfx_rgb(24, 35, 50)
#define COL_MENU_DIM         gfx_rgb(98, 111, 134)
#define COL_MENU_HEAD        gfx_rgb(20, 96, 162)
#define COL_MENU_RULE        gfx_rgb(210, 220, 236)
#define COL_MENU_ACTIVE      gfx_rgb(224, 234, 249)
#define COL_FIELD_BG         gfx_rgb(238, 244, 251)
#define COL_STATUS_BAD       gfx_rgb(180, 52, 72)
#define COL_BTN_BG           gfx_rgb(57, 68, 88)
#define COL_BTN_ACTIVE       gfx_rgb(71, 91, 122)
#define COL_CURSOR_FG        gfx_rgb(255, 255, 255)
#define COL_CURSOR_BG        gfx_rgb(0, 0, 0)

#define START_BUTTON_X   8
#define START_BUTTON_W   68
#define START_MENU_W     380
#define START_HDR_H      40
#define START_FOOTER_H   48
#define START_APP_COLS   3
#define START_CELL_W     116
#define START_CELL_H     58

#define RESIZE_HANDLE    10
#define WIN_MIN_W        220
#define WIN_MIN_H        160

#define COL_MINIMIZE_BG  gfx_rgb(200, 170, 30)
#define COL_WIN_MINIMIZED gfx_rgb(38, 46, 62)

#define DESKTOP_ICON_MARGIN_X 16
#define DESKTOP_ICON_MARGIN_Y 22
#define DESKTOP_ICON_SLOT_W   92
#define DESKTOP_ICON_SLOT_H   84
#define DESKTOP_ICON_GAP_X    12
#define DESKTOP_ICON_GAP_Y    10
#define DESKTOP_ICON_SIZE     32
#define DESKTOP_DBLCLICK_TICKS 40u
#define CURSOR_W 16
#define CURSOR_H 16

typedef enum {
    SHELL_FOCUS_DESKTOP = 0,
    SHELL_FOCUS_START,
    SHELL_FOCUS_TASKBAR,
    SHELL_FOCUS_WINDOW,
} shell_focus_zone_t;

typedef enum {
    SESSION_ACTION_ADD_USER = 0,
    SESSION_ACTION_SHUTDOWN,
    SESSION_ACTION_REBOOT,
    SESSION_ACTION_REBOOT_ASWD,
    SESSION_ACTION_LOGOUT,
    SESSION_ACTION_DEV_TOOLS,
} session_action_t;

typedef struct {
    int x;
    int w;
    int win_id;
} taskbar_slot_t;

static gui_window_t g_windows[GUI_MAX_WINDOWS];
static int g_zorder[GUI_MAX_WINDOWS];
static int g_zcount;
static int g_focus = -1;
static int g_drag_win = -1;
static int g_resize_win = -1;
static int g_start_open = 0;
static int g_start_sel = 0;
static int g_taskbar_sel = 0;
static int g_desktop_sel = 0;
static int g_last_icon_click = -1;
static uint32_t g_last_icon_click_ticks = 0;
static shell_focus_zone_t g_shell_focus = SHELL_FOCUS_DESKTOP;
static taskbar_slot_t g_taskbar_slots[GUI_MAX_WINDOWS];
static int g_cursor_drawn = 0;
static int g_cursor_x = 0;
static int g_cursor_y = 0;
static int g_logout_requested = 0;

/* App search overlay */
static int  g_search_active = 0;
static char g_search_buf[64];
static int  g_search_len    = 0;
static int  g_search_sel    = 0;

static uint32_t icon_badge_col(int k);
static void perform_session_action(session_action_t action);

/* Fields: id, label, desktop_label, icon, show_on_desktop, single_instance, in_store, dev_only, launch */
static const gui_app_t g_apps[] = {
    { "terminal",  "Terminal",      "Terminal",  GUI_ICON_TERMINAL, 1, 1, 0, 0, shell_gui_launch },
    { "files",     "Files",         "Files",     GUI_ICON_FILES,    1, 1, 0, 0, files_gui_launch },
    { "notes",     "Notes",         "Notes",     GUI_ICON_NOTES,    1, 0, 0, 0, notes_gui_launch },
    { "editor",    "AX Code",       "AX Code",   GUI_ICON_EDITOR,   1, 1, 0, 0, editor_gui_launch },
    { "store",     "App Store",     "AppStore",  GUI_ICON_STORE,    1, 1, 0, 0, appstore_gui_launch },
    { "osinfo",    "OS Info",       "OS Info",   GUI_ICON_OSINFO,   1, 1, 0, 0, osinfo_gui_launch },
    { "ctrlpanel", "Control Panel", "CtrlPanel", GUI_ICON_SETTINGS, 1, 1, 0, 0, settings_gui_launch },
    { "taskmgr",   "Task Manager",  "TaskMgr",   GUI_ICON_TASKMGR,  1, 1, 0, 0, taskmgr_launch },
    /* Apps below live in the App Store, not on the desktop */
    { "snake",     "Snake",         "Snake",     GUI_ICON_SNAKE,    0, 1, 1, 0, snake_gui_launch },
    { "calc",      "Calculator",    "Calc",      GUI_ICON_CALC,     0, 1, 1, 0, calc_gui_launch },
    { "browser",   "Browser",       "Browser",   GUI_ICON_BROWSER,  0, 1, 1, 1, browser_gui_launch },
    { "axstudio",  "AX Studio",     "AXStudio",  GUI_ICON_AXSTUDIO, 1, 1, 0, 0, axstudio_gui_launch },
};

static int is_up_key(char c) {
    return c == KEY_UP || c == 'w' || c == 'k';
}

static int is_down_key(char c) {
    return c == KEY_DOWN || c == 's' || c == 'j';
}

static const uint16_t cursor_bitmap[16] = {
    0x8000, 0xC000, 0xE000, 0xF000,
    0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFC00, 0xF800, 0xF000, 0xE000,
    0xC000, 0x8000, 0x0000, 0x0000,
};

static const uint16_t cursor_mask[16] = {
    0xC000, 0xE000, 0xF000, 0xF800,
    0xFC00, 0xFE00, 0xFF00, 0xFF80,
    0xFE00, 0xFC00, 0xF800, 0xF000,
    0xE000, 0xC000, 0x0000, 0x0000,
};

static void draw_cursor_front(int mx, int my) {
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            uint16_t bit = (uint16_t)(0x8000u >> col);
            if (cursor_mask[row] & bit) {
                uint32_t c = (cursor_bitmap[row] & bit) ? COL_CURSOR_FG : COL_CURSOR_BG;
                gfx_put_pixel_front(mx + col, my + row, c);
            }
        }
    }
}

static void present_cursor_overlay(int mx, int my) {
    if (g_cursor_drawn) {
        gfx_present_rect(g_cursor_x, g_cursor_y, CURSOR_W, CURSOR_H);
    }
    gfx_present_rect(mx, my, CURSOR_W, CURSOR_H);
    draw_cursor_front(mx, my);
    g_cursor_x = mx;
    g_cursor_y = my;
    g_cursor_drawn = 1;
}

static void clip_title(char *out, size_t out_size, const char *title, int max_chars) {
    int i;
    if (out_size == 0) return;
    if (max_chars < 1) max_chars = 1;
    for (i = 0; title && title[i] && i < max_chars && i + 1 < (int)out_size; i++) {
        out[i] = title[i];
    }
    out[i] = '\0';
}

static void draw_label(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    gfx_draw_string(x, y, text, fg, bg);
}

static int text_width(const char *text) {
    return (int)str_len(text) * FONT_WIDTH;
}

gui_rect_t gui_desktop_bounds(void) {
    gui_rect_t area;
    area.x = 0;
    area.y = 0;
    area.w = (int)gfx_width();
    area.h = (int)gfx_height() - TASKBAR_HEIGHT;
    if (area.h < 0) area.h = 0;
    return area;
}

static void fit_window_frame(int *x, int *y, int *w, int *h) {
    gui_rect_t area = gui_desktop_bounds();
    int margin = 8;
    int max_w = area.w - margin * 2;
    int max_h = area.h - margin * 2;
    int min_w = 220;
    int min_h = TITLE_BAR_HEIGHT + 96;

    if (max_w < 64) max_w = area.w;
    if (max_h < 64) max_h = area.h;
    if (max_w < min_w) min_w = max_w;
    if (max_h < min_h) min_h = max_h;

    if (*w > max_w) *w = max_w;
    if (*h > max_h) *h = max_h;
    if (*w < min_w) *w = min_w;
    if (*h < min_h) *h = min_h;

    if (*x < margin) *x = margin;
    if (*y < margin) *y = margin;
    if (*x + *w > area.w - margin) *x = area.w - margin - *w;
    if (*y + *h > area.h - margin) *y = area.h - margin - *h;
    if (*x < margin) *x = margin;
    if (*y < margin) *y = margin;
}

void gui_window_suggest_rect(int pref_w, int pref_h, gui_rect_t *out) {
    gui_rect_t area = gui_desktop_bounds();
    int x;
    int y;
    int w;
    int h;

    if (!out) return;

    w = pref_w;
    h = pref_h;
    x = area.x + (area.w - w) / 2;
    y = area.y + (area.h - h) / 3;
    fit_window_frame(&x, &y, &w, &h);

    out->x = x;
    out->y = y;
    out->w = w;
    out->h = h;
}

static int desktop_icon_count(void) {
    int count = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (g_apps[i].show_on_desktop) count++;
    }
    return count;
}

static int desktop_app_index(int visible_idx) {
    int seen = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (!g_apps[i].show_on_desktop) continue;
        if (seen == visible_idx) return i;
        seen++;
    }
    return -1;
}

static int desktop_rows_per_column(void) {
    int available = (int)gfx_height() - TASKBAR_HEIGHT - DESKTOP_ICON_MARGIN_Y - 12;
    int rows = available / (DESKTOP_ICON_SLOT_H + DESKTOP_ICON_GAP_Y);
    if (rows < 1) rows = 1;
    return rows;
}

static int desktop_slot_rect(int visible_idx, gui_rect_t *out) {
    int count = desktop_icon_count();
    int rows = desktop_rows_per_column();
    int col;
    int row;

    if (visible_idx < 0 || visible_idx >= count || !out) {
        return 0;
    }

    col = visible_idx / rows;
    row = visible_idx % rows;
    out->x = DESKTOP_ICON_MARGIN_X + col * (DESKTOP_ICON_SLOT_W + DESKTOP_ICON_GAP_X);
    out->y = DESKTOP_ICON_MARGIN_Y + row * (DESKTOP_ICON_SLOT_H + DESKTOP_ICON_GAP_Y);
    out->w = DESKTOP_ICON_SLOT_W;
    out->h = DESKTOP_ICON_SLOT_H;
    return 1;
}

static void fill_desktop_background(void) {
    int sw  = gfx_width();
    int sh  = gfx_height();
    int dh  = sh - TASKBAR_HEIGHT;
    uint32_t base = COL_DESKTOP;
    int br = (int)((base >> 16) & 0xFF);
    int bg = (int)((base >>  8) & 0xFF);
    int bb = (int)( base        & 0xFF);
    /* Fade to near-black over 32 bands */
    int N  = 32;
    int i;
    for (i = 0; i < N; i++) {
        int y0 = dh * i / N;
        int y1 = dh * (i + 1) / N;
        /* interpolate from base color (top) down to near-black (bottom) */
        int r = br * (N - i) / N + 8  * i / N;
        int g = bg * (N - i) / N + 10 * i / N;
        int b = bb * (N - i) / N + 14 * i / N;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        gfx_fill_rect(0, y0, sw, y1 - y0, gfx_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b));
    }
    gfx_fill_rect(0, dh, sw, sh - dh, gfx_rgb(19, 23, 34));
}

/* ---- New start menu helpers ---- */
static int start_menu_desktop_count(void) {
    int n = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (g_apps[i].show_on_desktop) n++;
    }
    return n;
}

/* visible_idx → real app index */
static int start_desktop_app_index(int vis) {
    int seen = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (!g_apps[i].show_on_desktop) continue;
        if (seen == vis) return i;
        seen++;
    }
    return -1;
}

static int start_menu_grid_rows(void) {
    int n = start_menu_desktop_count();
    return (n + START_APP_COLS - 1) / START_APP_COLS;
}

static int start_menu_h(void) {
    return START_HDR_H + start_menu_grid_rows() * START_CELL_H + START_FOOTER_H;
}

static int start_menu_x(void) { return 8; }

static int start_menu_y(void) {
    int y = (int)gfx_height() - TASKBAR_HEIGHT - start_menu_h() - 4;
    if (y < 4) y = 4;
    return y;
}

static void draw_start_menu(void) {
    int x   = start_menu_x();
    int y   = start_menu_y();
    int h   = start_menu_h();
    int w   = START_MENU_W;
    int grid_y = y + START_HDR_H;
    int footer_y = y + h - START_FOOTER_H;
    const char *uname = auth_session_active() ? auth_session_username() : users_current();
    char greet[48];

    /* outer border */
    gfx_fill_rect(x, y, w, h, COL_MENU_BORDER);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, COL_MENU_BG);

    /* header */
    gfx_fill_rect(x + 1, y + 1, w - 2, START_HDR_H - 1, gfx_rgb(16, 32, 64));
    greet[0] = '\0';
    str_copy(greet, "good morning, ", sizeof(greet));
    str_cat(greet, uname, sizeof(greet));
    str_cat(greet, "!", sizeof(greet));
    draw_label(x + 14, y + (START_HDR_H - FONT_HEIGHT) / 2, greet, gfx_rgb(240, 246, 255), gfx_rgb(16, 32, 64));

    /* app grid */
    {
        int n = start_menu_desktop_count();
        for (int vi = 0; vi < n; vi++) {
            int ai   = start_desktop_app_index(vi);
            int col  = vi % START_APP_COLS;
            int row  = vi / START_APP_COLS;
            int cx   = x + 1 + col * START_CELL_W;
            int cy   = grid_y + row * START_CELL_H;
            int sel  = (g_start_sel == vi);
            uint32_t bg = sel ? COL_MENU_ACTIVE : COL_MENU_BG;
            char clip[14];

            gfx_fill_rect(cx, cy, START_CELL_W, START_CELL_H, bg);
            /* icon badge */
            gfx_fill_rect(cx + (START_CELL_W - 10) / 2, cy + 10, 10, 10,
                          icon_badge_col((int)g_apps[ai].icon_kind));
            /* label */
            clip_title(clip, sizeof(clip), g_apps[ai].desktop_label, START_CELL_W / FONT_WIDTH - 1);
            {
                int lw = (int)str_len(clip) * FONT_WIDTH;
                draw_label(cx + (START_CELL_W - lw) / 2, cy + 24, clip, COL_MENU_TXT, bg);
            }
            /* grid lines */
            gfx_fill_rect(cx + START_CELL_W - 1, cy, 1, START_CELL_H, COL_MENU_RULE);
            gfx_fill_rect(cx, cy + START_CELL_H - 1, START_CELL_W, 1, COL_MENU_RULE);
        }
    }

    /* footer */
    gfx_fill_rect(x + 1, footer_y, w - 2, START_FOOTER_H, gfx_rgb(22, 30, 50));
    gfx_fill_rect(x + 1, footer_y, w - 2, 1, gfx_rgb(50, 65, 95));
    /* avatar circle */
    gfx_fill_rect(x + 10, footer_y + (START_FOOTER_H - 20) / 2, 20, 20, gfx_rgb(59, 130, 246));
    draw_label(x + 14, footer_y + (START_FOOTER_H - 20) / 2 + 2, "U", gfx_rgb(255,255,255), gfx_rgb(59,130,246));
    /* username */
    {
        char un[20];
        clip_title(un, sizeof(un), uname, 14);
        draw_label(x + 36, footer_y + 8, un, gfx_rgb(230, 238, 255), gfx_rgb(22, 30, 50));
        draw_label(x + 36, footer_y + 8 + FONT_HEIGHT + 2,
                   users_current_is_admin() ? "admin" : "user",
                   gfx_rgb(120, 140, 180), gfx_rgb(22, 30, 50));
    }
    /* power button */
    gfx_fill_rect(x + w - 38, footer_y + (START_FOOTER_H - 22) / 2, 28, 22, gfx_rgb(40, 50, 75));
    draw_label(x + w - 32, footer_y + (START_FOOTER_H - FONT_HEIGHT) / 2, "O|",
               gfx_rgb(200, 215, 240), gfx_rgb(40, 50, 75));
}

static void do_shutdown(void) {
    cpu_cli();
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) cpu_hlt();
}

static void do_reboot(void) {
    cpu_cli();
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02u)) break;
    }
    outb(0x64, 0xFE);
    for (;;) cpu_hlt();
}

static void draw_terminal_icon(int x, int y) {
    gfx_fill_rect(x + 2, y + 4, 28, 20, COL_ICON_PANEL);
    gfx_fill_rect(x + 2, y + 4, 28, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 2, y + 22, 28, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 2, y + 4, 2, 20, COL_ICON_STROKE);
    gfx_fill_rect(x + 28, y + 4, 2, 20, COL_ICON_STROKE);
    draw_label(x + 7, y + 7, ">_", COL_ICON_TILE_ACTIVE, COL_ICON_PANEL);
}

static void draw_files_icon(int x, int y) {
    gfx_fill_rect(x + 4, y + 8, 10, 6, COL_ICON_ACCENT);
    gfx_fill_rect(x + 4, y + 12, 24, 14, COL_ICON_ACCENT);
    gfx_fill_rect(x + 4, y + 8, 24, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 4, y + 24, 24, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 4, y + 10, 2, 16, COL_ICON_STROKE);
    gfx_fill_rect(x + 26, y + 10, 2, 16, COL_ICON_STROKE);
}

static void draw_editor_icon(int x, int y) {
    gfx_fill_rect(x + 6, y + 5, 18, 22, COL_ICON_TILE_ACTIVE);
    gfx_fill_rect(x + 6, y + 5, 18, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 6, y + 25, 18, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 6, y + 5, 2, 22, COL_ICON_STROKE);
    gfx_fill_rect(x + 22, y + 5, 2, 22, COL_ICON_STROKE);
    gfx_fill_rect(x + 10, y + 10, 10, 2, COL_ICON_ACCENT);
    gfx_fill_rect(x + 10, y + 14, 10, 2, COL_ICON_ACCENT);
    gfx_fill_rect(x + 10, y + 18, 7, 2, COL_ICON_ACCENT);
    gfx_fill_rect(x + 18, y + 18, 7, 3, COL_ICON_STEM);
    gfx_fill_rect(x + 24, y + 15, 4, 4, COL_ICON_LEAF);
}

static void draw_osinfo_icon(int x, int y) {
    gfx_fill_rect(x + 6, y + 6, 20, 20, COL_ICON_TILE_ACTIVE);
    gfx_fill_rect(x + 6, y + 6, 20, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 6, y + 24, 20, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 6, y + 6, 2, 20, COL_ICON_STROKE);
    gfx_fill_rect(x + 24, y + 6, 2, 20, COL_ICON_STROKE);
    gfx_fill_rect(x + 11, y + 10, 10, 3, COL_ICON_ACCENT);
    gfx_fill_rect(x + 14, y + 15, 4, 7, COL_ICON_ACCENT);
    gfx_fill_rect(x + 14, y + 23, 4, 2, COL_ICON_ACCENT);
}

static void draw_settings_icon(int x, int y) {
    gfx_fill_rect(x + 12, y + 6, 8, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 12, y + 22, 8, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 6, y + 12, 4, 8, COL_ICON_STROKE);
    gfx_fill_rect(x + 22, y + 12, 4, 8, COL_ICON_STROKE);
    gfx_fill_rect(x + 8, y + 8, 4, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 20, y + 8, 4, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 8, y + 20, 4, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 20, y + 20, 4, 4, COL_ICON_STROKE);
    gfx_fill_rect(x + 10, y + 10, 12, 12, COL_ICON_ACCENT);
    gfx_fill_rect(x + 13, y + 13, 6, 6, COL_ICON_TILE_ACTIVE);
}

static void draw_taskmgr_icon(int x, int y) {
    gfx_fill_rect(x + 4, y + 5, 24, 18, COL_ICON_TILE_ACTIVE);
    gfx_fill_rect(x + 4, y + 5, 24, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 4, y + 21, 24, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 4, y + 5, 2, 18, COL_ICON_STROKE);
    gfx_fill_rect(x + 26, y + 5, 2, 18, COL_ICON_STROKE);
    gfx_fill_rect(x + 8, y + 16, 4, 4, COL_ICON_ACCENT);
    gfx_fill_rect(x + 14, y + 12, 4, 8, COL_ICON_ACCENT);
    gfx_fill_rect(x + 20, y + 9, 4, 11, COL_ICON_ACCENT);
}

static void draw_snake_icon(int x, int y) {
    gfx_fill_rect(x + 13, y + 4, 4, 5, COL_ICON_STEM);
    gfx_fill_rect(x + 17, y + 5, 7, 3, COL_ICON_LEAF);
    gfx_fill_rect(x + 20, y + 7, 4, 2, COL_ICON_LEAF);

    gfx_fill_rect(x + 9, y + 9, 14, 2, COL_ICON_APPLE);
    gfx_fill_rect(x + 7, y + 11, 18, 2, COL_ICON_APPLE);
    gfx_fill_rect(x + 6, y + 13, 20, 8, COL_ICON_APPLE);
    gfx_fill_rect(x + 7, y + 21, 18, 3, COL_ICON_APPLE);
    gfx_fill_rect(x + 9, y + 24, 14, 2, COL_ICON_APPLE);

    gfx_fill_rect(x + 10, y + 12, 6, 3, COL_ICON_APPLE_GLOW);
    gfx_fill_rect(x + 9, y + 15, 4, 3, COL_ICON_APPLE_GLOW);
    gfx_fill_rect(x + 21, y + 15, 2, 2, COL_ICON_STROKE);
    gfx_fill_rect(x + 19, y + 18, 2, 2, COL_ICON_STROKE);
}

static void draw_app_icon(gui_icon_kind_t kind, int x, int y) {
    if (kind == GUI_ICON_TERMINAL) {
        draw_terminal_icon(x, y);
    } else if (kind == GUI_ICON_FILES) {
        draw_files_icon(x, y);
    } else if (kind == GUI_ICON_EDITOR) {
        draw_editor_icon(x, y);
    } else if (kind == GUI_ICON_OSINFO) {
        draw_osinfo_icon(x, y);
    } else if (kind == GUI_ICON_SETTINGS) {
        draw_settings_icon(x, y);
    } else if (kind == GUI_ICON_SNAKE) {
        draw_snake_icon(x, y);
    } else if (kind == GUI_ICON_NOTES) {
        /* Notepad-style: yellow page with lines */
        gfx_fill_rect(x + 4, y + 2, 22, 28, gfx_rgb(255, 248, 200));
        gfx_fill_rect(x + 4, y + 2, 22, 1, gfx_rgb(200, 170, 60));
        gfx_fill_rect(x + 7, y + 8,  14, 2, gfx_rgb(180, 160, 80));
        gfx_fill_rect(x + 7, y + 13, 14, 2, gfx_rgb(180, 160, 80));
        gfx_fill_rect(x + 7, y + 18, 14, 2, gfx_rgb(180, 160, 80));
        gfx_fill_rect(x + 7, y + 23, 10, 2, gfx_rgb(180, 160, 80));
    } else if (kind == GUI_ICON_STORE) {
        /* Shopping bag outline */
        gfx_fill_rect(x + 6, y + 10, 20, 18, gfx_rgb(16, 185, 129));
        gfx_fill_rect(x + 10, y + 6, 12, 6, gfx_rgb(0, 0, 0));
        gfx_fill_rect(x + 11, y + 7, 10, 4, gfx_rgb(16, 185, 129));
        gfx_fill_rect(x + 11, y + 15, 10, 2, gfx_rgb(255, 255, 255));
        gfx_fill_rect(x + 11, y + 20, 10, 2, gfx_rgb(255, 255, 255));
    } else {
        draw_taskmgr_icon(x, y);
    }
}

static void draw_desktop_icons(void) {
    int count = desktop_icon_count();

    for (int visible = 0; visible < count; visible++) {
        gui_rect_t slot;
        char label[18];
        const gui_app_t *app;
        uint32_t plate_bg;
        uint32_t label_fg;
        int tile_x;
        int tile_y;
        int text_x;
        int app_idx = desktop_app_index(visible);
        int selected = (visible == g_desktop_sel);
        int active = selected && g_focus < 0 && g_shell_focus == SHELL_FOCUS_DESKTOP;

        if (app_idx < 0 || !desktop_slot_rect(visible, &slot)) continue;
        app = &g_apps[app_idx];
        clip_title(label, sizeof(label),
                   app->desktop_label ? app->desktop_label : app->label, 10);

        if (selected) {
            plate_bg = active ? COL_ICON_SEL_ACTIVE : COL_ICON_SEL;
            gfx_fill_rect(slot.x + 4, slot.y + 2, slot.w - 8, slot.h - 6, plate_bg);
        }

        tile_x = slot.x + (slot.w - DESKTOP_ICON_SIZE) / 2;
        tile_y = slot.y + 8;
        gfx_fill_rect(tile_x, tile_y, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE,
                      selected ? COL_ICON_TILE_ACTIVE : COL_ICON_TILE);

        draw_app_icon(app->icon_kind, tile_x, tile_y);

        label_fg = active ? COL_ICON_TILE_ACTIVE : COL_DESKTOP_TXT;
        text_x = slot.x + (slot.w - text_width(label)) / 2;
        if (text_x < slot.x + 2) text_x = slot.x + 2;
        draw_label(text_x, slot.y + 50, label, label_fg,
                   selected ? plate_bg : COL_DESKTOP);
    }
}

static void rtc_read_time(int *h, int *m) {
    uint8_t hv, mv;
    outb(0x70, 0x04); hv = inb(0x71);
    outb(0x70, 0x02); mv = inb(0x71);
    *h = (hv & 0xF) + ((hv >> 4) & 0xF) * 10;
    *m = (mv & 0xF) + ((mv >> 4) & 0xF) * 10;
}

static uint32_t icon_badge_col(int k) {
    if (k == GUI_ICON_TERMINAL) return COL_ICON_PANEL;
    if (k == GUI_ICON_FILES)    return COL_ICON_ACCENT;
    if (k == GUI_ICON_EDITOR)   return COL_ICON_TILE_ACTIVE;
    if (k == GUI_ICON_OSINFO)   return COL_ICON_ACCENT;
    if (k == GUI_ICON_SETTINGS) return COL_ICON_ACCENT;
    if (k == GUI_ICON_TASKMGR)  return COL_ICON_TILE_ACTIVE;
    if (k == GUI_ICON_NOTES)    return gfx_rgb(252, 211, 77);
    if (k == GUI_ICON_STORE)    return gfx_rgb(16, 185, 129);
    if (k == GUI_ICON_CALC)     return gfx_rgb(234, 88, 12);
    if (k == GUI_ICON_BROWSER)  return gfx_rgb(37, 99, 235);
    if (k == GUI_ICON_AXDOCS)    return gfx_rgb(100, 149, 237);
    if (k == GUI_ICON_AXSTUDIO)  return gfx_rgb(50, 110, 220);
    return COL_ICON_APPLE;
}

static void draw_taskbar(void) {
    int sw = gfx_width();
    int sh = gfx_height();
    int x = START_BUTTON_X + START_BUTTON_W + 10;
    int start_hot = g_start_open || (g_focus < 0 && g_shell_focus == SHELL_FOCUS_START);

    gfx_fill_rect(0, sh - TASKBAR_HEIGHT, sw, TASKBAR_HEIGHT, COL_TASKBAR);
    gfx_fill_rect(0, sh - TASKBAR_HEIGHT, sw, 1, COL_TASKBAR_TOP);

    gfx_fill_rect(START_BUTTON_X, sh - TASKBAR_HEIGHT + (TASKBAR_HEIGHT - 20) / 2,
                  START_BUTTON_W, 20, start_hot ? COL_START_OPEN : COL_START_BG);
    draw_label(START_BUTTON_X + 12, sh - TASKBAR_HEIGHT + (TASKBAR_HEIGHT - FONT_HEIGHT) / 2, "Start",
               COL_TASKBAR_TXT, start_hot ? COL_START_OPEN : COL_START_BG);

    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        g_taskbar_slots[i].x = 0;
        g_taskbar_slots[i].w = 0;
        g_taskbar_slots[i].win_id = -1;
    }

    for (int i = 0; i < g_zcount; i++) {
        int win_id = g_zorder[i];
        int bw = 110;
        int btn_h = TASKBAR_HEIGHT - 8;
        int btn_y = sh - TASKBAR_HEIGHT + (TASKBAR_HEIGHT - btn_h) / 2;
        char title[18];
        int active = (win_id == g_focus) ||
                     (g_focus < 0 && g_shell_focus == SHELL_FOCUS_TASKBAR && i == g_taskbar_sel);
        uint32_t btn_col = g_windows[win_id].minimized ? COL_WIN_MINIMIZED :
                           (active ? COL_BTN_ACTIVE : COL_BTN_BG);
        if (x + bw >= sw - 90) break;
        g_taskbar_slots[i].x = x;
        g_taskbar_slots[i].w = bw;
        g_taskbar_slots[i].win_id = win_id;
        gfx_fill_rect(x, btn_y, bw, btn_h, btn_col);
        clip_title(title, sizeof(title), gui_window_title(win_id), 12);
        draw_label(x + 6, btn_y + (btn_h - FONT_HEIGHT) / 2, title, COL_TASKBAR_TXT, btn_col);
        x += bw + 6;
    }

    {
        int tb_y = sh - TASKBAR_HEIGHT;
        int rh, rm;
        rtc_read_time(&rh, &rm);
        char cl[6] = { (char)('0'+rh/10), (char)('0'+rh%10), ':', (char)('0'+rm/10), (char)('0'+rm%10), '\0' };
        const char *un = auth_session_active() ? auth_session_username() : users_current();
        int cx = sw - 5*FONT_WIDTH - 8;
        int ux = cx - (int)str_len(un)*FONT_WIDTH - 16;
        draw_label(ux, tb_y+8, un, COL_TASKBAR_DIM, COL_TASKBAR);
        draw_label(cx, tb_y+8, cl, COL_TASKBAR_TXT, COL_TASKBAR);
    }
}

static void draw_window_frame(gui_window_t *w) {
    int x = w->frame.x;
    int y = w->frame.y;
    int fw = w->frame.w;
    int fh = w->frame.h;
    uint32_t title_bg;
    uint32_t outer;
    int title_x;

    outer = w->focused ? gfx_rgb(59, 130, 246) : COL_WIN_BORDER;
    gfx_fill_rect(x, y, fw, fh, outer);
    gfx_fill_rect(x + 2, y + TITLE_BAR_HEIGHT, fw - 4, fh - TITLE_BAR_HEIGHT - 2, COL_WIN_BG);

    title_bg = w->focused ? COL_TITLE_FOCUS : COL_TITLE_BLUR;
    gfx_fill_rect(x + 2, y + 2, fw - 4, TITLE_BAR_HEIGHT - 2, title_bg);

    title_x = x + 8;
    if (w->icon_kind >= 0) {
        gfx_fill_rect(x + 4, y + (TITLE_BAR_HEIGHT - 10) / 2, 10, 10, icon_badge_col(w->icon_kind));
        title_x = x + 18;
    }
    draw_label(title_x, y + (TITLE_BAR_HEIGHT - FONT_HEIGHT) / 2, w->title, COL_TITLE_TXT, title_bg);

    /* minimize button */
    gfx_fill_rect(x + fw - 46, y + 3, 18, TITLE_BAR_HEIGHT - 6, COL_MINIMIZE_BG);
    draw_label(x + fw - 42, y + (TITLE_BAR_HEIGHT - FONT_HEIGHT) / 2, "-", COL_CLOSE_TXT, COL_MINIMIZE_BG);

    /* close button */
    gfx_fill_rect(x + fw - 24, y + 3, 18, TITLE_BAR_HEIGHT - 6, COL_CLOSE_BG);
    draw_label(x + fw - 20, y + (TITLE_BAR_HEIGHT - FONT_HEIGHT) / 2, "X", COL_CLOSE_TXT, COL_CLOSE_BG);

    /* resize dots bottom-right */
    gfx_fill_rect(x + fw - 3, y + fh - 3, 2, 2, gfx_rgb(130, 140, 160));
    gfx_fill_rect(x + fw - 3, y + fh - 7, 2, 2, gfx_rgb(130, 140, 160));
    gfx_fill_rect(x + fw - 7, y + fh - 3, 2, 2, gfx_rgb(130, 140, 160));

    w->content.x = x + 2;
    w->content.y = y + TITLE_BAR_HEIGHT;
    w->content.w = fw - 4;
    w->content.h = fh - TITLE_BAR_HEIGHT - 2;
}

static void draw_window(int idx) {
    gui_window_t *w = &g_windows[idx];
    if (!w->active) return;
    draw_window_frame(w);
    if (w->on_paint) {
        w->on_paint(idx);
    }
}

static void clamp_shell_state(void) {
    int icon_count = desktop_icon_count();
    int start_count = start_menu_desktop_count();
    if (start_count < 1) start_count = 1;

    if (g_start_sel < 0) g_start_sel = 0;
    if (g_start_sel >= start_count) g_start_sel = start_count - 1;
    if (g_start_sel < 0) g_start_sel = 0;

    if (icon_count <= 0) {
        g_desktop_sel = -1;
    } else {
        if (g_desktop_sel < 0) g_desktop_sel = 0;
        if (g_desktop_sel >= icon_count) g_desktop_sel = icon_count - 1;
    }

    if (g_zcount <= 0) {
        g_taskbar_sel = 0;
    } else {
        if (g_taskbar_sel < 0) g_taskbar_sel = 0;
        if (g_taskbar_sel >= g_zcount) g_taskbar_sel = g_zcount - 1;
    }

    if (g_start_open) {
        g_shell_focus = SHELL_FOCUS_START;
    } else if (g_focus >= 0 && g_windows[g_focus].active) {
        g_shell_focus = SHELL_FOCUS_WINDOW;
    } else if (g_shell_focus == SHELL_FOCUS_WINDOW) {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
    }

    if (g_shell_focus == SHELL_FOCUS_TASKBAR && g_zcount == 0) {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
    }
}

static int run_idle_ticks(uint32_t now) {
    int dirty = 0;

    for (int i = 0; i < g_zcount; i++) {
        int win_id = g_zorder[i];
        gui_window_t *w = &g_windows[win_id];

        if (!w->active || !w->on_tick) continue;
        if (w->on_tick(win_id, now)) dirty = 1;
    }

    /* Poll USB HID devices (USB mouse on real hardware) */
    usb_poll();

    /* Poll network stack */
    net_poll();

    return dirty;
}

void gui_repaint(void) {
    int sw = gfx_width();
    int sh = gfx_height();
    clamp_shell_state();
    mouse_set_bounds(sw, sh);
    fill_desktop_background();
    draw_desktop_icons();

    for (int i = 0; i < g_zcount; i++) {
        if (g_windows[g_zorder[i]].minimized) continue;
        draw_window(g_zorder[i]);
    }

    draw_taskbar();
    if (g_start_open) {
        draw_start_menu();
    }

    /* Context menu (painted above everything) */
    context_menu_paint();

    /* App search overlay */
    if (g_search_active) {
        int sw = (int)gfx_width();
        int sh = (int)gfx_height();
        int ow = 420, oh = 320;
        int ox = (sw - ow) / 2, oy = (sh - oh) / 3;
        /* Dim background */
        for (int ry = oy; ry < oy + oh; ry++)
            gfx_fill_rect(ox, ry, ow, 1, gfx_rgb(20, 26, 44));
        /* Border */
        gfx_fill_rect(ox, oy, ow, 1, gfx_rgb(60, 80, 130));
        gfx_fill_rect(ox, oy + oh - 1, ow, 1, gfx_rgb(60, 80, 130));
        gfx_fill_rect(ox, oy, 1, oh, gfx_rgb(60, 80, 130));
        gfx_fill_rect(ox + ow - 1, oy, 1, oh, gfx_rgb(60, 80, 130));
        /* Title */
        gfx_draw_string(ox + 12, oy + 8, "Search apps (Esc to close)",
                        gfx_rgb(160, 180, 220), gfx_rgb(20, 26, 44));
        /* Search input box */
        int bx = ox + 10, by = oy + 26, bw = ow - 20, bh = 28;
        gfx_fill_rect(bx, by, bw, bh, gfx_rgb(255, 255, 255));
        gfx_fill_rect(bx, by, bw, 1, gfx_rgb(80, 120, 200));
        gfx_draw_string(bx + 6, by + (bh - FONT_HEIGHT) / 2,
                        g_search_buf, gfx_rgb(20, 30, 60), gfx_rgb(255, 255, 255));
        /* Cursor in search box */
        gfx_fill_rect(bx + 6 + g_search_len * FONT_WIDTH,
                      by + 4, 2, bh - 8, gfx_rgb(38, 99, 235));
        /* Result list */
        int n = gui_app_count();
        int ry2 = by + bh + 8;
        int row = 0;
        int matched_row = 0;
        for (int i = 0; i < n && ry2 + 26 <= oy + oh - 8; i++) {
            const gui_app_t *app = gui_app_at(i);
            int match = 1;
            if (g_search_len > 0) {
                /* Case-insensitive substring match */
                const char *s = app->label;
                const char *q = g_search_buf;
                int qi = 0, qi_end = g_search_len;
                match = 0;
                while (*s && !match) {
                    int j = 0;
                    const char *p = s;
                    while (j < qi_end && *p) {
                        char sc = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
                        char qc = (q[j] >= 'A' && q[j] <= 'Z') ? (char)(q[j]+32) : q[j];
                        if (sc != qc) break;
                        j++; p++;
                    }
                    if (j == qi_end) match = 1;
                    s++;
                }
                (void)qi;
            }
            if (!match) continue;
            uint32_t row_bg = (matched_row == g_search_sel)
                            ? gfx_rgb(38, 99, 235) : gfx_rgb(30, 38, 60);
            uint32_t row_fg = gfx_rgb(230, 240, 255);
            gfx_fill_rect(ox + 2, ry2, ow - 4, 24, row_bg);
            gfx_fill_rect(ox + 14, ry2 + 4, 10, 10,
                          icon_badge_col((int)app->icon_kind));
            gfx_draw_string(ox + 30, ry2 + (24 - FONT_HEIGHT) / 2,
                            app->label, row_fg, row_bg);
            ry2 += 26;
            matched_row++;
            row++;
        }
        (void)row;
    }

    gfx_swap();
    g_cursor_drawn = 0;
    present_cursor_overlay(mouse_x(), mouse_y());
}

static void clear_focus(void) {
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        g_windows[i].focused = 0;
    }
    g_focus = -1;
    if (!g_start_open) {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
    }
}

static void bring_to_front(int idx) {
    int pos = -1;
    for (int i = 0; i < g_zcount; i++) {
        if (g_zorder[i] == idx) {
            pos = i;
            break;
        }
    }
    if (pos < 0) return;
    for (int i = pos; i < g_zcount - 1; i++) {
        g_zorder[i] = g_zorder[i + 1];
    }
    g_zorder[g_zcount - 1] = idx;
    clear_focus();
    g_windows[idx].focused = 1;
    g_focus = idx;
    g_taskbar_sel = g_zcount - 1;
    g_shell_focus = SHELL_FOCUS_WINDOW;
}

static int hit_test_window(int mx, int my) {
    for (int i = g_zcount - 1; i >= 0; i--) {
        gui_window_t *w = &g_windows[g_zorder[i]];
        if (!w->active || w->minimized) continue;
        if (mx >= w->frame.x && mx < w->frame.x + w->frame.w &&
            my >= w->frame.y && my < w->frame.y + w->frame.h) {
            return g_zorder[i];
        }
    }
    return -1;
}

static int in_title_bar(gui_window_t *w, int mx, int my) {
    return mx >= w->frame.x && mx < w->frame.x + w->frame.w &&
           my >= w->frame.y && my < w->frame.y + TITLE_BAR_HEIGHT;
}

static int in_close_button(gui_window_t *w, int mx, int my) {
    int cx = w->frame.x + w->frame.w - 24;
    int cy = w->frame.y + 3;
    return mx >= cx && mx < cx + 18 && my >= cy && my < cy + TITLE_BAR_HEIGHT - 6;
}

static int in_minimize_button(gui_window_t *w, int mx, int my) {
    int bx = w->frame.x + w->frame.w - 46;
    int by = w->frame.y + 3;
    return mx >= bx && mx < bx + 18 && my >= by && my < by + TITLE_BAR_HEIGHT - 6;
}

static int in_resize_handle(gui_window_t *w, int mx, int my) {
    int rx = w->frame.x + w->frame.w - RESIZE_HANDLE;
    int ry = w->frame.y + w->frame.h - RESIZE_HANDLE;
    return mx >= rx && mx < w->frame.x + w->frame.w &&
           my >= ry && my < w->frame.y + w->frame.h;
}

static int hit_test_taskbar_button(int mx, int my) {
    int sh = gfx_height();
    if (my < sh - TASKBAR_HEIGHT + 4 || my >= sh - 4) return -1;
    for (int i = 0; i < g_zcount; i++) {
        if (g_taskbar_slots[i].w > 0 &&
            mx >= g_taskbar_slots[i].x &&
            mx < g_taskbar_slots[i].x + g_taskbar_slots[i].w) {
            return g_taskbar_slots[i].win_id;
        }
    }
    return -1;
}

static int hit_test_start_button(int mx, int my) {
    int sh = gfx_height();
    return mx >= START_BUTTON_X && mx < START_BUTTON_X + START_BUTTON_W &&
           my >= sh - TASKBAR_HEIGHT + 4 && my < sh - 4;
}

/* Returns visible desktop-app index (0-based) if click is in app grid, -1 otherwise */
static int hit_test_start_app_cell(int mx, int my) {
    int x      = start_menu_x();
    int y      = start_menu_y();
    int grid_y = y + START_HDR_H;
    int n      = start_menu_desktop_count();
    int rows   = start_menu_grid_rows();
    if (!g_start_open) return -1;
    if (mx < x + 1 || mx >= x + 1 + START_APP_COLS * START_CELL_W) return -1;
    if (my < grid_y || my >= grid_y + rows * START_CELL_H) return -1;
    {
        int col = (mx - (x + 1)) / START_CELL_W;
        int row = (my - grid_y)  / START_CELL_H;
        int vi  = row * START_APP_COLS + col;
        if (vi >= 0 && vi < n) return vi;
    }
    return -1;
}

/* Check if click hits the user area in footer (left 60%) */
static int hit_test_start_user_area(int mx, int my) {
    int x        = start_menu_x();
    int y        = start_menu_y();
    int h        = start_menu_h();
    int footer_y = y + h - START_FOOTER_H;
    if (!g_start_open) return 0;
    return mx >= x + 1 && mx < x + (START_MENU_W * 3 / 5) &&
           my >= footer_y && my < footer_y + START_FOOTER_H;
}

/* Check if click hits the power button in footer (right area) */
static int hit_test_start_power_btn(int mx, int my) {
    int x        = start_menu_x();
    int y        = start_menu_y();
    int h        = start_menu_h();
    int footer_y = y + h - START_FOOTER_H;
    if (!g_start_open) return 0;
    return mx >= x + START_MENU_W - 42 && mx < x + START_MENU_W - 2 &&
           my >= footer_y && my < footer_y + START_FOOTER_H;
}

static int hit_test_desktop_icon(int mx, int my) {
    int count = desktop_icon_count();
    for (int i = 0; i < count; i++) {
        gui_rect_t slot;
        if (!desktop_slot_rect(i, &slot)) continue;
        if (mx >= slot.x && mx < slot.x + slot.w &&
            my >= slot.y && my < slot.y + slot.h) {
            return i;
        }
    }
    return -1;
}

void gui_set_desktop_color(uint32_t color) {
    g_desktop_color = color;
}

uint32_t gui_get_desktop_color(void) {
    return g_desktop_color;
}

void gui_init(void) {
    g_desktop_color = gfx_rgb(12, 74, 110);
    mem_set(g_windows, 0, sizeof(g_windows));
    mem_set(g_taskbar_slots, 0, sizeof(g_taskbar_slots));
    g_zcount = 0;
    g_focus = -1;
    g_drag_win = -1;
    g_resize_win = -1;
    g_start_open = 0;
    g_start_sel = 0;
    g_taskbar_sel = 0;
    g_desktop_sel = desktop_icon_count() > 0 ? 0 : -1;
    g_last_icon_click = -1;
    g_last_icon_click_ticks = 0;
    g_shell_focus = SHELL_FOCUS_DESKTOP;
    g_cursor_drawn = 0;
    g_cursor_x = 0;
    g_cursor_y = 0;
    g_logout_requested = 0;
}

int gui_window_create(const char *title, int x, int y, int w, int h) {
    fit_window_frame(&x, &y, &w, &h);
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (!g_windows[i].active) {
            gui_window_t *win = &g_windows[i];
            win->active = 1;
            win->focused = 0;
            win->dragging = 0;
            win->minimized = 0;
            win->resizing = 0;
            win->on_paint = 0;
            win->on_tick = 0;
            win->on_key = 0;
            win->on_mouse = 0;
            win->on_close = 0;
            win->state = 0;
            win->icon_kind = -1;
            str_copy(win->title, title, GUI_TITLE_MAX);
            win->frame.x = x;
            win->frame.y = y;
            win->frame.w = w;
            win->frame.h = h;
            g_zorder[g_zcount++] = i;
            bring_to_front(i);
            clamp_shell_state();
            return i;
        }
    }
    return -1;
}

void gui_window_close(int id) {
    int pos = -1;
    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    if (g_windows[id].on_close) {
        g_windows[id].on_close(id);
    }
    g_windows[id].active = 0;
    for (int i = 0; i < g_zcount; i++) {
        if (g_zorder[i] == id) {
            pos = i;
            break;
        }
    }
    if (pos >= 0) {
        for (int i = pos; i < g_zcount - 1; i++) {
            g_zorder[i] = g_zorder[i + 1];
        }
        g_zcount--;
    }
    if (g_drag_win == id) g_drag_win = -1;
    if (g_resize_win == id) g_resize_win = -1;
    clear_focus();
    if (g_zcount > 0) {
        g_windows[g_zorder[g_zcount - 1]].focused = 1;
        g_focus = g_zorder[g_zcount - 1];
        g_taskbar_sel = g_zcount - 1;
        g_shell_focus = SHELL_FOCUS_WINDOW;
    } else {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
    }
    clamp_shell_state();
}

void gui_window_set_title(int id, const char *title) {
    if (id < 0 || id >= GUI_MAX_WINDOWS) return;
    str_copy(g_windows[id].title, title, GUI_TITLE_MAX);
}

void gui_window_focus(int id) {
    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    bring_to_front(id);
}

int gui_window_active(int id) {
    return id >= 0 && id < GUI_MAX_WINDOWS && g_windows[id].active;
}

const char *gui_window_title(int id) {
    if (id < 0 || id >= GUI_MAX_WINDOWS) return "";
    return g_windows[id].title;
}

int gui_window_count(void) {
    return g_zcount;
}

int gui_window_focused(void) {
    return g_focus;
}

int gui_window_id_at(int index) {
    if (index < 0 || index >= g_zcount) return -1;
    return g_zorder[index];
}

gui_rect_t gui_window_content(int id) {
    gui_rect_t rect = {0, 0, 0, 0};
    if (id < 0 || id >= GUI_MAX_WINDOWS) return rect;
    return g_windows[id].content;
}

gui_window_t *gui_get_window(int id) {
    if (id < 0 || id >= GUI_MAX_WINDOWS) return 0;
    return &g_windows[id];
}

static void close_all_windows(void) {
    while (g_zcount > 0) {
        gui_window_close(g_zorder[g_zcount - 1]);
    }
}

/* Context-menu callbacks for power/user menus */
static void ctx_action_shutdown(void *u) { (void)u; perform_session_action(SESSION_ACTION_SHUTDOWN); }
static void ctx_action_reboot(void *u)   { (void)u; perform_session_action(SESSION_ACTION_REBOOT);   }
static void ctx_action_reboot_aswd(void *u) { (void)u; perform_session_action(SESSION_ACTION_REBOOT_ASWD); }
static void ctx_action_logout(void *u)   { (void)u; perform_session_action(SESSION_ACTION_LOGOUT);   }
static void ctx_action_add_user(void *u) { (void)u; perform_session_action(SESSION_ACTION_ADD_USER); }

static void perform_session_action(session_action_t action) {
    if (action == SESSION_ACTION_ADD_USER) {
        g_start_open = 0;
        control_panel_open_users();
        return;
    }
    if (action == SESSION_ACTION_DEV_TOOLS) {
        g_start_open = 0;
        dev_tools_launch();
        return;
    }
    if (action == SESSION_ACTION_SHUTDOWN) {
        g_start_open = 0;
        do_shutdown();
        return;
    }
    if (action == SESSION_ACTION_REBOOT || action == SESSION_ACTION_REBOOT_ASWD) {
        g_start_open = 0;
        do_reboot();
        return;
    }

    close_all_windows();
    g_start_open = 0;
    clear_focus();
    users_logout();
    auth_session_end();
    g_logout_requested = 1;
}

int gui_app_count(void) {
    return (int)(sizeof(g_apps) / sizeof(g_apps[0]));
}

const gui_app_t *gui_app_at(int index) {
    if (index < 0 || index >= gui_app_count()) return 0;
    return &g_apps[index];
}

void gui_launch_app(int index) {
    const gui_app_t *app = gui_app_at(index);
    if (!app || !app->launch) return;
    app->launch();
    clamp_shell_state();
}

static void desktop_launch_selected(void) {
    int app_idx = desktop_app_index(g_desktop_sel);
    if (app_idx < 0) return;
    gui_launch_app(app_idx);
    clamp_shell_state();
}

static void cycle_shell_focus(void) {
    if (g_focus >= 0) return;

    if (g_start_open) {
        g_start_open = 0;
        g_shell_focus = (g_zcount > 0) ? SHELL_FOCUS_TASKBAR : SHELL_FOCUS_DESKTOP;
        return;
    }

    if (g_shell_focus == SHELL_FOCUS_START) {
        g_shell_focus = (g_zcount > 0) ? SHELL_FOCUS_TASKBAR : SHELL_FOCUS_DESKTOP;
    } else if (g_shell_focus == SHELL_FOCUS_TASKBAR) {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
    } else {
        g_shell_focus = SHELL_FOCUS_START;
    }
}

static int handle_desktop_key(char key) {
    int count = desktop_icon_count();
    int rows = desktop_rows_per_column();
    int next = g_desktop_sel;

    if (count <= 0 || g_desktop_sel < 0) return 0;

    if (key == KEY_UP) {
        if (next > 0) next--;
    } else if (key == KEY_DOWN) {
        if (next + 1 < count) next++;
    } else if (key == KEY_LEFT) {
        if (next - rows >= 0) next -= rows;
    } else if (key == KEY_RIGHT) {
        if (next + rows < count) next += rows;
    } else if (key == '\r' || key == '\n') {
        desktop_launch_selected();
        return 1;
    } else {
        return 0;
    }

    g_desktop_sel = next;
    return 1;
}

static void start_menu_activate_selection(void) {
    int ai = start_desktop_app_index(g_start_sel);
    if (ai >= 0) {
        g_start_open = 0;
        gui_launch_app(ai);
    }
}

static int handle_start_zone_key(char key) {
    if (key == 0x1B) {
        g_start_open = 0;
        g_shell_focus = SHELL_FOCUS_DESKTOP;
        return 1;
    }

    if (key == '\r' || key == '\n' || is_down_key(key) || is_up_key(key)) {
        if (!g_start_open) {
            g_start_open = 1;
            if (is_up_key(key)) {
                int n = start_menu_desktop_count();
                if (n > 0) g_start_sel = n - 1;
            }
            return 1;
        }
    }

    return 0;
}

static int handle_taskbar_key(char key) {
    if (g_zcount <= 0) return 0;

    if (key == KEY_LEFT && g_taskbar_sel > 0) {
        g_taskbar_sel--;
        return 1;
    }
    if (key == KEY_RIGHT && g_taskbar_sel + 1 < g_zcount) {
        g_taskbar_sel++;
        return 1;
    }
    if (key == '\r' || key == '\n') {
        int id = gui_window_id_at(g_taskbar_sel);
        if (id >= 0) {
            bring_to_front(id);
        }
        return 1;
    }
    if (key == 0x1B) {
        g_shell_focus = SHELL_FOCUS_DESKTOP;
        return 1;
    }

    return 0;
}

/* Launch the nth matched app in the search results (0-based match index) */
static void search_launch_selection(void) {
    int n = gui_app_count();
    int matched = 0;
    for (int i = 0; i < n; i++) {
        const gui_app_t *app = gui_app_at(i);
        int match = 1;
        if (g_search_len > 0) {
            const char *s = app->label;
            const char *q = g_search_buf;
            int qi_end = g_search_len;
            match = 0;
            while (*s && !match) {
                int j = 0;
                const char *p = s;
                while (j < qi_end && *p) {
                    char sc = (*p >= 'A' && *p <= 'Z') ? (char)(*p+32) : *p;
                    char qc = (q[j] >= 'A' && q[j] <= 'Z') ? (char)(q[j]+32) : q[j];
                    if (sc != qc) break;
                    j++; p++;
                }
                if (j == qi_end) match = 1;
                s++;
            }
        }
        if (!match) continue;
        if (matched == g_search_sel) {
            g_search_active = 0;
            if (app->launch) app->launch();
            return;
        }
        matched++;
    }
}

static void handle_key_event(char key, int *dirty) {
    clamp_shell_state();

    /* Search overlay captures keys first */
    if (g_search_active) {
        if (key == 0x1B) {
            g_search_active = 0;
        } else if (key == '\r' || key == '\n') {
            search_launch_selection();
        } else if (key == KEY_UP) {
            if (g_search_sel > 0) g_search_sel--;
        } else if (key == KEY_DOWN) {
            g_search_sel++;
        } else if (key == '\b') {
            if (g_search_len > 0) { g_search_buf[--g_search_len] = '\0'; g_search_sel = 0; }
        } else if (key >= 0x20 && key < 0x7F &&
                   g_search_len < (int)(sizeof(g_search_buf) - 1)) {
            g_search_buf[g_search_len++] = key;
            g_search_buf[g_search_len]   = '\0';
            g_search_sel = 0;
        }
        *dirty = 1;
        return;
    }

    /* Dismiss context menu on Escape */
    if (key == 0x1B && context_menu_active()) {
        context_menu_dismiss();
        *dirty = 1;
        return;
    }

    /* Ctrl+Space → open search overlay */
    if (key == 0x00 || key == ' ') {
        /* keyboard driver sends 0x00 for Ctrl+Space on some layouts;
           also accept plain space when no window is focused */
        if (key == 0x00) {
            g_search_active = 1;
            g_search_buf[0] = '\0';
            g_search_len    = 0;
            g_search_sel    = 0;
            *dirty = 1;
            return;
        }
    }

    if (g_start_open) {
        int n = start_menu_desktop_count();
        if (key == '\t') {
            cycle_shell_focus();
            *dirty = 1;
            return;
        }
        if (key == KEY_LEFT) {
            if (g_start_sel > 0) g_start_sel--;
            *dirty = 1;
            return;
        }
        if (key == KEY_RIGHT) {
            if (g_start_sel + 1 < n) g_start_sel++;
            *dirty = 1;
            return;
        }
        if (is_up_key(key)) {
            if (g_start_sel - START_APP_COLS >= 0)
                g_start_sel -= START_APP_COLS;
            *dirty = 1;
            return;
        }
        if (is_down_key(key)) {
            if (g_start_sel + START_APP_COLS < n)
                g_start_sel += START_APP_COLS;
            *dirty = 1;
            return;
        }
        if (key == '\r' || key == '\n') {
            start_menu_activate_selection();
            clamp_shell_state();
            *dirty = 1;
            return;
        }
        if (key == 0x1B) {
            g_start_open = 0;
            g_shell_focus = (g_focus >= 0) ? SHELL_FOCUS_WINDOW : SHELL_FOCUS_START;
            *dirty = 1;
            return;
        }
    }

    if (g_focus >= 0 && g_windows[g_focus].on_key) {
        g_windows[g_focus].on_key(g_focus, key);
        clamp_shell_state();
        *dirty = 1;
        return;
    }

    if (key == '\t') {
        cycle_shell_focus();
        *dirty = 1;
        return;
    }

    if (g_shell_focus == SHELL_FOCUS_START) {
        if (handle_start_zone_key(key)) {
            *dirty = 1;
            return;
        }
    } else if (g_shell_focus == SHELL_FOCUS_TASKBAR) {
        if (handle_taskbar_key(key)) {
            *dirty = 1;
            return;
        }
    } else {
        if (handle_desktop_key(key)) {
            *dirty = 1;
            return;
        }
    }
}

static void activate_desktop_icon_click(int visible_idx) {
    uint32_t now = timer_get_ticks();
    g_desktop_sel = visible_idx;
    if (visible_idx == g_last_icon_click &&
        (uint32_t)(now - g_last_icon_click_ticks) <= DESKTOP_DBLCLICK_TICKS) {
        desktop_launch_selected();
    }
    g_last_icon_click = visible_idx;
    g_last_icon_click_ticks = now;
}

/* ---- Context menu action callbacks ---- */
static void ctx_open_terminal(void *u) { (void)u; shell_gui_launch(); }
static void ctx_open_notes(void *u)    { (void)u; notes_gui_launch(); }
static void ctx_open_files(void *u)    { (void)u; files_gui_launch(); }
static void ctx_open_search(void *u) {
    (void)u;
    g_search_active = 1;
    g_search_buf[0] = '\0';
    g_search_len    = 0;
    g_search_sel    = 0;
}
static void ctx_close_win(void *u) {
    int id = (int)(intptr_t)u;
    gui_window_close(id);
}
static void ctx_refresh(void *u) { (void)u; /* just repaints */ }

static const context_menu_item_t k_desktop_menu[] = {
    { "Open Terminal",  ctx_open_terminal, 0 },
    { "New Note",       ctx_open_notes,    0 },
    { "Open Files",     ctx_open_files,    0 },
    { "Search Apps",    ctx_open_search,   0 },
    { "Refresh",        ctx_refresh,       0 },
};

static void handle_pointer_event(const input_event_t *evt, int *dirty, int *cursor_only) {
    int mx = evt->pointer.x;
    int my = evt->pointer.y;

    if (evt->pointer.dx != 0 || evt->pointer.dy != 0 || evt->pointer.changed != 0) {
        *cursor_only = 1;
    }

    /* Let context menu handle pointer first if it's active */
    if (context_menu_active()) {
        int r = context_menu_handle_pointer(mx, my,
                    evt->pointer.pressed, evt->pointer.released);
        if (r != 0) { *dirty = 1; return; }
    }

    /* Right-click (released) → show context menu */
    if (evt->pointer.released & 0x02u) {
        int hit = hit_test_window(mx, my);
        if (hit >= 0) {
            gui_window_t *w = &g_windows[hit];
            if (in_title_bar(w, mx, my)) {
                static context_menu_item_t win_menu[1];
                win_menu[0].label    = "Close Window";
                win_menu[0].action   = ctx_close_win;
                win_menu[0].userdata = (void *)(intptr_t)hit;
                context_menu_show(mx, my, win_menu, 1);
                *dirty = 1;
                return;
            }
        } else {
            context_menu_show(mx, my, k_desktop_menu,
                              (int)(sizeof(k_desktop_menu) /
                                    sizeof(k_desktop_menu[0])));
            *dirty = 1;
            return;
        }
    }

    /* Resize drag motion */
    if (g_resize_win >= 0) {
        gui_window_t *rw = &g_windows[g_resize_win];
        if (evt->pointer.buttons & 0x01u) {
            int dx = mx - rw->resize_start_mx;
            int dy = my - rw->resize_start_my;
            int nw = rw->resize_orig_w + dx;
            int nh = rw->resize_orig_h + dy;
            if (nw < WIN_MIN_W) nw = WIN_MIN_W;
            if (nh < WIN_MIN_H) nh = WIN_MIN_H;
            if (rw->frame.x + nw > (int)gfx_width()) nw = (int)gfx_width() - rw->frame.x;
            if (rw->frame.y + nh > (int)gfx_height() - TASKBAR_HEIGHT)
                nh = (int)gfx_height() - TASKBAR_HEIGHT - rw->frame.y;
            rw->frame.w = nw;
            rw->frame.h = nh;
            *dirty = 1;
        } else {
            g_resize_win = -1;
            *dirty = 1;
        }
    }

    if (g_drag_win >= 0) {
        gui_window_t *dw = &g_windows[g_drag_win];
        if (evt->pointer.buttons & 0x01u) {
            dw->frame.x = mx - dw->drag_off_x;
            dw->frame.y = my - dw->drag_off_y;
            if (dw->frame.x < 0) dw->frame.x = 0;
            if (dw->frame.y < 0) dw->frame.y = 0;
            if (dw->frame.x + dw->frame.w > (int)gfx_width()) {
                dw->frame.x = (int)gfx_width() - dw->frame.w;
            }
            if (dw->frame.y + dw->frame.h > (int)gfx_height() - TASKBAR_HEIGHT) {
                dw->frame.y = (int)gfx_height() - TASKBAR_HEIGHT - dw->frame.h;
            }
            *dirty = 1;
        } else {
            g_windows[g_drag_win].dragging = 0;
            g_drag_win = -1;
            *dirty = 1;
        }
    }

    if (!(evt->pointer.pressed & 0x01u)) {
        return;
    }

    if (hit_test_start_button(mx, my)) {
        g_start_open = !g_start_open;
        if (g_start_open) {
            g_shell_focus = SHELL_FOCUS_START;
        } else {
            g_shell_focus = (g_focus >= 0) ? SHELL_FOCUS_WINDOW : SHELL_FOCUS_START;
        }
        *dirty = 1;
        return;
    }

    if (g_start_open) {
        int vi = hit_test_start_app_cell(mx, my);
        if (vi >= 0) {
            int ai = start_desktop_app_index(vi);
            g_start_open = 0;
            if (ai >= 0) gui_launch_app(ai);
            clamp_shell_state();
            *dirty = 1;
            return;
        }
        if (hit_test_start_power_btn(mx, my)) {
            static const context_menu_item_t power_items[3] = {
                { "Shutdown",     ctx_action_shutdown,     0 },
                { "Reboot",       ctx_action_reboot,       0 },
                { "Reboot AswdOS",ctx_action_reboot_aswd,  0 },
            };
            g_start_open = 0;
            context_menu_show(mx, my, power_items, 3);
            *dirty = 1;
            return;
        }
        if (hit_test_start_user_area(mx, my)) {
            static const context_menu_item_t user_items_admin[2] = {
                { "Log Out",  ctx_action_logout,   0 },
                { "Add User", ctx_action_add_user, 0 },
            };
            static const context_menu_item_t user_items_plain[1] = {
                { "Log Out",  ctx_action_logout,   0 },
            };
            g_start_open = 0;
            if (users_current_is_admin()) {
                context_menu_show(mx, my, user_items_admin, 2);
            } else {
                context_menu_show(mx, my, user_items_plain, 1);
            }
            *dirty = 1;
            return;
        }
        if (mx >= start_menu_x() && mx < start_menu_x() + START_MENU_W &&
            my >= start_menu_y() && my < start_menu_y() + start_menu_h()) {
            g_shell_focus = SHELL_FOCUS_START;
            *dirty = 1;
            return;
        }
    }

    {
        int taskbar_win = hit_test_taskbar_button(mx, my);
        if (taskbar_win >= 0) {
            g_start_open = 0;
            if (g_windows[taskbar_win].minimized) {
                g_windows[taskbar_win].minimized = 0;
            }
            bring_to_front(taskbar_win);
            *dirty = 1;
            return;
        }
    }

    {
        int hit = hit_test_window(mx, my);
        if (hit >= 0) {
            gui_window_t *w = &g_windows[hit];
            g_start_open = 0;
            bring_to_front(hit);
            if (in_close_button(w, mx, my)) {
                gui_window_close(hit);
            } else if (in_minimize_button(w, mx, my)) {
                w->minimized = 1;
                clear_focus();
                *dirty = 1;
            } else if (in_resize_handle(w, mx, my)) {
                g_resize_win = hit;
                w->resize_start_mx = mx;
                w->resize_start_my = my;
                w->resize_orig_w   = w->frame.w;
                w->resize_orig_h   = w->frame.h;
            } else if (in_title_bar(w, mx, my)) {
                w->dragging = 1;
                w->drag_off_x = mx - w->frame.x;
                w->drag_off_y = my - w->frame.y;
                g_drag_win = hit;
            } else if (w->on_mouse) {
                w->on_mouse(hit, mx - w->content.x, my - w->content.y, evt->pointer.buttons);
            }
            *dirty = 1;
            return;
        }
    }

    {
        int icon = hit_test_desktop_icon(mx, my);
        if (icon >= 0) {
            g_start_open = 0;
            clear_focus();
            g_shell_focus = SHELL_FOCUS_DESKTOP;
            activate_desktop_icon_click(icon);
            *dirty = 1;
            return;
        }
    }

    g_start_open = 0;
    clear_focus();
    g_shell_focus = SHELL_FOCUS_DESKTOP;
    *dirty = 1;
}

void gui_run(void) {
    input_event_t evt;
    gui_init();
    gui_repaint();

    for (;;) {
        int dirty = 0;
        int cursor_only = 0;

        if (!input_try_get_event(&evt)) {
            if (run_idle_ticks(timer_get_ticks())) {
                gui_repaint();
                continue;
            }
            __asm__ volatile("sti; hlt");
            continue;
        }

        if (evt.type == INPUT_EVENT_KEY) {
            handle_key_event(evt.key.ch, &dirty);
        } else if (evt.type == INPUT_EVENT_POINTER) {
            handle_pointer_event(&evt, &dirty, &cursor_only);
        }

        if (g_logout_requested) {
            g_logout_requested = 0;
            return;
        }

        if (dirty) {
            gui_repaint();
        } else if (cursor_only) {
            present_cursor_overlay(evt.pointer.x, evt.pointer.y);
        }
    }
}
