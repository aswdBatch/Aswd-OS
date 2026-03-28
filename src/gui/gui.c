#include "gui/gui.h"

#include <stdint.h>

#include "common/config.h"
#include "common/power.h"
#include "cpu/pic.h"
#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/icon.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "fs/vfs.h"
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
#include "gui/permission_gui.h"
#include "gui/settings_gui.h"
#include "gui/theme.h"
#include "gui/shell_gui.h"
#include "gui/snake_gui.h"
#include "gui/taskmgr.h"
#include "gui/work_gui.h"
#include "net/net.h"
#include "input/input.h"
#include "usb/usb.h"
#include "lib/string.h"
#include "auth/auth_store.h"
#include "users/users.h"
#include "cpu/ports.h"

#define GUI_RGB(r, g, b) ((((uint32_t)(r)) << 16) | (((uint32_t)(g)) << 8) | ((uint32_t)(b)))

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
#define START_APP_COLS   3

#define COL_MINIMIZE_BG  gfx_rgb(200, 170, 30)
#define COL_MAXIMIZE_BG  gfx_rgb(34, 139, 94)
#define COL_WIN_MINIMIZED gfx_rgb(38, 46, 62)

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

static gui_shell_metrics_t g_shell_metrics;

typedef struct {
    gui_background_theme_t id;
    const char *persist_id;
    const char *name;
    uint32_t desktop_top;
    uint32_t desktop_bottom;
    uint32_t band_a;
    uint32_t band_b;
    uint32_t glow;
    uint32_t taskbar_top;
    uint32_t taskbar_bottom;
    uint32_t accent_top;
    uint32_t accent_bottom;
    uint32_t accent_hot_top;
    uint32_t accent_hot_bottom;
    uint32_t inactive_top;
    uint32_t inactive_bottom;
    uint32_t auth_overlay;
} gui_background_style_t;

#define GUI_THEME_FILE "DESKTOP.CFG"

static const gui_background_style_t k_background_themes[GUI_BG_THEME_COUNT] = {
    { GUI_BG_THEME_MINT, "mint", "Mint",      GUI_RGB(22,  84,  76), GUI_RGB(11,  20,  30),
      GUI_RGB(44, 155, 118), GUI_RGB(154, 235, 196), GUI_RGB(92, 201, 148),
      GUI_RGB(26,  33,  41), GUI_RGB(12,  16,  22),
      GUI_RGB(58, 154, 122), GUI_RGB(26, 110,  88),
      GUI_RGB(88, 196, 160), GUI_RGB(31, 134, 103),
      GUI_RGB(115, 126, 141), GUI_RGB(70,  80,  95), GUI_RGB(5, 20, 18) },
    { GUI_BG_THEME_GLASS, "glass", "Blue Glass", GUI_RGB(18,  76, 132), GUI_RGB(10,  17,  30),
      GUI_RGB(40, 119, 212), GUI_RGB(175, 222, 255), GUI_RGB(96, 184, 255),
      GUI_RGB(27,  34,  48), GUI_RGB(14,  18,  29),
      GUI_RGB(56, 148, 245), GUI_RGB(27, 104, 188),
      GUI_RGB(96, 186, 255), GUI_RGB(37, 128, 228),
      GUI_RGB(120, 130, 150), GUI_RGB(79,  88, 108), GUI_RGB(6, 18, 32) },
    { GUI_BG_THEME_STUDIO, "studio", "Studio Dark", GUI_RGB(36,  44,  72), GUI_RGB(12,  14,  20),
      GUI_RGB(128, 80, 184), GUI_RGB(243, 112, 126), GUI_RGB(165, 116, 255),
      GUI_RGB(26,  27,  35), GUI_RGB(10,  11,  16),
      GUI_RGB(120, 93, 214), GUI_RGB(75,  58, 132),
      GUI_RGB(160, 124, 255), GUI_RGB(97,  74, 182),
      GUI_RGB(124, 122, 146), GUI_RGB(72,  71,  92), GUI_RGB(20, 12, 26) },
    { GUI_BG_THEME_SUNSET, "sunset", "Sunset", GUI_RGB(126,  60,  50), GUI_RGB(26,  12,  24),
      GUI_RGB(233, 142,  65), GUI_RGB(255, 208, 133), GUI_RGB(237, 109,  98),
      GUI_RGB(39,  28,  33), GUI_RGB(18,  12,  18),
      GUI_RGB(206, 96,  84), GUI_RGB(152, 60,  72),
      GUI_RGB(236, 143, 110), GUI_RGB(183, 72,  84),
      GUI_RGB(145, 120, 124), GUI_RGB(88,  66,  76), GUI_RGB(28, 12, 12) },
    { GUI_BG_THEME_OCEAN, "ocean", "Ocean",   GUI_RGB(10,  83,  95), GUI_RGB(6,  15,  24),
      GUI_RGB(18, 156, 166), GUI_RGB(160, 236, 226), GUI_RGB(88, 205, 205),
      GUI_RGB(20,  31,  38), GUI_RGB(8,  14,  18),
      GUI_RGB(19, 152, 171), GUI_RGB(10, 108, 125),
      GUI_RGB(53, 196, 212), GUI_RGB(14, 132, 146),
      GUI_RGB(111, 127, 136), GUI_RGB(63,  76,  84), GUI_RGB(4, 18, 20) },
    { GUI_BG_THEME_NEUTRAL, "neutral", "Geometric", GUI_RGB(70,  79,  92), GUI_RGB(13,  16,  22),
      GUI_RGB(160, 170, 184), GUI_RGB(230, 235, 244), GUI_RGB(150, 190, 220),
      GUI_RGB(30,  35,  43), GUI_RGB(14,  17,  23),
      GUI_RGB(79, 132, 194), GUI_RGB(43,  92, 145),
      GUI_RGB(113, 164, 226), GUI_RGB(59, 110, 174),
      GUI_RGB(127, 135, 149), GUI_RGB(79,  88, 102), GUI_RGB(10, 14, 18) },
};

static gui_background_theme_t g_background_theme = GUI_BG_THEME_MINT;
static int g_background_loaded = 0;

#define START_BUTTON_W        (g_shell_metrics.start_button_w)
#define START_MENU_W          (g_shell_metrics.start_menu_w)
#define START_HDR_H           (g_shell_metrics.start_header_h)
#define START_FOOTER_H        (g_shell_metrics.start_footer_h)
#define START_CELL_W          (g_shell_metrics.start_cell_w)
#define START_CELL_H          (g_shell_metrics.start_cell_h)
#define RESIZE_HANDLE         (g_shell_metrics.resize_handle)
#define WIN_MIN_W             (g_shell_metrics.window_min_w)
#define WIN_MIN_H             (g_shell_metrics.window_min_h)
#define DESKTOP_ICON_MARGIN_X (g_shell_metrics.desktop_margin_x)
#define DESKTOP_ICON_MARGIN_Y (g_shell_metrics.desktop_margin_y)
#define DESKTOP_ICON_SLOT_W   (g_shell_metrics.desktop_slot_w)
#define DESKTOP_ICON_SLOT_H   (g_shell_metrics.desktop_slot_h)
#define DESKTOP_ICON_GAP_X    (g_shell_metrics.desktop_gap_x)
#define DESKTOP_ICON_GAP_Y    (g_shell_metrics.desktop_gap_y)
#define DESKTOP_ICON_SIZE     (g_shell_metrics.desktop_icon_size)

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

typedef struct {
    int active;
    int opening;
    uint32_t start_tick;
    uint32_t duration_ticks;
} shell_anim_t;

typedef enum {
    START_POPUP_NONE = 0,
    START_POPUP_POWER,
    START_POPUP_USER,
} start_popup_kind_t;

typedef struct {
    int visible;
    int hover;
    int count;
    int w;
    int h;
    gui_rect_t anchor;
    gui_rect_t rect;
    start_popup_kind_t kind;
    shell_anim_t anim;
    const context_menu_item_t *items;
} start_popup_state_t;

typedef struct {
    int active;
    uint32_t start_tick;
    uint32_t duration_ticks;
    int from_y;
    int from_h;
} window_intro_anim_t;

static shell_anim_t g_start_anim = {0, 0, 0, 16};
static shell_anim_t g_search_anim = {0, 0, 0, 14};
static start_popup_state_t g_start_popup;
static window_intro_anim_t g_window_intro[GUI_MAX_WINDOWS];
static void start_popup_close(void);

static void gui_refresh_shell_metrics(void) {
    const gfx_display_profile_t *dp = gfx_display_profile();

    if (dp->density == GFX_DENSITY_COMPACT) {
        g_shell_metrics.title_bar_h = 24;
        g_shell_metrics.taskbar_h = 30;
        g_shell_metrics.resize_handle = 10;
        g_shell_metrics.window_min_w = 220;
        g_shell_metrics.window_min_h = 160;
        g_shell_metrics.desktop_margin_x = 16;
        g_shell_metrics.desktop_margin_y = 22;
        g_shell_metrics.desktop_slot_w = 96;
        g_shell_metrics.desktop_slot_h = 88;
        g_shell_metrics.desktop_gap_x = 12;
        g_shell_metrics.desktop_gap_y = 10;
        g_shell_metrics.desktop_icon_size = 32;
        g_shell_metrics.start_button_w = 76;
        g_shell_metrics.start_menu_w = 396;
        g_shell_metrics.start_header_h = 44;
        g_shell_metrics.start_footer_h = 52;
        g_shell_metrics.start_cell_w = 116;
        g_shell_metrics.start_cell_h = 58;
        g_shell_metrics.search_w = 440;
        g_shell_metrics.search_h = 332;
    } else if (dp->density == GFX_DENSITY_NORMAL) {
        g_shell_metrics.title_bar_h = 28;
        g_shell_metrics.taskbar_h = 36;
        g_shell_metrics.resize_handle = 12;
        g_shell_metrics.window_min_w = 260;
        g_shell_metrics.window_min_h = 190;
        g_shell_metrics.desktop_margin_x = 20;
        g_shell_metrics.desktop_margin_y = 24;
        g_shell_metrics.desktop_slot_w = 112;
        g_shell_metrics.desktop_slot_h = 100;
        g_shell_metrics.desktop_gap_x = 14;
        g_shell_metrics.desktop_gap_y = 12;
        g_shell_metrics.desktop_icon_size = 32;
        g_shell_metrics.start_button_w = 90;
        g_shell_metrics.start_menu_w = 436;
        g_shell_metrics.start_header_h = 48;
        g_shell_metrics.start_footer_h = 56;
        g_shell_metrics.start_cell_w = 128;
        g_shell_metrics.start_cell_h = 64;
        g_shell_metrics.search_w = 500;
        g_shell_metrics.search_h = 356;
    } else {
        g_shell_metrics.title_bar_h = 32;
        g_shell_metrics.taskbar_h = 42;
        g_shell_metrics.resize_handle = 14;
        g_shell_metrics.window_min_w = 300;
        g_shell_metrics.window_min_h = 220;
        g_shell_metrics.desktop_margin_x = 24;
        g_shell_metrics.desktop_margin_y = 28;
        g_shell_metrics.desktop_slot_w = 128;
        g_shell_metrics.desktop_slot_h = 112;
        g_shell_metrics.desktop_gap_x = 16;
        g_shell_metrics.desktop_gap_y = 12;
        g_shell_metrics.desktop_icon_size = 48;
        g_shell_metrics.start_button_w = 104;
        g_shell_metrics.start_menu_w = 488;
        g_shell_metrics.start_header_h = 52;
        g_shell_metrics.start_footer_h = 60;
        g_shell_metrics.start_cell_w = 144;
        g_shell_metrics.start_cell_h = 70;
        g_shell_metrics.search_w = 548;
        g_shell_metrics.search_h = 376;
    }

    if (dp->aspect == GFX_ASPECT_4_3) {
        g_shell_metrics.start_menu_w -= 28;
    } else if (dp->aspect == GFX_ASPECT_16_10) {
        g_shell_metrics.start_menu_w -= 8;
    }
}

static void shell_anim_begin(shell_anim_t *anim, int opening, uint32_t duration_ticks) {
    if (!anim) return;
    anim->active = 1;
    anim->opening = opening;
    anim->start_tick = timer_get_ticks();
    if (duration_ticks > 0) anim->duration_ticks = duration_ticks;
}

static void shell_anim_stop(shell_anim_t *anim) {
    if (!anim) return;
    anim->active = 0;
    anim->opening = 0;
}

static uint8_t shell_anim_alpha(shell_anim_t *anim) {
    if (!anim || !anim->active) {
        return 255u;
    }
    return th_anim_ease(th_anim_progress(anim->start_tick, anim->duration_ticks, anim->opening));
}

static void shell_anim_tick(shell_anim_t *anim) {
    if (!anim || !anim->active) return;
    if (timer_get_ticks() - anim->start_tick >= anim->duration_ticks) {
        anim->active = 0;
    }
}

static void start_menu_set_open(int open) {
    if (open) {
        if (!g_start_open) {
            g_start_open = 1;
            shell_anim_begin(&g_start_anim, 1, 16);
        }
        g_shell_focus = SHELL_FOCUS_START;
    } else {
        g_start_open = 0;
        g_start_sel = 0;
        shell_anim_stop(&g_start_anim);
        start_popup_close();
    }
}

static int start_menu_visible(void) {
    return g_start_open || (g_start_anim.active && g_start_anim.opening);
}

static void search_overlay_set_open(int open) {
    if (open) {
        g_search_active = 1;
        shell_anim_begin(&g_search_anim, 1, 14);
    } else if (g_search_active) {
        g_search_active = 0;
        shell_anim_begin(&g_search_anim, 0, 12);
    }
}

static int search_overlay_visible(void) {
    return g_search_active || (g_search_anim.active && !g_search_anim.opening);
}

static void start_popup_close(void) {
    g_start_popup.visible = 0;
    g_start_popup.hover = -1;
    g_start_popup.count = 0;
    g_start_popup.kind = START_POPUP_NONE;
    g_start_popup.items = 0;
    shell_anim_stop(&g_start_popup.anim);
}

static int shell_motion_active(void) {
    if (g_start_anim.active || g_search_anim.active) return 1;
    if (g_start_popup.anim.active) return 1;
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (g_window_intro[i].active) return 1;
    }
    return 0;
}

static void perform_session_action(session_action_t action);
static icon_asset_id_t app_icon_asset(gui_icon_kind_t kind);

/* Fields: id, label, desktop_label, icon, show_on_desktop, single_instance, in_store, dev_only, launch */
static const gui_app_t g_apps[] = {
    { "terminal",  "Terminal",      "Terminal",  GUI_ICON_TERMINAL, 1, 1, 0, 0, shell_gui_launch },
    { "files",     "Files",         "Files",     GUI_ICON_FILES,    1, 1, 0, 0, files_gui_launch },
    { "notes",     "Notes",         "Notes",     GUI_ICON_NOTES,    1, 0, 0, 0, notes_gui_launch },
    { "work180",   "180 Work",      "180 Work",  GUI_ICON_WORK180,  1, 1, 0, 0, work_gui_launch },
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
    gfx_draw_string_role(x, y, text, FONT_ROLE_UI, th_metrics()->font_body, fg, bg);
}

static void draw_label_overlay(int x, int y, const char *text, uint32_t fg) {
    gfx_draw_string_role_transparent(x, y, text, FONT_ROLE_UI, th_metrics()->font_body, fg);
}

static int text_width(const char *text) {
    return gfx_measure_text(FONT_ROLE_UI, th_metrics()->font_body, text);
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
    int min_w = WIN_MIN_W;
    int min_h = WIN_MIN_H;

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

static void fit_window_frame_with_min(int *x, int *y, int *w, int *h, int min_w, int min_h) {
    int save_min_w = WIN_MIN_W;
    int save_min_h = WIN_MIN_H;

    if (min_w < save_min_w) min_w = save_min_w;
    if (min_h < save_min_h) min_h = save_min_h;

    if (*w < min_w) *w = min_w;
    if (*h < min_h) *h = min_h;
    fit_window_frame(x, y, w, h);
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

const gui_shell_metrics_t *gui_shell_metrics(void) {
    return &g_shell_metrics;
}

static int window_min_w(const gui_window_t *w) {
    if (w && w->min_w > WIN_MIN_W) return w->min_w;
    return WIN_MIN_W;
}

static int window_min_h(const gui_window_t *w) {
    if (w && w->min_h > WIN_MIN_H) return w->min_h;
    return WIN_MIN_H;
}

static void begin_window_intro(int id) {
    gui_window_t *w;

    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    w = &g_windows[id];
    g_window_intro[id].active = 1;
    g_window_intro[id].start_tick = timer_get_ticks();
    g_window_intro[id].duration_ticks = 15;
    g_window_intro[id].from_y = 12;
    g_window_intro[id].from_h = 14;
    w->content = (gui_rect_t){0, 0, 0, 0};
}

static gui_rect_t window_visual_frame(int id) {
    gui_rect_t frame = g_windows[id].frame;
    window_intro_anim_t *anim;

    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return frame;
    anim = &g_window_intro[id];
    if (!anim->active) return frame;

    {
        uint8_t eased = th_anim_ease(th_anim_progress(anim->start_tick, anim->duration_ticks, 1));
        int dy = th_lerp_int(anim->from_y, 0, eased);
        int dh = th_lerp_int(anim->from_h, 0, eased);
        frame.y += dy;
        frame.h -= dh;
        if (frame.h < window_min_h(&g_windows[id])) frame.h = window_min_h(&g_windows[id]);
    }
    return frame;
}

static void maximized_frame_rect(gui_rect_t *out) {
    gui_rect_t area = gui_desktop_bounds();
    if (!out) return;
    out->x = area.x;
    out->y = area.y;
    out->w = area.w;
    out->h = area.h;
    if (out->w < WIN_MIN_W) out->w = WIN_MIN_W;
    if (out->h < WIN_MIN_H) out->h = WIN_MIN_H;
}

static void set_window_maximized(int id, int maximized) {
    gui_window_t *w;

    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    w = &g_windows[id];

    if (maximized) {
        gui_rect_t frame;
        if (w->maximized) return;
        w->restore_frame = w->frame;
        maximized_frame_rect(&frame);
        w->frame = frame;
        w->maximized = 1;
    } else {
        int x, y, fw, fh;
        if (!w->maximized) return;
        x = w->restore_frame.x;
        y = w->restore_frame.y;
        fw = w->restore_frame.w;
        fh = w->restore_frame.h;
        fit_window_frame_with_min(&x, &y, &fw, &fh, window_min_w(w), window_min_h(w));
        w->frame.x = x;
        w->frame.y = y;
        w->frame.w = fw;
        w->frame.h = fh;
        w->maximized = 0;
        begin_window_intro(id);
    }

    w->dragging = 0;
    w->resizing = 0;
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

static const gui_background_style_t *background_style(gui_background_theme_t theme) {
    if (theme < 0 || theme >= GUI_BG_THEME_COUNT) {
        return &k_background_themes[GUI_BG_THEME_MINT];
    }
    return &k_background_themes[theme];
}

static int gui_restore_path(const char *path) {
    char segment[13];
    int seg_len = 0;

    if (!vfs_available()) return 0;
    if (!path || path[0] != '/') return vfs_cd("/");
    if (!vfs_cd("/")) return 0;
    if (path[1] == '\0') return 1;

    for (int i = 1; ; i++) {
        char ch = path[i];
        if (ch == '/' || ch == '\0') {
            if (seg_len > 0) {
                segment[seg_len] = '\0';
                if (!vfs_cd(segment)) return 0;
                seg_len = 0;
            }
            if (ch == '\0') break;
        } else if (seg_len + 1 < (int)sizeof(segment)) {
            segment[seg_len++] = ch;
        }
    }
    return 1;
}

static int background_theme_save(void) {
    char saved[256];
    char buf[48];
    const gui_background_style_t *style = background_style(g_background_theme);
    int ok = 1;

    if (!vfs_available()) return 1;
    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return 0;
    str_copy(buf, "theme=", sizeof(buf));
    str_cat(buf, style->persist_id, sizeof(buf));
    str_cat(buf, "\n", sizeof(buf));
    if (vfs_write(GUI_THEME_FILE, (const uint8_t *)buf, (uint32_t)str_len(buf)) <= 0) {
        ok = 0;
    }
    gui_restore_path(saved);
    /* BIOS-backed storage helpers can leave IRQ1 masked; keep keyboard input
       alive after persisting desktop settings from GUI code. */
    pic_clear_mask(1);
    return ok;
}

static void background_theme_load_once(void) {
    char saved[256];
    uint8_t buf[64];
    int read;
    int touched_storage = 0;

    if (g_background_loaded) return;
    g_background_loaded = 1;
    g_background_theme = GUI_BG_THEME_MINT;
    g_desktop_color = background_style(g_background_theme)->accent_bottom;

    if (!vfs_available()) return;
    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    touched_storage = 1;
    if (!vfs_cd("/")) {
        pic_clear_mask(1);
        return;
    }
    read = vfs_cat(GUI_THEME_FILE, buf, (int)sizeof(buf) - 1);
    gui_restore_path(saved);
    if (touched_storage) {
        /* The theme file read happens before the login loop starts on GUI
           boots, so explicitly restore IRQ1 afterward. */
        pic_clear_mask(1);
    }
    if (read <= 0) return;

    buf[read] = '\0';
    for (int i = 0; i < GUI_BG_THEME_COUNT; i++) {
        const gui_background_style_t *style = &k_background_themes[i];
        if (str_ncmp((const char *)buf, "theme=", 6) == 0) {
            if (str_ncmp((const char *)buf + 6, style->persist_id,
                         (int)str_len(style->persist_id)) == 0) {
                g_background_theme = style->id;
                g_desktop_color = style->accent_bottom;
                return;
            }
        }
    }
}

static void draw_soft_blob(int cx, int cy, int radius, uint32_t color, uint8_t alpha) {
    if (radius <= 0) return;
    for (int dy = -radius; dy <= radius; dy++) {
        int band = radius * radius - dy * dy;
        int half = 0;
        while ((half + 1) * (half + 1) <= band) half++;
        gfx_fill_rect_alpha(cx - half, cy + dy, half * 2 + 1, 1, color, alpha);
    }
}

static void draw_diagonal_ribbon(int sw, int y, int thickness, int slant, uint32_t color, uint8_t alpha) {
    int width = sw + sw / 2;
    if (thickness < 4) thickness = 4;
    for (int row = 0; row < thickness; row++) {
        int x = -sw / 6 + row * slant / thickness;
        gfx_fill_rect_alpha(x, y + row, width, 1, color, alpha);
    }
}

static void draw_background_scene(int full_h, int auth_mode) {
    const gui_background_style_t *style;
    int sw;
    int sh;
    int base_h;

    background_theme_load_once();
    style = background_style(g_background_theme);
    sw = (int)gfx_width();
    sh = (int)gfx_height();
    base_h = full_h > 0 ? full_h : sh;

    gfx_fill_rect_gradient_v(0, 0, sw, base_h, style->desktop_top, style->desktop_bottom);
    gfx_fill_rect_alpha(0, 0, sw, base_h / 3, gfx_rgb(255, 255, 255), 12);

    if (g_background_theme == GUI_BG_THEME_MINT) {
        draw_soft_blob(sw / 5, base_h / 5, sw / 7, style->glow, 22);
        draw_soft_blob(sw - sw / 6, base_h / 3, sw / 8, style->band_b, 18);
        draw_diagonal_ribbon(sw, base_h / 7, base_h / 8, sw / 5, style->band_a, 28);
        draw_diagonal_ribbon(sw, base_h / 2, base_h / 7, sw / 4, style->band_b, 24);
    } else if (g_background_theme == GUI_BG_THEME_GLASS) {
        draw_soft_blob(sw / 3, base_h / 4, sw / 6, style->glow, 26);
        draw_soft_blob(sw - sw / 4, base_h / 2, sw / 5, style->band_b, 22);
        draw_diagonal_ribbon(sw, base_h / 8, base_h / 9, sw / 3, style->band_a, 22);
        draw_diagonal_ribbon(sw, base_h / 3, base_h / 6, sw / 5, style->band_b, 18);
        draw_diagonal_ribbon(sw, base_h * 3 / 5, base_h / 10, sw / 4, gfx_rgb(255, 255, 255), 14);
    } else if (g_background_theme == GUI_BG_THEME_STUDIO) {
        draw_soft_blob(sw / 4, base_h / 3, sw / 7, style->glow, 24);
        draw_soft_blob(sw - sw / 5, base_h / 4, sw / 8, style->band_b, 20);
        draw_diagonal_ribbon(sw, base_h / 5, base_h / 11, sw / 6, style->band_a, 24);
        draw_diagonal_ribbon(sw, base_h / 2, base_h / 8, sw / 5, style->band_b, 20);
        draw_diagonal_ribbon(sw, base_h * 3 / 4, base_h / 12, sw / 7, gfx_rgb(255, 255, 255), 10);
    } else if (g_background_theme == GUI_BG_THEME_SUNSET) {
        draw_soft_blob(sw / 2, base_h / 5, sw / 7, style->glow, 28);
        draw_soft_blob(sw - sw / 6, base_h / 2, sw / 8, style->band_b, 18);
        draw_diagonal_ribbon(sw, base_h / 4, base_h / 9, sw / 4, style->band_a, 26);
        draw_diagonal_ribbon(sw, base_h / 2, base_h / 7, sw / 6, style->band_b, 20);
    } else if (g_background_theme == GUI_BG_THEME_OCEAN) {
        draw_soft_blob(sw / 4, base_h / 4, sw / 6, style->glow, 24);
        draw_soft_blob(sw - sw / 5, base_h * 2 / 3, sw / 7, style->band_b, 20);
        draw_diagonal_ribbon(sw, base_h / 6, base_h / 10, sw / 7, style->band_a, 18);
        draw_diagonal_ribbon(sw, base_h / 2, base_h / 8, sw / 9, style->band_b, 24);
        draw_diagonal_ribbon(sw, base_h * 3 / 4, base_h / 9, sw / 5, gfx_rgb(255, 255, 255), 10);
    } else {
        int tile_w = sw / 5;
        int tile_h = base_h / 5;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 6; col++) {
                int x = col * tile_w - ((row & 1) ? tile_w / 3 : 0);
                int y = row * tile_h + row * 10;
                uint32_t color = ((row + col) & 1) ? style->band_a : style->band_b;
                gfx_fill_rect_alpha(x, y, tile_w, tile_h, color, (uint8_t)(12 + ((row + col) & 1) * 8));
            }
        }
        draw_soft_blob(sw / 2, base_h / 2, sw / 6, style->glow, 20);
    }

    gfx_fill_rect_alpha(0, base_h - base_h / 5, sw, base_h / 5, gfx_rgb(5, 8, 14), auth_mode ? 80 : 64);
    if (auth_mode) {
        gfx_fill_rect_alpha(0, 0, sw, base_h, style->auth_overlay, 80);
    }
}

static void fill_desktop_background(void) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int dh = sh - TASKBAR_HEIGHT;
    const gui_background_style_t *style;

    draw_background_scene(dh, 0);
    style = background_style(g_background_theme);
    gfx_fill_rect_gradient_v(0, dh, sw, sh - dh, style->taskbar_top, style->taskbar_bottom);
    gfx_fill_rect_alpha(0, dh, sw, 1, gfx_rgb(210, 221, 241), 104);
}

/* ---- New start menu helpers ---- */
typedef struct {
    const char *app_id;
    const char *label;
} start_quick_item_t;

static const start_quick_item_t k_start_quick_items[] = {
    { "files",     "Files" },
    { "browser",   "Browser" },
    { "ctrlpanel", "Settings" },
    { "osinfo",    "OS Info" },
};

static void start_menu_draw_bounds(int *out_x, int *out_y, int *out_w, int *out_h, uint8_t *out_alpha);

static int app_index_by_id(const char *id) {
    for (int i = 0; i < gui_app_count(); i++) {
        if (str_eq(g_apps[i].id, id)) return i;
    }
    return -1;
}

static int start_menu_app_visible(int app_idx) {
    if (app_idx < 0 || app_idx >= gui_app_count()) return 0;
    if (g_apps[app_idx].dev_only && !users_current_is_admin()) return 0;
    return 1;
}

static int start_menu_app_count(void) {
    int n = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (start_menu_app_visible(i)) n++;
    }
    return n;
}

static int start_menu_app_index(int vis) {
    int seen = 0;
    for (int i = 0; i < gui_app_count(); i++) {
        if (!start_menu_app_visible(i)) continue;
        if (seen == vis) return i;
        seen++;
    }
    return -1;
}

static int start_quick_app_count(void) {
    int n = 0;
    for (int i = 0; i < (int)(sizeof(k_start_quick_items) / sizeof(k_start_quick_items[0])); i++) {
        int app_idx = app_index_by_id(k_start_quick_items[i].app_id);
        if (start_menu_app_visible(app_idx)) n++;
    }
    return n;
}

static int start_quick_app_index(int vis) {
    int seen = 0;
    for (int i = 0; i < (int)(sizeof(k_start_quick_items) / sizeof(k_start_quick_items[0])); i++) {
        int app_idx = app_index_by_id(k_start_quick_items[i].app_id);
        if (!start_menu_app_visible(app_idx)) continue;
        if (seen == vis) return app_idx;
        seen++;
    }
    return -1;
}

static int start_menu_row_h(void) {
    return th_metrics()->list_row_h + 8;
}

static int start_menu_card_h(void) {
    return (th_metrics()->font_body * 2) + th_metrics()->gap_lg + 12;
}

static int start_menu_header_h(void) {
    const th_metrics_t *tm = th_metrics();
    return tm->font_title + tm->font_small + tm->gap_sm + 10;
}

static int start_menu_h(void) {
    const th_metrics_t *tm = th_metrics();
    int app_area_h = start_menu_app_count() * start_menu_row_h();
    int quick_rows = (start_quick_app_count() + 1) / 2;
    int quick_area_h = quick_rows * (start_menu_card_h() + tm->gap_sm);
    int body_h = app_area_h;
    int header_h = start_menu_header_h();

    if (body_h < quick_area_h) body_h = quick_area_h;
    body_h += tm->font_small + tm->gap_md + 8;
    return tm->gap_lg + header_h + tm->gap_sm + tm->field_h + tm->gap_md
         + body_h + START_FOOTER_H + tm->gap_lg;
}

static int start_menu_x(void) { return 10; }

static int start_menu_y(void) {
    int y = (int)gfx_height() - TASKBAR_HEIGHT - start_menu_h() - 8;
    if (y < 6) y = 6;
    return y;
}

static gui_rect_t start_search_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r;
    int x;
    int y;
    start_menu_draw_bounds(&x, &y, 0, 0, 0);

    r.x = x + tm->gap_lg;
    r.y = y + tm->gap_lg + start_menu_header_h() + tm->gap_sm;
    r.w = START_MENU_W - tm->gap_lg * 2;
    r.h = tm->field_h;
    return r;
}

static gui_rect_t start_list_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t sr = start_search_rect();
    gui_rect_t r;
    int x;
    int y;
    int w;
    int h;
    (void)x;
    (void)w;
    start_menu_draw_bounds(&x, &y, &w, &h, 0);
    int footer_y = y + h - START_FOOTER_H;

    r.x = sr.x;
    r.y = sr.y + sr.h + tm->gap_md + tm->font_small + tm->gap_sm;
    r.w = START_MENU_W - (tm->gap_lg * 3) - 136;
    r.h = footer_y - r.y - tm->gap_md;
    return r;
}

static gui_rect_t start_quick_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t list = start_list_rect();
    gui_rect_t r;

    r.x = list.x + list.w + tm->gap_md;
    r.y = list.y;
    r.w = start_search_rect().x + start_search_rect().w - r.x;
    r.h = list.h;
    return r;
}

static gui_rect_t start_user_footer_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r;
    int x;
    int y;
    int h;
    start_menu_draw_bounds(&x, &y, 0, &h, 0);
    int footer_y = y + h - START_FOOTER_H;

    r.x = x + tm->gap_lg;
    r.y = footer_y + 7;
    r.w = START_MENU_W - 74;
    r.h = START_FOOTER_H - 14;
    return r;
}

static gui_rect_t start_power_footer_rect(void) {
    gui_rect_t r;
    const th_metrics_t *tm = th_metrics();
    int x;
    int y;
    int h;
    start_menu_draw_bounds(&x, &y, 0, &h, 0);
    int footer_y = y + h - START_FOOTER_H;

    r.w = 40;
    r.h = START_FOOTER_H - 16;
    r.x = x + START_MENU_W - r.w - tm->gap_lg;
    r.y = footer_y + 8;
    return r;
}

static void start_menu_draw_bounds(int *out_x, int *out_y, int *out_w, int *out_h, uint8_t *out_alpha) {
    uint8_t alpha = shell_anim_alpha(&g_start_anim);
    int x = start_menu_x();
    int y = start_menu_y();
    int w = START_MENU_W;
    int h = start_menu_h();
    int slide = th_lerp_int(18, 0, alpha);

    if (!start_menu_visible()) {
        alpha = 0;
    }

    if (out_x) *out_x = x;
    if (out_y) *out_y = y + slide;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (out_alpha) *out_alpha = alpha;
}

static gui_rect_t start_quick_card_rect(int vis) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t rail = start_quick_rect();
    gui_rect_t r;
    int col = vis & 1;
    int row = vis / 2;
    int cell_w = (rail.w - tm->gap_sm) / 2;
    int cell_h = start_menu_card_h();

    r.x = rail.x + col * (cell_w + tm->gap_sm);
    r.y = rail.y + row * (cell_h + tm->gap_sm);
    r.w = cell_w;
    r.h = cell_h;
    return r;
}

static int rect_contains(gui_rect_t r, int px, int py) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

static int start_popup_row_h(void) {
    int row_h = th_metrics()->font_body + th_metrics()->gap_md;
    if (row_h < th_metrics()->min_hit) row_h = th_metrics()->min_hit;
    return row_h;
}

static void start_popup_open(start_popup_kind_t kind, gui_rect_t anchor,
                             const context_menu_item_t *items, int count) {
    if (!items || count <= 0) return;
    if (count > CONTEXT_MENU_MAX_ITEMS) count = CONTEXT_MENU_MAX_ITEMS;

    context_menu_measure(items, count, &g_start_popup.w, &g_start_popup.h);
    g_start_popup.anchor = anchor;
    g_start_popup.rect.x = anchor.x + anchor.w - g_start_popup.w;
    g_start_popup.rect.y = anchor.y - g_start_popup.h - 8;
    if (g_start_popup.rect.y < 6) {
        g_start_popup.rect.y = anchor.y + anchor.h + 6;
    }
    if (g_start_popup.rect.x < 6) g_start_popup.rect.x = 6;
    if (g_start_popup.rect.x + g_start_popup.w > (int)gfx_width() - 6) {
        g_start_popup.rect.x = (int)gfx_width() - g_start_popup.w - 6;
    }
    g_start_popup.kind = kind;
    g_start_popup.items = items;
    g_start_popup.count = count;
    g_start_popup.hover = -1;
    g_start_popup.visible = 1;
    shell_anim_begin(&g_start_popup.anim, 1, 12);
}

static int start_popup_visible(void) {
    return g_start_popup.visible;
}

static gui_rect_t start_popup_visual_rect(void) {
    gui_rect_t rect = g_start_popup.rect;
    uint8_t alpha = shell_anim_alpha(&g_start_popup.anim);
    rect.y += th_lerp_int(8, 0, alpha);
    return rect;
}

static void draw_start_popup(void) {
    const th_metrics_t *m = th_metrics();
    int row_h = start_popup_row_h();
    int pad_x = m->gap_md;
    int pad_y = m->gap_sm;
    int icon_slot = m->font_body + m->gap_sm;
    uint8_t alpha = shell_anim_alpha(&g_start_popup.anim);
    gui_rect_t rect;

    if (!start_popup_visible() || !g_start_popup.items || g_start_popup.count <= 0) return;

    rect = start_popup_visual_rect();
    gfx_fill_rect_alpha(rect.x + 4, rect.y + 6, rect.w, rect.h, gfx_rgb(5, 8, 14), (uint8_t)(alpha / 6));
    gfx_fill_rect_alpha(rect.x + 1, rect.y + 2, rect.w, rect.h, gfx_rgb(5, 8, 14), (uint8_t)(alpha / 12));
    gfx_fill_rect(rect.x, rect.y, rect.w, rect.h, gfx_rgb(60, 77, 101));
    gfx_fill_rect_gradient_v(rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2,
                             gfx_rgb(252, 254, 255), gfx_rgb(245, 248, 255));
    gfx_fill_rect_alpha(rect.x + 1, rect.y + 1, rect.w - 2, 1, gfx_rgb(255, 255, 255), (uint8_t)(alpha / 2));

    for (int i = 0; i < g_start_popup.count; i++) {
        int iy = rect.y + pad_y + i * row_h;
        uint32_t row_bg = (i & 1) ? gfx_rgb(237, 243, 251) : gfx_rgb(245, 248, 255);
        uint32_t hover_bg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(180, 52, 72) : TH_ACCENT_HOT;
        uint32_t fg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(153, 27, 27) : TH_TEXT;
        uint32_t icon_fg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(153, 27, 27) : TH_TEXT_DIM;
        int tx = rect.x + pad_x;

        if (i == g_start_popup.hover) {
            row_bg = hover_bg;
            fg = TH_TEXT_INVERT;
            icon_fg = TH_TEXT_INVERT;
        }

        gfx_fill_rect(rect.x + 1, iy, rect.w - 2, row_h, row_bg);
        if (g_start_popup.items[i].icon_id != ICON_NONE) {
            icon_draw(rect.x + pad_x, iy + (row_h - m->font_body) / 2, m->font_body,
                      g_start_popup.items[i].icon_id, icon_fg);
            tx += icon_slot;
        }
        gfx_draw_string_role_transparent(tx, iy + (row_h - m->font_body) / 2,
                                         g_start_popup.items[i].label, FONT_ROLE_UI, m->font_body, fg);
    }
}

static int start_popup_handle_pointer(int mx, int my, uint8_t pressed, uint8_t released) {
    int row_h = start_popup_row_h();
    int pad_y = th_metrics()->gap_sm;
    gui_rect_t rect = start_popup_visual_rect();
    int inside;

    if (!start_popup_visible()) return 0;

    inside = rect_contains(rect, mx, my);
    if (inside) {
        g_start_popup.hover = (my - rect.y - pad_y) / row_h;
        if (g_start_popup.hover < 0 || g_start_popup.hover >= g_start_popup.count) {
            g_start_popup.hover = -1;
        }
    } else {
        g_start_popup.hover = -1;
    }

    if ((pressed & 0x01u) && !inside) {
        start_popup_close();
        return 1;
    }

    if ((released & 0x01u) && inside && g_start_popup.hover >= 0) {
        int idx = g_start_popup.hover;
        const context_menu_item_t *item = &g_start_popup.items[idx];
        start_popup_close();
        start_menu_set_open(0);
        if (item->action) item->action(item->userdata);
        return 1;
    }

    if ((released & 0x01u) && !inside) {
        start_popup_close();
        return 1;
    }

    return inside ? 1 : 0;
}

static void draw_start_menu(void) {
    const th_metrics_t *tm = th_metrics();
    const gui_background_style_t *style = background_style(gui_get_background_theme());
    uint8_t alpha;
    int x;
    int y;
    int h;
    int w;
    int footer_y;
    int icon_sz = (gfx_display_profile()->density == GFX_DENSITY_COMFORTABLE) ? 28 : 24;
    gui_rect_t search_rect = start_search_rect();
    gui_rect_t list_rect = start_list_rect();
    gui_rect_t quick_rect = start_quick_rect();
    gui_rect_t user_rect = start_user_footer_rect();
    gui_rect_t power_rect = start_power_footer_rect();
    const char *uname = auth_session_active() ? auth_session_username() : users_current();
    char greet[48];
    char search_hint[40];

    start_menu_draw_bounds(&x, &y, &w, &h, &alpha);
    footer_y = y + h - START_FOOTER_H;

    gfx_fill_rect_alpha(x + 8, y + 10, w, h, gfx_rgb(5, 8, 14), (uint8_t)(20 + alpha / 4));
    gfx_fill_rect_alpha(x + 2, y + 4, w, h, gfx_rgb(5, 8, 14), (uint8_t)(8 + alpha / 12));
    gfx_fill_rect(x, y, w, h, gfx_rgb(26, 34, 46));
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(248, 251, 255), gfx_rgb(232, 239, 249));
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), (uint8_t)(32 + alpha / 3));
    gfx_fill_rect_gradient_h(x + 1, y + 1, w - 2, 6, style->accent_top, style->accent_bottom);
    greet[0] = '\0';
    str_copy(greet, "Welcome, ", sizeof(greet));
    str_cat(greet, uname, sizeof(greet));
    gfx_draw_string_role_transparent(x + tm->gap_lg, y + 12,
                                     greet, FONT_ROLE_UI, tm->font_title, TH_TEXT);
    gfx_draw_string_role_transparent(x + tm->gap_lg, y + 12 + tm->font_title + 2,
                                     "Apps, shortcuts, and session controls",
                                     FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM);

    th_draw_field(search_rect.x, search_rect.y, search_rect.w, "", 0, 0);
    icon_draw(search_rect.x + 8, search_rect.y + (search_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_SEARCH, TH_TEXT_DIM);
    str_copy(search_hint, g_search_active && g_search_len > 0 ? g_search_buf : "Search apps...", sizeof(search_hint));
    gfx_draw_string_role_transparent(search_rect.x + 8 + tm->font_body + 8,
                                     search_rect.y + (search_rect.h - tm->font_body) / 2,
                                     search_hint, FONT_ROLE_UI, tm->font_body,
                                     (g_search_active && g_search_len > 0) ? TH_TEXT : TH_TEXT_DIM);

    gfx_draw_string_role_transparent(list_rect.x, list_rect.y - tm->font_small - 4,
                                     "Apps", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM);
    gfx_draw_string_role_transparent(quick_rect.x, quick_rect.y - tm->font_small - 4,
                                     "Quick Access", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM);

    {
        int n = start_menu_app_count();
        for (int vi = 0; vi < n; vi++) {
            int ai = start_menu_app_index(vi);
            int row_y = list_rect.y + vi * start_menu_row_h();
            int selected = (g_start_sel == vi);
            uint32_t row_bg = selected ? gfx_rgb(223, 234, 251) : gfx_rgb(248, 251, 255);
            uint32_t icon_fg = selected ? style->accent_bottom : TH_TEXT_DIM;

            th_draw_list_row(list_rect.x, row_y, list_rect.w, start_menu_row_h(), "", selected);
            gfx_fill_rect(list_rect.x + 1, row_y + 1, list_rect.w - 2, start_menu_row_h() - 2, row_bg);
            icon_draw(list_rect.x + tm->gap_sm, row_y + (start_menu_row_h() - icon_sz) / 2,
                      icon_sz, app_icon_asset(g_apps[ai].icon_kind), 0);
            gfx_draw_string_role_transparent(list_rect.x + tm->gap_sm + icon_sz + tm->gap_sm,
                                             row_y + 7,
                                             g_apps[ai].label, FONT_ROLE_UI, tm->font_body, TH_TEXT);
            gfx_draw_string_role_transparent(list_rect.x + tm->gap_sm + icon_sz + tm->gap_sm,
                                             row_y + 7 + tm->font_body + 1,
                                             g_apps[ai].id, FONT_ROLE_UI, tm->font_small,
                                             selected ? icon_fg : TH_TEXT_DIM);
        }
    }

    {
        int quick_count = start_quick_app_count();
        for (int qi = 0; qi < quick_count; qi++) {
            int ai = start_quick_app_index(qi);
            gui_rect_t qr = start_quick_card_rect(qi);
            uint32_t card_bg = (qi & 1) ? gfx_rgb(239, 245, 252) : gfx_rgb(245, 249, 255);
            th_draw_card(qr.x, qr.y, qr.w, qr.h, 0, card_bg, 0);
            icon_draw(qr.x + 10, qr.y + 10, tm->font_title + 6,
                      app_icon_asset(g_apps[ai].icon_kind), 0);
            gfx_draw_string_role_transparent(qr.x + 10, qr.y + qr.h - tm->font_body - 16,
                                             g_apps[ai].label, FONT_ROLE_UI, tm->font_body, TH_TEXT);
            gfx_draw_string_role_transparent(qr.x + 10, qr.y + qr.h - tm->font_small - 8,
                                             "Open", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM);
        }
    }

    gfx_fill_rect_gradient_h(x + 1, footer_y, w - 2, START_FOOTER_H,
                             gfx_rgb(22, 30, 43), gfx_rgb(30, 41, 57));
    gfx_fill_rect(x + 1, footer_y, w - 2, 1, gfx_rgb(87, 101, 126));

    gfx_fill_rect_alpha(user_rect.x + 2, user_rect.y + 3, user_rect.w, user_rect.h, gfx_rgb(5, 8, 14), 24);
    gfx_fill_rect(user_rect.x, user_rect.y, user_rect.w, user_rect.h, gfx_rgb(48, 62, 82));
    gfx_fill_rect_gradient_v(user_rect.x + 1, user_rect.y + 1, user_rect.w - 2, user_rect.h - 2,
                             gfx_rgb(61, 78, 102), gfx_rgb(34, 43, 60));
    icon_draw(user_rect.x + 8, user_rect.y + (user_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_USER, gfx_rgb(235, 242, 255));
    {
        char un[20];
        int line_h = gfx_font_line_height(FONT_ROLE_UI, tm->font_small);
        clip_title(un, sizeof(un), uname, 16);
        gfx_draw_string_role_transparent(user_rect.x + 8 + tm->font_body + 10,
                                         user_rect.y + 6,
                                         un, FONT_ROLE_UI, tm->font_body, gfx_rgb(232, 239, 255));
        gfx_draw_string_role_transparent(user_rect.x + 8 + tm->font_body + 10,
                                         user_rect.y + 6 + line_h + 1,
                                         users_current_is_admin() ? "admin" : "user",
                                         FONT_ROLE_UI, tm->font_small, gfx_rgb(154, 171, 203));
    }

    gfx_fill_rect_alpha(power_rect.x + 2, power_rect.y + 3, power_rect.w, power_rect.h, gfx_rgb(5, 8, 14), 24);
    gfx_fill_rect(power_rect.x, power_rect.y, power_rect.w, power_rect.h, gfx_rgb(92, 63, 69));
    gfx_fill_rect_gradient_v(power_rect.x + 1, power_rect.y + 1, power_rect.w - 2, power_rect.h - 2,
                             gfx_rgb(112, 79, 87), gfx_rgb(72, 46, 54));
    icon_draw(power_rect.x + (power_rect.w - tm->font_body) / 2,
              power_rect.y + (power_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_POWER, gfx_rgb(239, 226, 229));
}

/* Spin-wait ~1 second (no timer dependency) */
static void __attribute__((unused)) gui_spin_1s(void) {
    for (volatile int i = 0; i < 200000000; i++) {}
}

static void __attribute__((unused)) show_power_msg(const char *msg) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int font_px = th_metrics()->font_title;
    int msg_w = gfx_measure_text(FONT_ROLE_UI, font_px, msg);
    gfx_fill_rect(0, 0, sw, sh, gfx_rgb(10, 20, 40));
    gfx_draw_string_role((sw - msg_w) / 2,
                         sh / 2 - gfx_font_line_height(FONT_ROLE_UI, font_px) / 2,
                         msg, FONT_ROLE_UI, font_px,
                         gfx_rgb(220, 235, 255), gfx_rgb(10, 20, 40));
    gfx_swap();
}

static void do_shutdown(void) {
    power_shutdown();
}

static void do_reboot(void) {
    power_reboot();
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

static icon_asset_id_t app_icon_asset(gui_icon_kind_t kind) {
    if (kind == GUI_ICON_TERMINAL) return ICON_APP_TERMINAL;
    if (kind == GUI_ICON_FILES)    return ICON_APP_FILES;
    if (kind == GUI_ICON_EDITOR)   return ICON_APP_EDITOR;
    if (kind == GUI_ICON_OSINFO)   return ICON_APP_OSINFO;
    if (kind == GUI_ICON_SETTINGS) return ICON_APP_SETTINGS;
    if (kind == GUI_ICON_TASKMGR)  return ICON_APP_TASKMGR;
    if (kind == GUI_ICON_SNAKE)    return ICON_APP_SNAKE;
    if (kind == GUI_ICON_NOTES)    return ICON_APP_NOTES;
    if (kind == GUI_ICON_STORE)    return ICON_APP_STORE;
    if (kind == GUI_ICON_CALC)     return ICON_APP_CALC;
    if (kind == GUI_ICON_BROWSER)  return ICON_APP_BROWSER;
    if (kind == GUI_ICON_AXDOCS)   return ICON_APP_AXDOCS;
    if (kind == GUI_ICON_AXSTUDIO) return ICON_APP_AXSTUDIO;
    if (kind == GUI_ICON_WORK180)  return ICON_APP_WORK180;
    return ICON_NONE;
}

static void draw_app_icon(gui_icon_kind_t kind, int x, int y, int size) {
    icon_asset_id_t asset = app_icon_asset(kind);

    if (asset != ICON_NONE && icon_best_variant_size(asset, size) > 0) {
        icon_draw(x, y, size, asset, 0);
        return;
    }

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
    } else if (kind == GUI_ICON_WORK180) {
        gfx_fill_rect(x + 4, y + 6, 24, 20, gfx_rgb(235, 241, 250));
        gfx_fill_rect(x + 4, y + 6, 24, 2, COL_ICON_STROKE);
        gfx_fill_rect(x + 4, y + 24, 24, 2, COL_ICON_STROKE);
        gfx_fill_rect(x + 4, y + 6, 2, 20, COL_ICON_STROKE);
        gfx_fill_rect(x + 26, y + 6, 2, 20, COL_ICON_STROKE);
        gfx_fill_rect(x + 7, y + 10, 5, 12, gfx_rgb(228, 118, 46));
        gfx_fill_rect(x + 14, y + 10, 5, 12, gfx_rgb(41, 156, 93));
        gfx_fill_rect(x + 21, y + 10, 4, 12, gfx_rgb(37, 99, 198));
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
        uint32_t label_fg;
        int tile_x;
        int tile_y;
        int label_y;
        int text_x;
        int app_idx = desktop_app_index(visible);
        int selected = (visible == g_desktop_sel);
        int active = selected && g_focus < 0 && g_shell_focus == SHELL_FOCUS_DESKTOP;

        if (app_idx < 0 || !desktop_slot_rect(visible, &slot)) continue;
        app = &g_apps[app_idx];
        clip_title(label, sizeof(label),
                   app->desktop_label ? app->desktop_label : app->label, 10);

        if (selected) {
            uint32_t plate_bg = active ? COL_ICON_SEL_ACTIVE : COL_ICON_SEL;
            gfx_fill_rect_alpha(slot.x + 7, slot.y + 7, slot.w - 14, slot.h - 14, gfx_rgb(5, 8, 14), 20);
            gfx_fill_rect(slot.x + 4, slot.y + 4, slot.w - 8, slot.h - 10, plate_bg);
        }

        tile_x = slot.x + (slot.w - DESKTOP_ICON_SIZE) / 2;
        tile_y = slot.y + 10;
        if (!selected) {
            gfx_fill_rect_alpha(tile_x + 1, tile_y + 2, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, gfx_rgb(5, 8, 14), 18);
        }
        draw_app_icon(app->icon_kind, tile_x, tile_y, DESKTOP_ICON_SIZE);

        label_fg = active ? COL_ICON_TILE_ACTIVE : COL_DESKTOP_TXT;
        label_y = tile_y + DESKTOP_ICON_SIZE + 8;
        text_x = slot.x + (slot.w - text_width(label)) / 2;
        if (text_x < slot.x + 2) text_x = slot.x + 2;
        if (selected) {
            uint32_t plate_bg = active ? COL_ICON_SEL_ACTIVE : COL_ICON_SEL;
            draw_label(text_x, label_y, label, label_fg, plate_bg);
        } else {
            draw_label_overlay(text_x, label_y, label, label_fg);
        }
    }
}

static void rtc_read_time(int *h, int *m) {
    uint8_t hv, mv;
    outb(0x70, 0x04); hv = inb(0x71);
    outb(0x70, 0x02); mv = inb(0x71);
    *h = (hv & 0xF) + ((hv >> 4) & 0xF) * 10;
    *m = (mv & 0xF) + ((mv >> 4) & 0xF) * 10;
}

static void draw_taskbar(void) {
    const th_metrics_t *tm = th_metrics();
    const gui_background_style_t *style = background_style(gui_get_background_theme());
    int sw = gfx_width();
    int sh = gfx_height();
    int tb_y = sh - TASKBAR_HEIGHT;
    int btn_h = TASKBAR_HEIGHT - 8;
    int btn_y = tb_y + (TASKBAR_HEIGHT - btn_h) / 2;
    int x = START_BUTTON_X + START_BUTTON_W + 10;
    int start_hot = g_start_open || start_popup_visible() ||
                    (g_focus < 0 && g_shell_focus == SHELL_FOCUS_START);

    gfx_fill_rect_gradient_v(0, tb_y, sw, TASKBAR_HEIGHT, style->taskbar_top, style->taskbar_bottom);
    gfx_fill_rect_alpha(0, tb_y, sw, 1, gfx_rgb(196, 213, 242), 120);
    gfx_fill_rect_alpha(0, tb_y - 3, sw, 3, gfx_rgb(4, 6, 10), 24);

    gfx_fill_rect_alpha(START_BUTTON_X + 2, btn_y + 2, START_BUTTON_W, btn_h, gfx_rgb(5, 8, 14), 28);
    gfx_fill_rect(START_BUTTON_X, btn_y, START_BUTTON_W, btn_h, gfx_rgb(54, 72, 96));
    gfx_fill_rect_gradient_v(START_BUTTON_X + 1, btn_y + 1, START_BUTTON_W - 2, btn_h - 2,
                             start_hot ? style->accent_hot_top : style->accent_top,
                             start_hot ? style->accent_hot_bottom : style->accent_bottom);
    gfx_fill_rect_alpha(START_BUTTON_X + 1, btn_y + 1, START_BUTTON_W - 2, btn_h / 2,
                        gfx_rgb(255, 255, 255), 34);
    icon_draw(START_BUTTON_X + 9, btn_y + (btn_h - tm->font_body) / 2, tm->font_body,
              ICON_SYM_SEARCH, COL_TASKBAR_TXT);
    draw_label_overlay(START_BUTTON_X + 9 + tm->font_body + 6,
                       tb_y + (TASKBAR_HEIGHT - tm->font_body) / 2, "Apps", COL_TASKBAR_TXT);

    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        g_taskbar_slots[i].x = 0;
        g_taskbar_slots[i].w = 0;
        g_taskbar_slots[i].win_id = -1;
    }

    for (int i = 0; i < g_zcount; i++) {
        int win_id = g_zorder[i];
        int bw = 110;
        char title[18];
        int active = (win_id == g_focus) ||
                     (g_focus < 0 && g_shell_focus == SHELL_FOCUS_TASKBAR && i == g_taskbar_sel);
        uint32_t top = g_windows[win_id].minimized ? gfx_rgb(74, 86, 108) :
                       (active ? style->accent_top : gfx_rgb(71, 82, 105));
        uint32_t bottom = g_windows[win_id].minimized ? COL_WIN_MINIMIZED :
                          (active ? style->accent_bottom : COL_BTN_BG);
        if (x + bw >= sw - 90) break;
        g_taskbar_slots[i].x = x;
        g_taskbar_slots[i].w = bw;
        g_taskbar_slots[i].win_id = win_id;
        gfx_fill_rect_alpha(x + 2, btn_y + 2, bw, btn_h, gfx_rgb(5, 8, 14), 24);
        gfx_fill_rect(x, btn_y, bw, btn_h, gfx_rgb(80, 95, 122));
        gfx_fill_rect_gradient_v(x + 1, btn_y + 1, bw - 2, btn_h - 2, top, bottom);
        gfx_fill_rect_alpha(x + 1, btn_y + 1, bw - 2, btn_h / 2, gfx_rgb(255, 255, 255), active ? 28 : 18);
        if (g_windows[win_id].icon_kind >= 0) {
            icon_draw(x + 8, btn_y + (btn_h - tm->font_body) / 2, tm->font_body,
                      app_icon_asset((gui_icon_kind_t)g_windows[win_id].icon_kind), 0);
        }
        clip_title(title, sizeof(title), gui_window_title(win_id), 12);
        draw_label_overlay(x + 8 + tm->font_body + 6,
                           btn_y + (btn_h - tm->font_body) / 2,
                           title, COL_TASKBAR_TXT);
        x += bw + 6;
    }

    {
        int rh, rm;
        rtc_read_time(&rh, &rm);
        char cl[6] = { (char)('0'+rh/10), (char)('0'+rh%10), ':', (char)('0'+rm/10), (char)('0'+rm%10), '\0' };
        const char *un = auth_session_active() ? auth_session_username() : users_current();
        int clock_w = gfx_measure_text(FONT_ROLE_UI, tm->font_small, cl);
        int user_w = gfx_measure_text(FONT_ROLE_UI, tm->font_small, un);
        int cy = tb_y + (TASKBAR_HEIGHT - tm->font_small) / 2;
        int cx = sw - clock_w - 10;
        int ux = cx - user_w - 16;
        gfx_draw_string_role_transparent(ux, cy, un, FONT_ROLE_UI, tm->font_small, COL_TASKBAR_DIM);
        gfx_draw_string_role_transparent(cx, cy, cl, FONT_ROLE_UI, tm->font_small, COL_TASKBAR_TXT);
    }
}

static void draw_window_frame(gui_window_t *w, gui_rect_t visual) {
    const th_metrics_t *tm = th_metrics();
    const gui_background_style_t *style = background_style(gui_get_background_theme());
    int x = visual.x;
    int y = visual.y;
    int fw = visual.w;
    int fh = visual.h;
    uint32_t outer;
    int title_x;
    int btn_y = y + 4;
    int btn_h = TITLE_BAR_HEIGHT - 8;

    outer = w->focused ? style->accent_bottom : gfx_rgb(66, 76, 92);
    if (!w->maximized) {
        gfx_fill_rect_alpha(x + 8, y + 12, fw, fh, gfx_rgb(7, 10, 18), 42);
        gfx_fill_rect_alpha(x + 3, y + 5, fw, fh, gfx_rgb(7, 10, 18), 18);
    }
    gfx_fill_rect(x, y, fw, fh, outer);
    gfx_fill_rect_gradient_v(x + 1, y + 1, fw - 2, fh - 2, gfx_rgb(254, 255, 255), gfx_rgb(236, 242, 251));
    gfx_fill_rect_gradient_v(x + 2, y + TITLE_BAR_HEIGHT, fw - 4, fh - TITLE_BAR_HEIGHT - 2,
                             gfx_rgb(251, 252, 255), COL_WIN_BG);
    gfx_fill_rect_alpha(x + 2, y + TITLE_BAR_HEIGHT, fw - 4, 18, gfx_rgb(255, 255, 255), 26);

    gfx_fill_rect_gradient_h(x + 2, y + 2, fw - 4, TITLE_BAR_HEIGHT - 2,
                             w->focused ? style->accent_top : style->inactive_top,
                             w->focused ? style->accent_bottom : style->inactive_bottom);
    gfx_fill_rect_alpha(x + 2, y + 2, fw - 4, 1, gfx_rgb(255, 255, 255), 110);
    gfx_fill_rect_alpha(x + 2, y + TITLE_BAR_HEIGHT - 2, fw - 4, 1, gfx_rgb(12, 18, 28), 28);

    title_x = x + 8;
    if (w->icon_kind >= 0) {
        icon_draw(x + 6, y + (TITLE_BAR_HEIGHT - tm->font_body) / 2, tm->font_body,
                  app_icon_asset((gui_icon_kind_t)w->icon_kind), 0);
        title_x = x + 12 + tm->font_body;
    }
    draw_label_overlay(title_x, y + (TITLE_BAR_HEIGHT - tm->font_body) / 2, w->title, COL_TITLE_TXT);

    /* minimize button */
    gfx_fill_rect(x + fw - 74, btn_y, 20, btn_h, gfx_rgb(135, 112, 24));
    gfx_fill_rect_gradient_v(x + fw - 73, btn_y + 1, 18, btn_h - 2, gfx_rgb(233, 198, 54), COL_MINIMIZE_BG);
    gfx_fill_rect_alpha(x + fw - 73, btn_y + 1, 18, btn_h / 2, gfx_rgb(255, 255, 255), 36);
    icon_draw(x + fw - 64, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              ICON_SYM_MINIMIZE, COL_CLOSE_TXT);

    /* maximize/restore button */
    gfx_fill_rect(x + fw - 50, btn_y, 20, btn_h, gfx_rgb(18, 102, 67));
    gfx_fill_rect_gradient_v(x + fw - 49, btn_y + 1, 18, btn_h - 2, gfx_rgb(58, 173, 123), COL_MAXIMIZE_BG);
    gfx_fill_rect_alpha(x + fw - 49, btn_y + 1, 18, btn_h / 2, gfx_rgb(255, 255, 255), 36);
    icon_draw(x + fw - 42, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              w->maximized ? ICON_SYM_RESTORE : ICON_SYM_MAXIMIZE, COL_CLOSE_TXT);

    /* close button */
    gfx_fill_rect(x + fw - 26, btn_y, 20, btn_h, gfx_rgb(128, 28, 44));
    gfx_fill_rect_gradient_v(x + fw - 25, btn_y + 1, 18, btn_h - 2, gfx_rgb(224, 92, 114), COL_CLOSE_BG);
    gfx_fill_rect_alpha(x + fw - 25, btn_y + 1, 18, btn_h / 2, gfx_rgb(255, 255, 255), 32);
    icon_draw(x + fw - 20, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              ICON_SYM_CLOSE, COL_CLOSE_TXT);

    /* resize dots bottom-right */
    if (!w->maximized) {
        gfx_fill_rect(x + fw - 3, y + fh - 3, 2, 2, gfx_rgb(130, 140, 160));
        gfx_fill_rect(x + fw - 3, y + fh - 7, 2, 2, gfx_rgb(130, 140, 160));
        gfx_fill_rect(x + fw - 7, y + fh - 3, 2, 2, gfx_rgb(130, 140, 160));
    }

    w->content.x = x + 2;
    w->content.y = y + TITLE_BAR_HEIGHT;
    w->content.w = fw - 4;
    w->content.h = fh - TITLE_BAR_HEIGHT - 2;
}

static void draw_window(int idx) {
    gui_window_t *w = &g_windows[idx];
    gui_rect_t visual;
    if (!w->active) return;
    visual = window_visual_frame(idx);
    draw_window_frame(w, visual);
    if (w->on_paint) {
        w->on_paint(idx);
    }
}

static void clamp_shell_state(void) {
    int icon_count = desktop_icon_count();
    int start_count = start_menu_app_count();
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

    shell_anim_tick(&g_start_anim);
    shell_anim_tick(&g_search_anim);
    shell_anim_tick(&g_start_popup.anim);
    if (g_start_popup.anim.active || g_search_anim.active || g_start_anim.active) dirty = 1;
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (g_window_intro[i].active &&
            now - g_window_intro[i].start_tick >= g_window_intro[i].duration_ticks) {
            g_window_intro[i].active = 0;
        }
        if (g_window_intro[i].active) dirty = 1;
    }
    if (shell_motion_active()) dirty = 1;

    /* Poll USB HID devices (USB mouse on real hardware) */
    usb_poll();

    /* Poll network stack */
    net_poll();

    return dirty;
}

void gui_repaint(void) {
    int sw = gfx_width();
    int sh = gfx_height();
    uint8_t search_alpha = shell_anim_alpha(&g_search_anim);
    clamp_shell_state();
    mouse_set_bounds(sw, sh);
    fill_desktop_background();
    draw_desktop_icons();

    for (int i = 0; i < g_zcount; i++) {
        if (g_windows[g_zorder[i]].minimized) continue;
        draw_window(g_zorder[i]);
    }

    draw_taskbar();
    if (start_menu_visible()) {
        draw_start_menu();
    }
    if (start_popup_visible()) {
        draw_start_popup();
    }

    /* Context menu (painted above everything) */
    context_menu_paint();

    /* App search overlay */
    if (search_overlay_visible()) {
        const th_metrics_t *tm = th_metrics();
        int sw = (int)gfx_width();
        int sh = (int)gfx_height();
        int ow = g_shell_metrics.search_w;
        int oh = g_shell_metrics.search_h;
        int ox = (sw - ow) / 2;
        int oy = (sh - oh) / 3 + th_lerp_int(14, 0, search_alpha);
        int bx;
        int by;
        int bw;
        int bh = tm->field_h;

        gfx_fill_rect_alpha(0, 0, sw, sh - TASKBAR_HEIGHT, gfx_rgb(6, 10, 18), (uint8_t)(search_alpha / 7));
        th_draw_dialog(ox, oy, ow, oh, "Search Apps");
        th_draw_text(ox + tm->gap_md, oy + tm->header_h + tm->gap_sm,
                     "Type to filter. Esc closes, Enter launches.",
                     TH_TEXT_DIM, TH_BG_PANEL, tm->font_small);

        bx = ox + tm->gap_md;
        by = oy + tm->header_h + tm->gap_lg;
        bw = ow - tm->gap_md * 2;
        th_draw_field(bx, by, bw, g_search_buf, 1, 0);
        /* Cursor in search box */
        gfx_fill_rect(bx + 6 + g_search_len * gfx_font_char_width(FONT_ROLE_UI, tm->font_body),
                      by + 4, 2, bh - 8, gfx_rgb(38, 99, 235));
        /* Result list */
        int n = gui_app_count();
        int ry2 = by + bh + tm->gap_sm;
        int row = 0;
        int matched_row = 0;
        for (int i = 0; i < n && ry2 + tm->list_row_h <= oy + oh - tm->gap_sm; i++) {
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
                            ? gfx_rgb(38, 99, 235) : TH_BG_CONTENT;
            uint32_t row_fg = (matched_row == g_search_sel) ? TH_TEXT_INVERT : TH_TEXT;
            gfx_fill_rect(ox + 2, ry2, ow - 4, tm->list_row_h, row_bg);
            icon_draw(ox + 12, ry2 + (tm->list_row_h - tm->font_body) / 2, tm->font_body,
                      app_icon_asset(app->icon_kind), 0);
            th_draw_text(ox + 30, ry2 + (tm->list_row_h - tm->font_body) / 2,
                         app->label, row_fg, row_bg, tm->font_body);
            ry2 += tm->list_row_h + 2;
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
        int id = g_zorder[i];
        gui_window_t *w = &g_windows[id];
        gui_rect_t frame = window_visual_frame(id);
        if (!w->active || w->minimized) continue;
        if (mx >= frame.x && mx < frame.x + frame.w &&
            my >= frame.y && my < frame.y + frame.h) {
            return id;
        }
    }
    return -1;
}

static int in_title_bar(gui_window_t *w, int mx, int my) {
    gui_rect_t frame = window_visual_frame((int)(w - g_windows));
    return mx >= frame.x && mx < frame.x + frame.w &&
           my >= frame.y && my < frame.y + TITLE_BAR_HEIGHT;
}

static int in_close_button(gui_window_t *w, int mx, int my) {
    gui_rect_t frame = window_visual_frame((int)(w - g_windows));
    int cx = frame.x + frame.w - 24;
    int cy = frame.y + 3;
    return mx >= cx && mx < cx + 18 && my >= cy && my < cy + TITLE_BAR_HEIGHT - 6;
}

static int in_minimize_button(gui_window_t *w, int mx, int my) {
    gui_rect_t frame = window_visual_frame((int)(w - g_windows));
    int bx = frame.x + frame.w - 68;
    int by = frame.y + 3;
    return mx >= bx && mx < bx + 18 && my >= by && my < by + TITLE_BAR_HEIGHT - 6;
}

static int in_maximize_button(gui_window_t *w, int mx, int my) {
    gui_rect_t frame = window_visual_frame((int)(w - g_windows));
    int bx = frame.x + frame.w - 46;
    int by = frame.y + 3;
    return mx >= bx && mx < bx + 18 && my >= by && my < by + TITLE_BAR_HEIGHT - 6;
}

static int in_resize_handle(gui_window_t *w, int mx, int my) {
    gui_rect_t frame = window_visual_frame((int)(w - g_windows));
    if (w->maximized) return 0;
    int rx = frame.x + frame.w - RESIZE_HANDLE;
    int ry = frame.y + frame.h - RESIZE_HANDLE;
    return mx >= rx && mx < frame.x + frame.w &&
           my >= ry && my < frame.y + frame.h;
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
    gui_rect_t list = start_list_rect();
    int n = start_menu_app_count();
    int row_h = start_menu_row_h();

    if (!g_start_open) return -1;
    if (mx < list.x || mx >= list.x + list.w) return -1;
    if (my < list.y || my >= list.y + n * row_h) return -1;

    {
        int vi = (my - list.y) / row_h;
        if (vi >= 0 && vi < n) return vi;
    }
    return -1;
}

static int hit_test_start_quick_card(int mx, int my) {
    int quick_count = start_quick_app_count();

    if (!g_start_open) return -1;
    for (int i = 0; i < quick_count; i++) {
        gui_rect_t r = start_quick_card_rect(i);
        if (rect_contains(r, mx, my)) return i;
    }
    return -1;
}

static int hit_test_start_search_box(int mx, int my) {
    if (!g_start_open) return 0;
    return rect_contains(start_search_rect(), mx, my);
}

/* Check if click hits the user area in footer (left 60%) */
static int hit_test_start_user_area(int mx, int my) {
    if (!g_start_open) return 0;
    return rect_contains(start_user_footer_rect(), mx, my);
}

/* Check if click hits the power button in footer (right area) */
static int hit_test_start_power_btn(int mx, int my) {
    if (!g_start_open) return 0;
    return rect_contains(start_power_footer_rect(), mx, my);
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
    background_theme_load_once();
    g_desktop_color = color;
}

uint32_t gui_get_desktop_color(void) {
    background_theme_load_once();
    return g_desktop_color;
}

void gui_set_background_theme(gui_background_theme_t theme) {
    background_theme_load_once();
    if (theme < 0 || theme >= GUI_BG_THEME_COUNT) {
        theme = GUI_BG_THEME_MINT;
    }
    g_background_theme = theme;
    g_desktop_color = background_style(theme)->accent_bottom;
    (void)background_theme_save();
}

gui_background_theme_t gui_get_background_theme(void) {
    background_theme_load_once();
    return g_background_theme;
}

int gui_background_theme_count(void) {
    return GUI_BG_THEME_COUNT;
}

const char *gui_background_theme_name(int index) {
    return background_style((gui_background_theme_t)index)->name;
}

uint32_t gui_background_theme_preview_color(int index) {
    return background_style((gui_background_theme_t)index)->accent_top;
}

void gui_draw_auth_backdrop(void) {
    draw_background_scene((int)gfx_height(), 1);
}

void gui_init(void) {
    background_theme_load_once();
    gui_refresh_shell_metrics();
    th_refresh_metrics();
    mem_set(g_windows, 0, sizeof(g_windows));
    mem_set(g_taskbar_slots, 0, sizeof(g_taskbar_slots));
    mem_set(g_window_intro, 0, sizeof(g_window_intro));
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
    g_start_anim.active = 0;
    g_search_anim.active = 0;
    g_start_popup.visible = 0;
    g_start_popup.kind = START_POPUP_NONE;
    g_start_popup.items = 0;
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
            win->maximized = 0;
            win->resizing = 0;
            win->on_paint = 0;
            win->on_tick = 0;
            win->on_key = 0;
            win->on_mouse = 0;
            win->on_close = 0;
            win->min_w = WIN_MIN_W;
            win->min_h = WIN_MIN_H;
            win->close_cancelled = 0;
            win->state = 0;
            win->icon_kind = -1;
            str_copy(win->title, title, GUI_TITLE_MAX);
            win->frame.x = x;
            win->frame.y = y;
            win->frame.w = w;
            win->frame.h = h;
            win->restore_frame = win->frame;
            g_window_intro[i].active = 0;
            g_zorder[g_zcount++] = i;
            bring_to_front(i);
            begin_window_intro(i);
            clamp_shell_state();
            return i;
        }
    }
    return -1;
}

void gui_window_close(int id) {
    int pos = -1;
    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    g_windows[id].close_cancelled = 0;
    if (g_windows[id].on_close) {
        g_windows[id].on_close(id);
    }
    if (g_windows[id].close_cancelled) {
        return;
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
    g_window_intro[id].active = 0;
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

void gui_window_set_min_size(int id, int min_w, int min_h) {
    gui_window_t *w;

    if (id < 0 || id >= GUI_MAX_WINDOWS || !g_windows[id].active) return;
    w = &g_windows[id];
    if (min_w < WIN_MIN_W) min_w = WIN_MIN_W;
    if (min_h < WIN_MIN_H) min_h = WIN_MIN_H;
    w->min_w = min_w;
    w->min_h = min_h;
    fit_window_frame_with_min(&w->frame.x, &w->frame.y, &w->frame.w, &w->frame.h,
                              w->min_w, w->min_h);
    if (!w->maximized) {
        w->restore_frame = w->frame;
    }
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

/* Power operation pending flag (set by context menu, acted on after GUI is ready) */
static int g_pending_power = 0;
#define PWR_NONE 0
#define PWR_SHUTDOWN 1
#define PWR_REBOOT 2

/* Context-menu callbacks for power/user menus */
static void ctx_action_shutdown(void *u) { (void)u; g_pending_power = PWR_SHUTDOWN; }
static void ctx_action_reboot(void *u)   { (void)u; g_pending_power = PWR_REBOOT; }
static void ctx_action_reboot_aswd(void *u) { (void)u; g_pending_power = PWR_REBOOT; }
static void ctx_action_logout(void *u)   { (void)u; perform_session_action(SESSION_ACTION_LOGOUT);   }
static void ctx_action_add_user(void *u) {
    (void)u;
    if (!users_current_is_admin()) {
        if (!permission_prompt_run("add a new user")) {
            gui_repaint();
            return;
        }
        gui_repaint();
    }
    perform_session_action(SESSION_ACTION_ADD_USER);
}

static const context_menu_item_t k_start_power_items[3] = {
    { "Shutdown",      ICON_SYM_POWER,   CONTEXT_MENU_STYLE_DANGER, ctx_action_shutdown,    0 },
    { "Reboot",        ICON_SYM_RESTORE, CONTEXT_MENU_STYLE_NORMAL, ctx_action_reboot,      0 },
    { "Reboot AswdOS", ICON_SYM_RESTORE, CONTEXT_MENU_STYLE_NORMAL, ctx_action_reboot_aswd, 0 },
};

static const context_menu_item_t k_start_user_items[2] = {
    { "Log Out",  ICON_SYM_LOGOUT,   CONTEXT_MENU_STYLE_NORMAL, ctx_action_logout,   0 },
    { "Add User", ICON_SYM_ADD_USER, CONTEXT_MENU_STYLE_NORMAL, ctx_action_add_user, 0 },
};

static void perform_session_action(session_action_t action) {
    if (action == SESSION_ACTION_ADD_USER) {
        start_popup_close();
        start_menu_set_open(0);
        control_panel_open_users();
        return;
    }
    if (action == SESSION_ACTION_DEV_TOOLS) {
        start_popup_close();
        start_menu_set_open(0);
        dev_tools_launch();
        return;
    }
    if (action == SESSION_ACTION_SHUTDOWN) {
        start_popup_close();
        start_menu_set_open(0);
        do_shutdown();
        return;
    }
    if (action == SESSION_ACTION_REBOOT || action == SESSION_ACTION_REBOOT_ASWD) {
        start_popup_close();
        start_menu_set_open(0);
        do_reboot();
        return;
    }

    close_all_windows();
    start_popup_close();
    start_menu_set_open(0);
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
        start_popup_close();
        start_menu_set_open(0);
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
    int ai = start_menu_app_index(g_start_sel);
    if (ai >= 0) {
        start_popup_close();
        start_menu_set_open(0);
        gui_launch_app(ai);
    }
}

static int handle_start_zone_key(char key) {
    if (key == 0x1B) {
        start_popup_close();
        start_menu_set_open(0);
        g_shell_focus = SHELL_FOCUS_DESKTOP;
        return 1;
    }

    if (key == '\r' || key == '\n' || is_down_key(key) || is_up_key(key)) {
        if (!g_start_open) {
            start_menu_set_open(1);
            if (is_up_key(key)) {
                int n = start_menu_app_count();
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
            if (g_windows[id].minimized) {
                g_windows[id].minimized = 0;
                begin_window_intro(id);
            }
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
            search_overlay_set_open(0);
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
            search_overlay_set_open(0);
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

    if (start_popup_visible()) {
        if (key == 0x1B) {
            start_popup_close();
            *dirty = 1;
            return;
        }
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
            search_overlay_set_open(1);
            g_search_buf[0] = '\0';
            g_search_len    = 0;
            g_search_sel    = 0;
            *dirty = 1;
            return;
        }
    }

    if (g_start_open) {
        int n = start_menu_app_count();
        if (key == '\t') {
            cycle_shell_focus();
            *dirty = 1;
            return;
        }
        if (is_up_key(key)) {
            if (g_start_sel > 0) g_start_sel--;
            *dirty = 1;
            return;
        }
        if (is_down_key(key)) {
            if (g_start_sel + 1 < n) g_start_sel++;
            *dirty = 1;
            return;
        }
        if ((unsigned char)key >= 32 && (unsigned char)key < 127) {
            search_overlay_set_open(1);
            g_search_len = 0;
            g_search_buf[0] = '\0';
            g_search_sel = 0;
            if (g_search_len + 1 < (int)sizeof(g_search_buf)) {
                g_search_buf[g_search_len++] = key;
                g_search_buf[g_search_len] = '\0';
            }
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
            start_popup_close();
            start_menu_set_open(0);
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
    search_overlay_set_open(1);
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
    { "Open Terminal", ICON_APP_TERMINAL, CONTEXT_MENU_STYLE_NORMAL, ctx_open_terminal, 0 },
    { "New Note",      ICON_APP_NOTES,    CONTEXT_MENU_STYLE_NORMAL, ctx_open_notes,    0 },
    { "Open Files",    ICON_APP_FILES,    CONTEXT_MENU_STYLE_NORMAL, ctx_open_files,    0 },
    { "Search Apps",   ICON_SYM_SEARCH,   CONTEXT_MENU_STYLE_NORMAL, ctx_open_search,   0 },
    { "Refresh",       ICON_NONE,         CONTEXT_MENU_STYLE_NORMAL, ctx_refresh,       0 },
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

    if (start_popup_visible()) {
        if (start_popup_handle_pointer(mx, my, evt->pointer.pressed, evt->pointer.released)) {
            g_shell_focus = SHELL_FOCUS_START;
            *dirty = 1;
            return;
        }
    }

    /* Right-click (released) → show context menu */
    if (evt->pointer.released & 0x02u) {
        int hit = hit_test_window(mx, my);
        if (hit >= 0) {
            gui_window_t *w = &g_windows[hit];
            if (in_title_bar(w, mx, my)) {
                static context_menu_item_t win_menu[1];
                win_menu[0].label    = "Close Window";
                win_menu[0].icon_id  = ICON_SYM_CLOSE;
                win_menu[0].style    = CONTEXT_MENU_STYLE_DANGER;
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

    if ((evt->pointer.released & 0x01u) && g_start_open) {
        if (hit_test_start_power_btn(mx, my)) {
            start_popup_open(START_POPUP_POWER, start_power_footer_rect(),
                             k_start_power_items,
                             (int)(sizeof(k_start_power_items) / sizeof(k_start_power_items[0])));
            *dirty = 1;
            return;
        }
        if (hit_test_start_user_area(mx, my)) {
            start_popup_open(START_POPUP_USER, start_user_footer_rect(),
                             k_start_user_items,
                             (int)(sizeof(k_start_user_items) / sizeof(k_start_user_items[0])));
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
            if (nw < window_min_w(rw)) nw = window_min_w(rw);
            if (nh < window_min_h(rw)) nh = window_min_h(rw);
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
        int was_open = g_start_open;
        start_popup_close();
        start_menu_set_open(!was_open);
        if (was_open) {
            g_shell_focus = (g_focus >= 0) ? SHELL_FOCUS_WINDOW : SHELL_FOCUS_DESKTOP;
        } else {
            g_shell_focus = SHELL_FOCUS_START;
        }
        *dirty = 1;
        return;
    }

    if (g_start_open) {
        int vi = hit_test_start_app_cell(mx, my);
        if (vi >= 0) {
            int ai = start_menu_app_index(vi);
            start_popup_close();
            start_menu_set_open(0);
            if (ai >= 0) gui_launch_app(ai);
            clamp_shell_state();
            *dirty = 1;
            return;
        }
        {
            int quick = hit_test_start_quick_card(mx, my);
            if (quick >= 0) {
                int ai = start_quick_app_index(quick);
                start_popup_close();
                start_menu_set_open(0);
                if (ai >= 0) gui_launch_app(ai);
                clamp_shell_state();
                *dirty = 1;
                return;
            }
        }
        if (hit_test_start_search_box(mx, my)) {
            search_overlay_set_open(1);
            g_search_buf[0] = '\0';
            g_search_len = 0;
            g_search_sel = 0;
            g_shell_focus = SHELL_FOCUS_START;
            *dirty = 1;
            return;
        }
        if (hit_test_start_power_btn(mx, my) || hit_test_start_user_area(mx, my)) {
            g_shell_focus = SHELL_FOCUS_START;
            *dirty = 1;
            return;
        }
        {
            int sx;
            int sy;
            int sw;
            int sh;
            start_menu_draw_bounds(&sx, &sy, &sw, &sh, 0);
            if (mx >= sx && mx < sx + sw &&
                my >= sy && my < sy + sh) {
                g_shell_focus = SHELL_FOCUS_START;
                *dirty = 1;
                return;
            }
        }
    }

    {
        int taskbar_win = hit_test_taskbar_button(mx, my);
        if (taskbar_win >= 0) {
            start_popup_close();
            start_menu_set_open(0);
            if (g_windows[taskbar_win].minimized) {
                g_windows[taskbar_win].minimized = 0;
                begin_window_intro(taskbar_win);
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
            start_popup_close();
            start_menu_set_open(0);
            bring_to_front(hit);
            if (in_close_button(w, mx, my)) {
                gui_window_close(hit);
            } else if (in_maximize_button(w, mx, my)) {
                set_window_maximized(hit, !w->maximized);
                *dirty = 1;
            } else if (in_minimize_button(w, mx, my)) {
                w->minimized = 1;
                clear_focus();
                *dirty = 1;
            } else if (in_resize_handle(w, mx, my)) {
                gui_rect_t visual = window_visual_frame(hit);
                g_resize_win = hit;
                w->resize_start_mx = mx;
                w->resize_start_my = my;
                w->resize_orig_w   = visual.w;
                w->resize_orig_h   = visual.h;
            } else if (!w->maximized && in_title_bar(w, mx, my)) {
                gui_rect_t visual = window_visual_frame(hit);
                w->dragging = 1;
                w->drag_off_x = mx - visual.x;
                w->drag_off_y = my - visual.y;
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
            start_popup_close();
            start_menu_set_open(0);
            clear_focus();
            g_shell_focus = SHELL_FOCUS_DESKTOP;
            activate_desktop_icon_click(icon);
            *dirty = 1;
            return;
        }
    }

    start_popup_close();
    start_menu_set_open(0);
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

        if (g_pending_power == PWR_SHUTDOWN) {
            g_pending_power = PWR_NONE;
            do_shutdown();
            return;
        }
        if (g_pending_power == PWR_REBOOT) {
            g_pending_power = PWR_NONE;
            do_reboot();
            return;
        }

        if (dirty) {
            gui_repaint();
        } else if (cursor_only) {
            present_cursor_overlay(evt.pointer.x, evt.pointer.y);
        }
    }
}
