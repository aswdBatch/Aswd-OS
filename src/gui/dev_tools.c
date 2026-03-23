#include "gui/dev_tools.h"

#include <stdint.h>

#include "cpu/bugcheck.h"
#include "diagnostics/diagnostics.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"

#define COL_BG      gfx_rgb(250, 251, 254)
#define COL_TEXT    gfx_rgb(31, 41, 55)
#define COL_DIM     gfx_rgb(100, 116, 139)
#define COL_SEL_BG  gfx_rgb(37, 99, 235)
#define COL_SEL_TXT gfx_rgb(255, 255, 255)
#define COL_RULE    gfx_rgb(203, 213, 225)

static int g_win_id = -1;
static int g_sel = 0;
static int g_force_confirm = 0;
static char g_msg[96];

static const char *g_items[3] = {
    "Smoke Test",
    "Temp Write",
    "Keyboard Info",
};

static void set_msg(const char *msg) {
    str_copy(g_msg, msg ? msg : "", sizeof(g_msg));
}

static void run_selected(void) {
    diagnostic_test_mode_t mode = DIAGNOSTIC_TEST_SMOKE;
    if (g_sel == 1) mode = DIAGNOSTIC_TEST_TEMP_WRITE;
    else if (g_sel == 2) mode = DIAGNOSTIC_TEST_KEYBOARD;
    set_msg("Running...");
    if (diagnostics_run_test(mode)) set_msg("Test passed.");
    else set_msg("Test failed.");
}

static void dev_tools_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int i;

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    gfx_draw_string(r.x + 12, r.y + 10, "Diagnostics", COL_DIM, COL_BG);
    gfx_fill_rect(r.x + 8, r.y + 30, r.w - 16, 1, COL_RULE);

    for (i = 0; i < 3; i++) {
        int y = r.y + 40 + i * 28;
        uint32_t bg = (i == g_sel) ? COL_SEL_BG : COL_BG;
        uint32_t fg = (i == g_sel) ? COL_SEL_TXT : COL_TEXT;
        gfx_fill_rect(r.x + 12, y - 2, r.w - 24, 22, bg);
        gfx_draw_string(r.x + 18, y + 2, g_items[i], fg, bg);
    }

    gfx_draw_string(r.x + 12, r.y + 40 + 3 * 28 + 8, "[Enter] Run selected test", COL_DIM, COL_BG);

    {
        int dy = r.y + 40 + 3 * 28 + 36;
        uint32_t fc_bg;
        th_draw_section_header(r.x + 8, dy, r.w - 16, "Danger Zone", TH_DANGER_BG);
        dy += 24;
        fc_bg = (g_sel == 3) ? TH_DANGER_BG : gfx_rgb(60, 20, 20);
        gfx_fill_rect(r.x + 12, dy - 2, r.w - 24, 22, fc_bg);
        gfx_draw_string(r.x + 18, dy + 2,
            g_force_confirm ? "Force Crash  [Enter=yes, Esc=cancel]" : "Force Crash",
            TH_DANGER_TEXT, fc_bg);
    }

    gfx_draw_string(r.x + 12, r.y + r.h - 26, g_msg, COL_DIM, COL_BG);
}

static void dev_tools_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }
    if (key == KEY_UP && g_sel > 0) { g_sel--; g_force_confirm = 0; return; }
    if (key == KEY_DOWN && g_sel < 3) { g_sel++; g_force_confirm = 0; return; }
    if (key == '\r' || key == '\n') {
        if (g_sel < 3) { run_selected(); return; }
        if (!g_force_confirm) { g_force_confirm = 1; return; }
        bugcheck("MANUALLY_INITIATED_CRASH", "Triggered from Dev Tools");
    }
    if (key == 0x1B) { g_force_confirm = 0; return; }
}

static void dev_tools_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    gui_rect_t r;
    int i;
    (void)x;
    if (win_id != g_win_id) return;
    if (!(buttons & 1)) return;
    r = gui_window_content(g_win_id);
    (void)r;
    for (i = 0; i < 3; i++) {
        int row_y = 40 + i * 28 - 2;
        if (y >= row_y && y < row_y + 22) {
            if (g_sel == i) {
                run_selected(); /* second click = run */
            } else {
                g_sel = i;
            }
            return;
        }
    }
}

static void dev_tools_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void dev_tools_launch(void) {
    gui_window_t *w;
    gui_rect_t rect;
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_window_suggest_rect(460, 340, &rect);
    g_win_id = gui_window_create("Dev Tools", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;
    g_sel = 0;
    set_msg("Select a test and press Enter or click twice.");
    w = gui_get_window(g_win_id);
    w->on_paint = dev_tools_on_paint;
    w->on_key   = dev_tools_on_key;
    w->on_mouse = dev_tools_on_mouse;
    w->on_close = dev_tools_on_close;
}
