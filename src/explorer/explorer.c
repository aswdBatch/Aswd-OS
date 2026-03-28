#include "explorer/explorer.h"

#include "common/colors.h"
#include "common/config.h"
#include "common/power.h"
#include "console/console.h"
#include "cpu/pic.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "editor/editor.h"
#include "input/input.h"
#include "settings/settings.h"
#include "lib/string.h"
#include "shell/shell.h"
#include "tui/tui.h"

/*
 * Legacy TUI hub — the fallback text-mode UI launched from boot options.
 *
 * Screen layout (80×25):
 *
 *   row  0  blue header "AswdOS v0.2"                 "Legacy TUI"
 *   row  1  dim menu bar (decorative)
 *   row  2  ─ separator ─
 *   row  3  (blank)
 *   row  4  ─── Applications ──────────────────────────────────────────
 *   row  5  (blank)
 *   rows 6–14  three app tiles  (20 wide × 9 tall)
 *   row 15  (blank)
 *   row 16  ─── Power Options ─────────────────────────────────────────
 *   row 17  (blank)
 *   rows 18–22  three power tiles  (20 wide × 5 tall)
 *   row 23  ← → ↑ ↓ navigate    Enter launch
 *   row 24  status bar — description of focused item
 */

/* ── Tile geometry ───────────────────────────────────────────────────────── */
#define APP_ROW    6
#define APP_H      9
#define APP_W      20
#define APP1_COL   5
#define APP2_COL   30   /* 5 + 20 + 5 gap */
#define APP3_COL   55   /* 30 + 20 + 5 gap */

#define PWR_ROW    18
#define PWR_H      5
#define PWR_W      20
#define PWR1_COL   5
#define PWR2_COL   30
#define PWR3_COL   55

/* ── Item indices ────────────────────────────────────────────────────────── */
#define ITEM_TERMINAL  0
#define ITEM_EDITOR    1
#define ITEM_SETTINGS  2
#define ITEM_SHUTDOWN  3
#define ITEM_REBOOT    4
#define ITEM_REBOOTUSB 5
#define ITEM_COUNT     6

static int g_sel = 0;

/* ── Drawing helpers ─────────────────────────────────────────────────────── */

static void fill_rect(int row, int col, int w, int h, char c, uint8_t color) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            vga_put_char_at(c, color, row + y, col + x);
}

/* Single-line or double-line box using CP437 box-drawing characters. */
static void draw_box(int row, int col, int w, int h, uint8_t color, int dbl) {
    char tl = dbl ? (char)0xC9 : (char)0xDA;
    char tr = dbl ? (char)0xBB : (char)0xBF;
    char bl = dbl ? (char)0xC8 : (char)0xC0;
    char br = dbl ? (char)0xBC : (char)0xD9;
    char hz = dbl ? (char)0xCD : (char)0xC4;
    char vt = dbl ? (char)0xBA : (char)0xB3;

    vga_put_char_at(tl, color, row,       col);
    vga_put_char_at(tr, color, row,       col + w - 1);
    vga_put_char_at(bl, color, row + h - 1, col);
    vga_put_char_at(br, color, row + h - 1, col + w - 1);
    for (int x = 1; x < w - 1; x++) {
        vga_put_char_at(hz, color, row,         col + x);
        vga_put_char_at(hz, color, row + h - 1, col + x);
    }
    for (int y = 1; y < h - 1; y++) {
        vga_put_char_at(vt, color, row + y, col);
        vga_put_char_at(vt, color, row + y, col + w - 1);
    }
}

/* Write text centered inside a region [inner_col, inner_col+inner_w). */
static void write_centered(int row, int inner_col, int inner_w,
                            const char *s, uint8_t color) {
    int len = (int)str_len(s);
    int off = (inner_w - len) / 2;
    if (off < 0) off = 0;
    tui_write_at(row, inner_col + off, s, color);
}

/* ── Tile drawing ────────────────────────────────────────────────────────── */

/*
 * App tile interior rows (relative to tile top row):
 *   +0  border
 *   +1  blank
 *   +2  icon row 1  (mini box top)
 *   +3  icon row 2  (mini box content)
 *   +4  icon row 3  (mini box bottom)
 *   +5  blank
 *   +6  app name  (bright when selected)
 *   +7  short description  (dim)
 *   +8  border
 */
static void draw_app_tile(int row, int col,
                           const char *icon1,
                           const char *icon2,
                           const char *icon3,
                           const char *name,
                           const char *desc,
                           int selected) {
    uint8_t bg        = VGA_COLOR_BLACK;
    uint8_t border_fg = selected ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_DARK_GREY;
    uint8_t icon_fg   = selected ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_DARK_GREY;
    uint8_t name_fg   = selected ? VGA_COLOR_WHITE       : VGA_COLOR_LIGHT_GREY;
    uint8_t desc_fg   = selected ? VGA_COLOR_LIGHT_GREY  : VGA_COLOR_DARK_GREY;

    uint8_t border_c = vga_make_color(border_fg, bg);
    uint8_t fill_c   = vga_make_color(VGA_COLOR_WHITE,  bg);

    fill_rect(row + 1, col + 1, APP_W - 2, APP_H - 2, ' ', fill_c);
    draw_box(row, col, APP_W, APP_H, border_c, selected);

    /* Icon: centered in the inner area */
    int in_col = col + 1;
    int in_w   = APP_W - 2;   /* 18 */
    int icon_w = (int)str_len(icon1);
    int icon_x = in_col + (in_w - icon_w) / 2;
    tui_write_at(row + 2, icon_x, icon1, vga_make_color(icon_fg, bg));
    tui_write_at(row + 3, icon_x, icon2, vga_make_color(icon_fg, bg));
    tui_write_at(row + 4, icon_x, icon3, vga_make_color(icon_fg, bg));

    write_centered(row + 6, in_col, in_w, name, vga_make_color(name_fg, bg));
    write_centered(row + 7, in_col, in_w, desc, vga_make_color(desc_fg, bg));
}

/*
 * Power tile interior (5 tall):
 *   +0  border
 *   +1  blank
 *   +2  label  (colored when selected)
 *   +3  blank
 *   +4  border
 */
static void draw_pwr_tile(int row, int col, const char *label,
                           uint8_t sel_fg, int selected) {
    uint8_t bg        = VGA_COLOR_BLACK;
    uint8_t border_fg = selected ? sel_fg           : VGA_COLOR_DARK_GREY;
    uint8_t label_fg  = selected ? sel_fg           : VGA_COLOR_DARK_GREY;

    uint8_t border_c = vga_make_color(border_fg, bg);
    uint8_t fill_c   = vga_make_color(VGA_COLOR_WHITE, bg);

    fill_rect(row + 1, col + 1, PWR_W - 2, PWR_H - 2, ' ', fill_c);
    draw_box(row, col, PWR_W, PWR_H, border_c, selected);
    write_centered(row + 2, col + 1, PWR_W - 2, label,
                   vga_make_color(label_fg, bg));
}

/* ── Full screen ─────────────────────────────────────────────────────────── */

/*
 * Mini icon art for the app tiles (13 chars wide each line).
 * CP437:  0xDA=┌  0xC4=─  0xBF=┐  0xC0=└  0xD9=┘  0xB3=│  0xF0=≡
 */
/* Icon strings are 9 chars wide — inner tile width = APP_W-2 = 18, offset = (18-9)/2 = 4 */
#define ICN_TOP  "\xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF"   /* ┌───────┐  9 chars */
#define ICN_BOT  "\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9"   /* └───────┘  9 chars */
#define ICN_TERM "\xB3  C:>  \xB3"                          /* │  C:>  │  9 chars */
#define ICN_EDIT "\xB3 \xF0\xF0\xF0\xF0\xF0 \xB3"          /* │ ≡≡≡≡≡ │  9 chars */
#define ICN_SET  "\xB3 \x0F\x0F\x0F\x0F\x0F \xB3"          /* │ ☼☼☼☼☼ │  9 chars */

static void draw_tiles(void) {
    draw_app_tile(APP_ROW, APP1_COL,
                  ICN_TOP, ICN_TERM, ICN_BOT,
                  "Terminal", "Shell & commands",
                  g_sel == ITEM_TERMINAL);

    draw_app_tile(APP_ROW, APP2_COL,
                  ICN_TOP, ICN_EDIT, ICN_BOT,
                  "Text Editor", "Edit text files",
                  g_sel == ITEM_EDITOR);

    draw_app_tile(APP_ROW, APP3_COL,
                  ICN_TOP, ICN_SET, ICN_BOT,
                  "Settings", "System info & config",
                  g_sel == ITEM_SETTINGS);

    draw_pwr_tile(PWR_ROW, PWR1_COL, "Shutdown",
                  VGA_COLOR_LIGHT_RED,  g_sel == ITEM_SHUTDOWN);
    draw_pwr_tile(PWR_ROW, PWR2_COL, "Reboot",
                  VGA_COLOR_YELLOW,     g_sel == ITEM_REBOOT);
    draw_pwr_tile(PWR_ROW, PWR3_COL, "Reboot AswdOS",
                  VGA_COLOR_LIGHT_CYAN, g_sel == ITEM_REBOOTUSB);
}

static void draw_status(void) {
    static const char *desc[ITEM_COUNT] = {
        "Terminal  \xf7  Full shell with commands and filesystem access",
        "Text Editor  \xf7  Create and edit text files on disk",
        "Settings  \xf7  System information and configuration",
        "Shutdown  \xf7  Power off the computer",
        "Reboot  \xf7  Restart the computer",
        "Reboot AswdOS  \xf7  Restart and boot Aswd OS from USB",
    };
    tui_status_bar(desc[g_sel]);
}

/* Section label: fill row with ─ then stamp the label text over it. */
static void draw_section(int row, const char *label) {
    uint8_t line_c  = vga_make_color(VGA_COLOR_DARK_GREY,  VGA_COLOR_BLACK);
    uint8_t label_c = vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_fill_row(row, (char)0xC4, line_c);
    tui_write_at(row, 2, label, label_c);
}

static void draw_frame(void) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();

    /* Header */
    tui_header_bar(ASWD_OS_BANNER);
    tui_write_at(0, 66, "Legacy TUI",
                 vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));

    /* Menu bar (decorative, non-interactive) */
    uint8_t menu_c = vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_fill_row(1, ' ', menu_c);
    tui_write_at(1, 1, "File   Options   Window   Help", menu_c);

    /* Horizontal rule below menu */
    vga_fill_row(2, (char)0xC4,
                 vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));

    /* Section headers */
    draw_section(4,  " Applications ");
    draw_section(16, " Power Options ");

    /* Navigation hint */
    uint8_t hint_c = vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_fill_row(23, ' ', hint_c);
    /* CP437: \x1B=←  \x1A=→  \x18=↑  \x19=↓ */
    tui_write_at(23, 2, "\x1B\x1A\x18\x19  navigate    Enter  launch", hint_c);
}

static void full_draw(void) {
    draw_frame();
    draw_tiles();
    draw_status();
}

/* ── Navigation ──────────────────────────────────────────────────────────── */

static void nav_left(void) {
    if      (g_sel == ITEM_EDITOR)    g_sel = ITEM_TERMINAL;
    else if (g_sel == ITEM_SETTINGS)  g_sel = ITEM_EDITOR;
    else if (g_sel == ITEM_REBOOT)    g_sel = ITEM_SHUTDOWN;
    else if (g_sel == ITEM_REBOOTUSB) g_sel = ITEM_REBOOT;
}

static void nav_right(void) {
    if      (g_sel == ITEM_TERMINAL)  g_sel = ITEM_EDITOR;
    else if (g_sel == ITEM_EDITOR)    g_sel = ITEM_SETTINGS;
    else if (g_sel == ITEM_SHUTDOWN)  g_sel = ITEM_REBOOT;
    else if (g_sel == ITEM_REBOOT)    g_sel = ITEM_REBOOTUSB;
}

static void nav_up(void) {
    if      (g_sel == ITEM_SHUTDOWN)  g_sel = ITEM_TERMINAL;
    else if (g_sel == ITEM_REBOOT)    g_sel = ITEM_EDITOR;
    else if (g_sel == ITEM_REBOOTUSB) g_sel = ITEM_SETTINGS;
}

static void nav_down(void) {
    if      (g_sel == ITEM_TERMINAL)  g_sel = ITEM_SHUTDOWN;
    else if (g_sel == ITEM_EDITOR)    g_sel = ITEM_REBOOT;
    else if (g_sel == ITEM_SETTINGS)  g_sel = ITEM_REBOOTUSB;
}

static void activate(void) {
    switch (g_sel) {
    case ITEM_TERMINAL:
        console_set_mode(CONSOLE_MODE_SHELL);
        shell_run(SHELL_MODE_NORMAL);
        /* shell_run loops forever; if it ever returns, redraw the hub */
        full_draw();
        break;

    case ITEM_EDITOR:
        pic_clear_mask(1);   /* ensure IRQ1 (keyboard) is unmasked after trampoline */
        editor_open("UNTITLED.TXT");
        full_draw();
        break;

    case ITEM_SETTINGS:
        settings_run();
        full_draw();
        break;

    case ITEM_SHUTDOWN:
        power_shutdown();
        break;

    case ITEM_REBOOT:
    case ITEM_REBOOTUSB:
        power_reboot();
        break;
    }
}

/* ── Entry point ─────────────────────────────────────────────────────────── */

void explorer_run(void) {
    console_set_mode(CONSOLE_MODE_SHELL);
    full_draw();

    for (;;) {
        char c = input_getchar();

        if (c == KEY_LEFT)  { nav_left();  draw_tiles(); draw_status(); continue; }
        if (c == KEY_RIGHT) { nav_right(); draw_tiles(); draw_status(); continue; }
        if (c == KEY_UP)    { nav_up();    draw_tiles(); draw_status(); continue; }
        if (c == KEY_DOWN)  { nav_down();  draw_tiles(); draw_status(); continue; }

        if (c == '\r' || c == '\n') { activate(); continue; }

        /* ESC or any unmapped key: just redraw (handles screen corruption) */
        if (c == 0x1B) { full_draw(); continue; }
    }
}
