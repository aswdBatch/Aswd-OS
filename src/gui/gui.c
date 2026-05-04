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
#define COL_DESKTOP_TXT      gfx_rgb(44, 62, 84)
#define COL_ICON_SEL         gfx_rgb(214, 230, 249)
#define COL_ICON_SEL_ACTIVE  gfx_rgb(59, 130, 246)
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
/* Logical pointer = hotspot; sprite drawn offset so tip aligns with (mx,my). */
#define CURSOR_HOTSPOT_X 0
#define CURSOR_HOTSPOT_Y 0

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
    { GUI_BG_THEME_MINT, "mint", "Mint",      GUI_RGB(240, 247, 245), GUI_RGB(225, 236, 241),
      GUI_RGB(177, 225, 212), GUI_RGB(228, 247, 241), GUI_RGB(168, 214, 201),
      GUI_RGB(241, 245, 250), GUI_RGB(228, 234, 242),
      GUI_RGB(88, 145, 204), GUI_RGB(62, 118, 182),
      GUI_RGB(114, 167, 223), GUI_RGB(77, 131, 194),
      GUI_RGB(212, 220, 232), GUI_RGB(188, 198, 211), GUI_RGB(220, 234, 230) },
    { GUI_BG_THEME_GLASS, "glass", "Blue Glass", GUI_RGB(238, 245, 252), GUI_RGB(224, 233, 245),
      GUI_RGB(183, 215, 249), GUI_RGB(233, 242, 254), GUI_RGB(171, 203, 238),
      GUI_RGB(241, 245, 250), GUI_RGB(227, 233, 242),
      GUI_RGB(79, 132, 198), GUI_RGB(56, 108, 176),
      GUI_RGB(108, 158, 222), GUI_RGB(72, 123, 188),
      GUI_RGB(212, 220, 232), GUI_RGB(188, 198, 211), GUI_RGB(224, 232, 244) },
    { GUI_BG_THEME_STUDIO, "studio", "Studio", GUI_RGB(245, 242, 252), GUI_RGB(234, 230, 244),
      GUI_RGB(218, 201, 244), GUI_RGB(245, 239, 251), GUI_RGB(209, 193, 236),
      GUI_RGB(241, 245, 250), GUI_RGB(227, 233, 242),
      GUI_RGB(111, 126, 214), GUI_RGB(88, 102, 182),
      GUI_RGB(138, 150, 226), GUI_RGB(104, 116, 194),
      GUI_RGB(212, 220, 232), GUI_RGB(188, 198, 211), GUI_RGB(232, 228, 244) },
    { GUI_BG_THEME_SUNSET, "sunset", "Sunset", GUI_RGB(252, 244, 238), GUI_RGB(244, 231, 226),
      GUI_RGB(245, 206, 179), GUI_RGB(252, 239, 232), GUI_RGB(237, 191, 170),
      GUI_RGB(241, 245, 250), GUI_RGB(227, 233, 242),
      GUI_RGB(193, 128, 106), GUI_RGB(168, 104, 92),
      GUI_RGB(218, 150, 127), GUI_RGB(186, 118, 98),
      GUI_RGB(214, 220, 228), GUI_RGB(192, 199, 210), GUI_RGB(244, 228, 220) },
    { GUI_BG_THEME_OCEAN, "ocean", "Ocean",   GUI_RGB(238, 248, 249), GUI_RGB(224, 238, 241),
      GUI_RGB(180, 226, 228), GUI_RGB(232, 247, 247), GUI_RGB(166, 214, 217),
      GUI_RGB(241, 245, 250), GUI_RGB(227, 233, 242),
      GUI_RGB(72, 145, 176), GUI_RGB(52, 119, 154),
      GUI_RGB(101, 171, 199), GUI_RGB(66, 136, 168),
      GUI_RGB(212, 220, 232), GUI_RGB(188, 198, 211), GUI_RGB(223, 238, 239) },
    { GUI_BG_THEME_NEUTRAL, "neutral", "Neutral", GUI_RGB(242, 245, 250), GUI_RGB(229, 234, 242),
      GUI_RGB(208, 216, 230), GUI_RGB(241, 245, 251), GUI_RGB(198, 208, 224),
      GUI_RGB(241, 245, 250), GUI_RGB(227, 233, 242),
      GUI_RGB(85, 128, 188), GUI_RGB(62, 103, 162),
      GUI_RGB(112, 152, 210), GUI_RGB(77, 119, 176),
      GUI_RGB(212, 220, 232), GUI_RGB(188, 198, 211), GUI_RGB(230, 234, 242) },
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
/** Pointer move already issued gfx_invalidate_rect union for drag/resize */
static int g_gui_pointer_narrow_inv = 0;
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
static int g_repaint_active = 0;
static uint32_t g_last_cursor_present_tick = 0;

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
static int start_popup_visible(void);
static void start_menu_draw_bounds(int *out_x, int *out_y, int *out_w, int *out_h, uint8_t *out_alpha);
static gui_rect_t start_popup_visual_rect(void);

static void gui_refresh_shell_metrics(void) {
    const gfx_display_profile_t *dp = gfx_display_profile();

    if (dp->density == GFX_DENSITY_COMPACT) {
        g_shell_metrics.title_bar_h = 24;
        g_shell_metrics.taskbar_h = 30;
        g_shell_metrics.resize_handle = 10;
        g_shell_metrics.window_min_w = 220;
        g_shell_metrics.window_min_h = 160;
        g_shell_metrics.desktop_margin_x = 14;
        g_shell_metrics.desktop_margin_y = 14;
        g_shell_metrics.desktop_slot_w = 84;
        g_shell_metrics.desktop_slot_h = 66;
        g_shell_metrics.desktop_gap_x = 12;
        g_shell_metrics.desktop_gap_y = 4;
        g_shell_metrics.desktop_icon_size = 26;
        g_shell_metrics.start_button_w = 58;
        g_shell_metrics.start_menu_w = 320;
        g_shell_metrics.start_header_h = 38;
        g_shell_metrics.start_footer_h = 48;
        g_shell_metrics.start_cell_w = 88;
        g_shell_metrics.start_cell_h = 50;
        g_shell_metrics.search_w = 440;
        g_shell_metrics.search_h = 332;
    } else if (dp->density == GFX_DENSITY_NORMAL) {
        g_shell_metrics.title_bar_h = 26;
        g_shell_metrics.taskbar_h = 34;
        g_shell_metrics.resize_handle = 12;
        g_shell_metrics.window_min_w = 260;
        g_shell_metrics.window_min_h = 190;
        g_shell_metrics.desktop_margin_x = 18;
        g_shell_metrics.desktop_margin_y = 16;
        g_shell_metrics.desktop_slot_w = 92;
        g_shell_metrics.desktop_slot_h = 70;
        g_shell_metrics.desktop_gap_x = 14;
        g_shell_metrics.desktop_gap_y = 4;
        g_shell_metrics.desktop_icon_size = 28;
        g_shell_metrics.start_button_w = 62;
        g_shell_metrics.start_menu_w = 344;
        g_shell_metrics.start_header_h = 40;
        g_shell_metrics.start_footer_h = 50;
        g_shell_metrics.start_cell_w = 94;
        g_shell_metrics.start_cell_h = 52;
        g_shell_metrics.search_w = 500;
        g_shell_metrics.search_h = 356;
    } else {
        g_shell_metrics.title_bar_h = 28;
        g_shell_metrics.taskbar_h = 38;
        g_shell_metrics.resize_handle = 14;
        g_shell_metrics.window_min_w = 300;
        g_shell_metrics.window_min_h = 220;
        g_shell_metrics.desktop_margin_x = 22;
        g_shell_metrics.desktop_margin_y = 16;
        g_shell_metrics.desktop_slot_w = 104;
        g_shell_metrics.desktop_slot_h = 76;
        g_shell_metrics.desktop_gap_x = 14;
        g_shell_metrics.desktop_gap_y = 6;
        g_shell_metrics.desktop_icon_size = 32;
        g_shell_metrics.start_button_w = 68;
        g_shell_metrics.start_menu_w = 372;
        g_shell_metrics.start_header_h = 42;
        g_shell_metrics.start_footer_h = 54;
        g_shell_metrics.start_cell_w = 102;
        g_shell_metrics.start_cell_h = 56;
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
    { "work180",   "180 Work",      "180 Work",  GUI_ICON_WORK180,  0, 1, 0, 0, work_gui_launch },
    { "store",     "App Store",     "AppStore",  GUI_ICON_STORE,    0, 1, 0, 0, appstore_gui_launch },
    { "osinfo",    "OS Info",       "OS Info",   GUI_ICON_OSINFO,   1, 1, 0, 0, osinfo_gui_launch },
    { "ctrlpanel", "Control Panel", "CtrlPanel", GUI_ICON_SETTINGS, 1, 1, 0, 0, settings_gui_launch },
    { "taskmgr",   "Task Manager",  "TaskMgr",   GUI_ICON_TASKMGR,  1, 1, 0, 0, taskmgr_launch },
    /* Apps below live in the App Store, not on the desktop */
    { "snake",     "Snake",         "Snake",     GUI_ICON_SNAKE,    0, 1, 1, 0, snake_gui_launch },
    { "calc",      "Calculator",    "Calc",      GUI_ICON_CALC,     0, 1, 1, 0, calc_gui_launch },
    { "browser",   "Browser",       "Browser",   GUI_ICON_BROWSER,  0, 1, 1, 1, browser_gui_launch },
    { "axstudio",  "AX Studio",     "AXStudio",  GUI_ICON_AXSTUDIO, 0, 1, 0, 0, axstudio_gui_launch },
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
    int sx = mx - CURSOR_HOTSPOT_X;
    int sy = my - CURSOR_HOTSPOT_Y;
    if (g_cursor_drawn) {
        gfx_present_rect(g_cursor_x, g_cursor_y, CURSOR_W, CURSOR_H);
    }
    gfx_present_rect(sx, sy, CURSOR_W, CURSOR_H);
    draw_cursor_front(sx, sy);
    g_cursor_x = sx;
    g_cursor_y = sy;
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
    y = area.y + (area.h - h) / 4;
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

static gui_rect_t search_overlay_rect(void) {
    gui_rect_t rect;
    uint8_t alpha = shell_anim_alpha(&g_search_anim);
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();

    rect.w = g_shell_metrics.search_w;
    rect.h = g_shell_metrics.search_h;
    rect.x = (sw - rect.w) / 2;
    rect.y = (sh - rect.h) / 3 + th_lerp_int(14, 0, alpha);
    return rect;
}

static void gui_invalidate_padded_rect(gui_rect_t rect, int pad) {
    gfx_invalidate_rect(rect.x - pad, rect.y - pad, rect.w + pad * 2, rect.h + pad * 2);
}

static void gui_invalidate_taskbar(void) {
    gfx_invalidate_rect(0, gfx_height() - TASKBAR_HEIGHT, gfx_width(), TASKBAR_HEIGHT);
}

static void gui_invalidate_shell_overlays(void) {
    if (start_menu_visible()) {
        gui_rect_t rect;
        start_menu_draw_bounds(&rect.x, &rect.y, &rect.w, &rect.h, 0);
        gui_invalidate_padded_rect(rect, 24);
    }
    if (start_popup_visible()) {
        gui_invalidate_padded_rect(start_popup_visual_rect(), 20);
    }
    if (search_overlay_visible()) {
        gui_invalidate_padded_rect(search_overlay_rect(), 24);
        gfx_invalidate_rect(0, 0, gfx_width(), gfx_height() - TASKBAR_HEIGHT);
    }
    gui_invalidate_taskbar();
}

static void gui_invalidate_motion_regions(void) {
    int any_window_motion = 0;

    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (!g_window_intro[i].active || !g_windows[i].active || g_windows[i].minimized) continue;
        gui_invalidate_padded_rect(window_visual_frame(i), 16);
        any_window_motion = 1;
    }

    if (g_start_anim.active || g_start_popup.anim.active || g_search_anim.active) {
        gui_invalidate_shell_overlays();
    }
    if (any_window_motion) {
        gui_invalidate_taskbar();
    }
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
    gfx_fill_rect_alpha(0, 0, sw, base_h / 2, gfx_rgb(255, 255, 255), 18);
    draw_soft_blob(sw / 5, base_h / 5, sw / 8, style->glow, 26);
    draw_soft_blob(sw - sw / 6, base_h / 3, sw / 9, style->band_a, 22);
    draw_soft_blob(sw / 2, base_h * 3 / 5, sw / 7, style->band_b, 16);
    draw_diagonal_ribbon(sw, base_h / 8, base_h / 14, sw / 8, style->band_a, 10);
    draw_diagonal_ribbon(sw, base_h / 2, base_h / 12, sw / 10, style->band_b, 8);
    gfx_fill_rect_alpha(0, base_h - base_h / 4, sw, base_h / 4, gfx_rgb(255, 255, 255), 10);
    if (auth_mode) {
        gfx_fill_rect_alpha(0, 0, sw, base_h, style->auth_overlay, 42);
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
    gfx_fill_rect_alpha(0, dh, sw, 1, gfx_rgb(255, 255, 255), 150);
}

/* ---- New start menu helpers ---- */
typedef struct {
    const char *app_id;
    const char *label;
} start_quick_item_t;

static const start_quick_item_t k_start_quick_items[] = {
    { "files",     "Files" },
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

static const char *start_quick_app_label(int vis) {
    int seen = 0;

    for (int i = 0; i < (int)(sizeof(k_start_quick_items) / sizeof(k_start_quick_items[0])); i++) {
        int app_idx = app_index_by_id(k_start_quick_items[i].app_id);
        if (!start_menu_app_visible(app_idx)) continue;
        if (seen == vis) return k_start_quick_items[i].label;
        seen++;
    }
    return "";
}

static int start_menu_row_h(void) {
    return th_metrics()->list_row_h + 4;
}

static int start_menu_card_h(void) {
    return (th_metrics()->font_body * 2) + th_metrics()->gap_sm + 6;
}

static int start_menu_header_h(void) {
    const th_metrics_t *tm = th_metrics();
    return tm->font_title + tm->font_small + tm->gap_sm + 10;
}

static int start_menu_h(void) {
    const th_metrics_t *tm = th_metrics();
    int app_area_h = start_menu_app_count() * start_menu_row_h();
    int quick_count = start_quick_app_count();
    int quick_area_h = quick_count > 0
                     ? (quick_count * start_menu_card_h()) + ((quick_count - 1) * tm->gap_sm)
                     : 0;
    int body_h = app_area_h;
    int header_h = start_menu_header_h();

    if (body_h < quick_area_h) body_h = quick_area_h;
    body_h += tm->font_small + tm->gap_sm + 6;
    return tm->gap_lg + header_h + tm->gap_sm + tm->field_h + tm->gap_md
         + body_h + START_FOOTER_H + tm->gap_lg;
}

static int start_menu_x(void) { return 10; }
static gui_rect_t start_power_footer_rect(void);

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
    int quick_w = start_quick_app_count() > 0 ? START_CELL_W : 0;
    int quick_gap = quick_w > 0 ? tm->gap_md : 0;

    r.x = sr.x;
    r.y = sr.y + sr.h + tm->gap_md + tm->font_small + tm->gap_sm;
    r.w = START_MENU_W - (tm->gap_lg * 2) - quick_gap - quick_w;
    r.h = footer_y - r.y - tm->gap_md;
    return r;
}

static gui_rect_t start_quick_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t list = start_list_rect();
    gui_rect_t r;
    gui_rect_t search = start_search_rect();

    if (start_quick_app_count() <= 0) {
        r.x = list.x + list.w;
        r.y = list.y;
        r.w = 0;
        r.h = 0;
        return r;
    }

    r.x = list.x + list.w + tm->gap_md;
    r.y = list.y;
    r.w = search.x + search.w - r.x;
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
    gui_rect_t power = start_power_footer_rect();

    r.x = x + tm->gap_lg;
    r.y = footer_y + 7;
    r.w = power.x - r.x - tm->gap_sm;
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

    r.w = 34;
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
    int row = vis;
    int cell_w = rail.w;
    int cell_h = start_menu_card_h();

    r.x = rail.x;
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
    th_draw_soft_shadow(rect.x, rect.y, rect.w, rect.h, 16);
    th_fill_rounded_alpha(rect.x, rect.y, rect.w, rect.h, 16, TH_BG_CARD, alpha);
    th_draw_rounded_outline(rect.x, rect.y, rect.w, rect.h, 16, TH_BORDER);

    for (int i = 0; i < g_start_popup.count; i++) {
        int iy = rect.y + pad_y + i * row_h;
        uint32_t row_bg = (i & 1) ? TH_BG_CARD_ALT : TH_BG_CARD;
        uint32_t hover_bg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(180, 52, 72) : TH_ACCENT_HOT;
        uint32_t fg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(153, 27, 27) : TH_TEXT;
        uint32_t icon_fg = (g_start_popup.items[i].style == CONTEXT_MENU_STYLE_DANGER) ? gfx_rgb(153, 27, 27) : TH_TEXT_DIM;
        int tx = rect.x + pad_x;

        if (i == g_start_popup.hover) {
            row_bg = hover_bg;
            fg = TH_TEXT_INVERT;
            icon_fg = TH_TEXT_INVERT;
        }

        th_fill_rounded(rect.x + 4, iy, rect.w - 8, row_h, 10, row_bg);
        if (g_start_popup.items[i].icon_id != ICON_NONE) {
            icon_draw(rect.x + pad_x, iy + (row_h - m->font_body) / 2, m->font_body,
                      g_start_popup.items[i].icon_id, icon_fg);
            tx += icon_slot;
        }
        gfx_draw_string_role(tx, iy + (row_h - m->font_body) / 2,
                             g_start_popup.items[i].label, FONT_ROLE_UI, m->font_body, fg, row_bg);
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
    int icon_sz = (gfx_display_profile()->density == GFX_DENSITY_COMFORTABLE) ? 24 : 22;
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

    th_draw_soft_shadow(x, y, w, h, 20);
    th_fill_rounded_alpha(x, y, w, h, 24, TH_BG_CARD, alpha);
    th_draw_rounded_outline(x, y, w, h, 24, TH_BORDER);
    th_fill_rounded_alpha(x + 1, y + 1, w - 2, 6, 23, style->accent_top, 120);
    greet[0] = '\0';
    str_copy(greet, "Welcome, ", sizeof(greet));
    str_cat(greet, uname, sizeof(greet));
    gfx_draw_string_role(x + tm->gap_lg, y + 12,
                         greet, FONT_ROLE_UI, tm->font_title, TH_TEXT, TH_BG_CARD);
    gfx_draw_string_role(x + tm->gap_lg, y + 12 + tm->font_title + 2,
                         "Apps, shortcuts, and session controls",
                         FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM, TH_BG_CARD);

    th_draw_field(search_rect.x, search_rect.y, search_rect.w, "", 0, 0);
    icon_draw(search_rect.x + 8, search_rect.y + (search_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_SEARCH, TH_TEXT_DIM);
    str_copy(search_hint, g_search_active && g_search_len > 0 ? g_search_buf : "Search apps...", sizeof(search_hint));
    gfx_draw_string_role(search_rect.x + 8 + tm->font_body + 8,
                         search_rect.y + (search_rect.h - tm->font_body) / 2,
                         search_hint, FONT_ROLE_UI, tm->font_body,
                         (g_search_active && g_search_len > 0) ? TH_TEXT : TH_TEXT_DIM, TH_BG_FIELD);

    gfx_draw_string_role(list_rect.x, list_rect.y - tm->font_small - 4,
                         "Apps", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM, TH_BG_CARD);
    if (quick_rect.w > 0) {
        gfx_draw_string_role(quick_rect.x, quick_rect.y - tm->font_small - 4,
                             "Quick Access", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM, TH_BG_CARD);
    }

    {
        int n = start_menu_app_count();
        for (int vi = 0; vi < n; vi++) {
            int ai = start_menu_app_index(vi);
            int row_y = list_rect.y + vi * start_menu_row_h();
            int selected = (g_start_sel == vi);
            uint32_t row_bg = selected ? TH_SEL_BG : TH_BG_CARD;
            uint32_t icon_fg = selected ? style->accent_bottom : TH_TEXT_DIM;

            th_draw_list_row(list_rect.x, row_y, list_rect.w, start_menu_row_h(), "", selected);
            th_fill_rounded(list_rect.x + 1, row_y + 1, list_rect.w - 2, start_menu_row_h() - 2, 11, row_bg);
            icon_draw(list_rect.x + tm->gap_sm, row_y + (start_menu_row_h() - icon_sz) / 2,
                      icon_sz, app_icon_asset(g_apps[ai].icon_kind), 0);
            gfx_draw_string_role(list_rect.x + tm->gap_sm + icon_sz + tm->gap_sm,
                                 row_y + 7,
                                 g_apps[ai].label, FONT_ROLE_UI, tm->font_body, TH_TEXT, row_bg);
            gfx_draw_string_role(list_rect.x + tm->gap_sm + icon_sz + tm->gap_sm,
                                 row_y + 7 + tm->font_body + 1,
                                 g_apps[ai].id, FONT_ROLE_UI, tm->font_small,
                                 selected ? icon_fg : TH_TEXT_DIM, row_bg);
        }
    }

    {
        int quick_count = start_quick_app_count();
        for (int qi = 0; qi < quick_count; qi++) {
            int ai = start_quick_app_index(qi);
            gui_rect_t qr = start_quick_card_rect(qi);
            const char *quick_label = start_quick_app_label(qi);
            uint32_t card_bg = (qi == 0) ? gfx_rgb(243, 247, 254) : TH_BG_CARD;
            th_draw_card(qr.x, qr.y, qr.w, qr.h, 0, card_bg, 0);
            icon_draw(qr.x + 10, qr.y + 10, tm->font_body + 6,
                      app_icon_asset(g_apps[ai].icon_kind), 0);
            gfx_draw_string_role_transparent(qr.x + 10, qr.y + qr.h - tm->font_body - 14,
                                             quick_label, FONT_ROLE_UI, tm->font_body, TH_TEXT);
            gfx_draw_string_role_transparent(qr.x + 10, qr.y + qr.h - tm->font_small - 6,
                                             "Open", FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM);
        }
    }

    gfx_fill_rect(x + 16, footer_y, w - 32, 1, TH_RULE);

    th_fill_rounded(user_rect.x, user_rect.y, user_rect.w, user_rect.h, 13, TH_BORDER);
    th_fill_rounded(user_rect.x + 1, user_rect.y + 1, user_rect.w - 2, user_rect.h - 2, 12, TH_BG_CARD_ALT);
    icon_draw(user_rect.x + 8, user_rect.y + (user_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_USER, TH_TEXT_DIM);
    {
        char un[20];
        int line_h = gfx_font_line_height(FONT_ROLE_UI, tm->font_small);
        clip_title(un, sizeof(un), uname, 16);
        gfx_draw_string_role(user_rect.x + 8 + tm->font_body + 10,
                             user_rect.y + 6,
                             un, FONT_ROLE_UI, tm->font_body, TH_TEXT, TH_BG_CARD_ALT);
        gfx_draw_string_role(user_rect.x + 8 + tm->font_body + 10,
                             user_rect.y + 6 + line_h + 1,
                             users_current_is_admin() ? "admin" : "user",
                             FONT_ROLE_UI, tm->font_small, TH_TEXT_DIM, TH_BG_CARD_ALT);
    }

    th_fill_rounded(power_rect.x, power_rect.y, power_rect.w, power_rect.h, 13, TH_BORDER);
    th_fill_rounded(power_rect.x + 1, power_rect.y + 1, power_rect.w - 2, power_rect.h - 2, 12, TH_BG_CARD_ALT);
    icon_draw(power_rect.x + (power_rect.w - tm->font_body) / 2,
              power_rect.y + (power_rect.h - tm->font_body) / 2,
              tm->font_body, ICON_SYM_POWER, TH_TEXT_DIM);
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
            th_fill_rounded(slot.x + 10, slot.y + 6, slot.w - 20, slot.h - 18, 16, plate_bg);
            if (active) {
                th_draw_rounded_outline(slot.x + 10, slot.y + 6, slot.w - 20, slot.h - 18, 16,
                                        gfx_rgb(29, 78, 216));
            }
        }

        tile_x = slot.x + (slot.w - DESKTOP_ICON_SIZE) / 2;
        tile_y = slot.y + 8;
        if (!selected) {
            th_fill_rounded_alpha(tile_x + 1, tile_y + 2, DESKTOP_ICON_SIZE, DESKTOP_ICON_SIZE, 10, gfx_rgb(15, 23, 42), 4);
        }
        draw_app_icon(app->icon_kind, tile_x, tile_y, DESKTOP_ICON_SIZE);

        label_fg = active ? COL_ICON_TILE_ACTIVE : COL_DESKTOP_TXT;
        label_y = tile_y + DESKTOP_ICON_SIZE + 7;
        text_x = slot.x + (slot.w - text_width(label)) / 2;
        if (text_x < slot.x + 2) text_x = slot.x + 2;
        if (selected) {
            uint32_t plate_bg = active ? gfx_rgb(29, 78, 216) : COL_ICON_SEL;
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
    int btn_h = TASKBAR_HEIGHT - 10;
    int btn_y = tb_y + (TASKBAR_HEIGHT - btn_h) / 2;
    int x = START_BUTTON_X + START_BUTTON_W + 8;
    int start_hot = g_start_open || start_popup_visible() ||
                    (g_focus < 0 && g_shell_focus == SHELL_FOCUS_START);

    gfx_fill_rect_gradient_v(0, tb_y, sw, TASKBAR_HEIGHT, style->taskbar_top, style->taskbar_bottom);
    gfx_fill_rect_alpha(0, tb_y, sw, TASKBAR_HEIGHT, gfx_rgb(255, 255, 255), 64);
    gfx_fill_rect_alpha(0, tb_y, sw, 1, gfx_rgb(255, 255, 255), 150);

    th_fill_rounded(START_BUTTON_X, btn_y, START_BUTTON_W, btn_h, 12,
                    start_hot ? gfx_rgb(229, 238, 251) : TH_BG_CARD);
    th_draw_rounded_outline(START_BUTTON_X, btn_y, START_BUTTON_W, btn_h, 12,
                            start_hot ? style->accent_bottom : TH_BORDER);
    icon_draw(START_BUTTON_X + 8, btn_y + (btn_h - tm->font_body) / 2, tm->font_body,
              ICON_SYM_SEARCH, start_hot ? TH_ACCENT_DARK : TH_TEXT_DIM);
    {
      uint32_t sb_bg = start_hot ? gfx_rgb(229, 238, 251) : TH_BG_CARD;
      uint32_t sb_fg = start_hot ? TH_ACCENT_DARK : TH_TEXT;
      draw_label(START_BUTTON_X + 8 + tm->font_body + 6,
                 tb_y + (TASKBAR_HEIGHT - tm->font_body) / 2, "Apps", sb_fg, sb_bg);
    }

    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        g_taskbar_slots[i].x = 0;
        g_taskbar_slots[i].w = 0;
        g_taskbar_slots[i].win_id = -1;
    }

    for (int i = 0; i < g_zcount; i++) {
        int win_id = g_zorder[i];
        int bw = 98;
        char title[18];
        int active = (win_id == g_focus) ||
                     (g_focus < 0 && g_shell_focus == SHELL_FOCUS_TASKBAR && i == g_taskbar_sel);
        uint32_t bottom = g_windows[win_id].minimized ? COL_WIN_MINIMIZED :
                          (active ? style->accent_bottom : TH_BG_CARD);
        if (x + bw >= sw - 90) break;
        g_taskbar_slots[i].x = x;
        g_taskbar_slots[i].w = bw;
        g_taskbar_slots[i].win_id = win_id;
        th_fill_rounded(x, btn_y, bw, btn_h, 12, active ? style->accent_bottom : TH_BORDER);
        th_fill_rounded(x + 1, btn_y + 1, bw - 2, btn_h - 2, 11, bottom);
        if (g_windows[win_id].icon_kind >= 0) {
            icon_draw(x + 8, btn_y + (btn_h - tm->font_body) / 2, tm->font_body,
                      app_icon_asset((gui_icon_kind_t)g_windows[win_id].icon_kind), 0);
        }
        clip_title(title, sizeof(title), gui_window_title(win_id), 12);
        draw_label(x + 8 + tm->font_body + 6,
                   btn_y + (btn_h - tm->font_body) / 2,
                   title, active ? gfx_rgb(255, 255, 255) : TH_TEXT, bottom);
        x += bw + 5;
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
        gfx_draw_string_role(ux, cy, un, FONT_ROLE_UI, tm->font_small,
                             TH_TEXT_ON_DARK_DIM, gfx_rgb(19, 23, 34));
        gfx_draw_string_role(cx, cy, cl, FONT_ROLE_UI, tm->font_small,
                             TH_TEXT_ON_DARK, gfx_rgb(19, 23, 34));
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
    int btn_h = TITLE_BAR_HEIGHT - 10;
    int btn_y = y + (TITLE_BAR_HEIGHT - btn_h) / 2;
    int btn_w = 18;
    int btn_gap = 4;
    int close_x = x + fw - btn_w - 8;
    int max_x = close_x - btn_gap - btn_w;
    int min_x = max_x - btn_gap - btn_w;

    outer = w->focused ? style->accent_bottom : TH_BORDER;
    if (!w->maximized) {
        th_fill_rounded_alpha(x + 2, y + 5, fw, fh, 18, gfx_rgb(15, 23, 42), 8);
    }
    th_fill_rounded(x, y, fw, fh, 18, outer);
    th_fill_rounded(x + 1, y + 1, fw - 2, fh - 2, 17, TH_BG_CARD);
    th_fill_rounded(x + 2, y + 2, fw - 4, TITLE_BAR_HEIGHT, 16,
                    w->focused ? gfx_rgb(232, 240, 252) : TH_BG_TOOLBAR);
    gfx_fill_rect(x + 16, y + TITLE_BAR_HEIGHT - 1, fw - 32, 1, TH_RULE);

    title_x = x + 8;
    if (w->icon_kind >= 0) {
        icon_draw(x + 6, y + (TITLE_BAR_HEIGHT - tm->font_body) / 2, tm->font_body,
                  app_icon_asset((gui_icon_kind_t)w->icon_kind), 0);
        title_x = x + 12 + tm->font_body;
    }
    {
      uint32_t tbg = w->focused ? gfx_rgb(232, 240, 252) : TH_BG_TOOLBAR;
      draw_label(title_x, y + (TITLE_BAR_HEIGHT - tm->font_body) / 2, w->title, TH_TEXT, tbg);
    }

    /* minimize button */
    th_fill_rounded(min_x, btn_y, btn_w, btn_h, 8, TH_BORDER);
    th_fill_rounded(min_x + 1, btn_y + 1, btn_w - 2, btn_h - 2, 7, TH_BG_CARD_ALT);
    icon_draw(min_x + (btn_w - tm->font_small) / 2, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              ICON_SYM_MINIMIZE, TH_TEXT_DIM);

    /* maximize/restore button */
    th_fill_rounded(max_x, btn_y, btn_w, btn_h, 8, TH_BORDER);
    th_fill_rounded(max_x + 1, btn_y + 1, btn_w - 2, btn_h - 2, 7, TH_BG_CARD_ALT);
    icon_draw(max_x + (btn_w - tm->font_small) / 2, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              w->maximized ? ICON_SYM_RESTORE : ICON_SYM_MAXIMIZE, TH_TEXT_DIM);

    /* close button */
    th_fill_rounded(close_x, btn_y, btn_w, btn_h, 8, gfx_rgb(235, 186, 195));
    th_fill_rounded(close_x + 1, btn_y + 1, btn_w - 2, btn_h - 2, 7, gfx_rgb(252, 241, 244));
    icon_draw(close_x + (btn_w - tm->font_small) / 2, btn_y + (btn_h - tm->font_small) / 2, tm->font_small,
              ICON_SYM_CLOSE, gfx_rgb(180, 52, 72));

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
    if (g_repaint_active) {
        return;
    }
    g_repaint_active = 1;
    gfx_begin_frame();

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

    if (g_drag_win >= 0 && g_windows[g_drag_win].dragging) {
        gui_rect_t vr = window_visual_frame(g_drag_win);
        th_draw_rounded_outline(vr.x - 2, vr.y - 2, vr.w + 4, vr.h + 4, 12,
                                gfx_rgb(59, 130, 246));
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

    if (!gfx_partial_present_enabled()) {
        gfx_swap();
    } else {
        gfx_present_dirty();
    }
    g_cursor_drawn = 0;
    present_cursor_overlay(mouse_x(), mouse_y());
    gfx_end_frame();
    g_repaint_active = 0;
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
    g_repaint_active = 0;
    g_last_cursor_present_tick = 0;
    gfx_set_partial_present(1);
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

    if (evt->pointer.dx != 0 || evt->pointer.dy != 0 || evt->pointer.changed != 0 ||
        evt->pointer.wheel != 0) {
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
            gui_rect_t prev_vis = window_visual_frame(g_resize_win);
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
            gui_invalidate_padded_rect(prev_vis, 24);
            gui_invalidate_padded_rect(window_visual_frame(g_resize_win), 24);
            gui_invalidate_taskbar();
            g_gui_pointer_narrow_inv = 1;
            *dirty = 1;
        } else {
            g_resize_win = -1;
            *dirty = 1;
        }
    }

    if (g_drag_win >= 0) {
        gui_window_t *dw = &g_windows[g_drag_win];
        if (evt->pointer.buttons & 0x01u) {
            gui_rect_t prev_vis = window_visual_frame(g_drag_win);
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
            gui_invalidate_padded_rect(prev_vis, 24);
            gui_invalidate_padded_rect(window_visual_frame(g_drag_win), 24);
            gui_invalidate_taskbar();
            g_gui_pointer_narrow_inv = 1;
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
    gfx_invalidate_full();
    gui_repaint();

    for (;;) {
        int dirty = 0;
        int cursor_only = 0;

        if (!input_try_get_event(&evt)) {
            if (run_idle_ticks(timer_get_ticks())) {
                gui_invalidate_motion_regions();
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
            int narrow_key = (evt.type == INPUT_EVENT_KEY && g_focus >= 0 &&
                              !start_menu_visible() && !start_popup_visible() &&
                              !search_overlay_visible() && !context_menu_active());
            if (narrow_key) {
                gui_invalidate_padded_rect(window_visual_frame(g_focus), 12);
                gui_invalidate_taskbar();
            } else if (!g_gui_pointer_narrow_inv) {
                gfx_invalidate_full();
            }
            g_gui_pointer_narrow_inv = 0;
            gui_repaint();
        } else if (cursor_only) {
            uint32_t now = timer_get_ticks();
            if (now != g_last_cursor_present_tick) {
                gfx_begin_frame();
                present_cursor_overlay(evt.pointer.x, evt.pointer.y);
                gfx_end_frame();
                g_last_cursor_present_tick = now;
            } else {
                gfx_mark_frame_coalesced();
            }
        }
    }
}
