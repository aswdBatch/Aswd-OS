#include "settings/settings.h"

#include "boot/multiboot.h"
#include "common/colors.h"
#include "common/config.h"
#include "cpu/timer.h"
#include "diagnostics/diagnostics.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "fs/vfs.h"
#include "input/input.h"
#include "lib/string.h"
#include "tui/tui.h"

/*
 * Settings / About screen.
 *
 * Tabs (LEFT/RIGHT to switch):
 *   [About]        OS version, build info
 *   [System]       CPU, memory placeholder
 *   [Diagnostics]   Run reusable smoke tests
 *
 * ESC / Ctrl+Q / Enter on non-diagnostics tabs returns to hub.
 */

#define TAB_ABOUT       0
#define TAB_SYSTEM      1
#define TAB_DIAGNOSTICS 2
#define TAB_COUNT       3

static int  g_tab = 0;
static int  g_diag_sel = 0;
static char g_diag_msg[96];

static void set_diag_msg(const char *msg) {
    str_copy(g_diag_msg, msg ? msg : "", sizeof(g_diag_msg));
}

static void draw_tabs(void) {
    const char *labels[TAB_COUNT] = { " About ", " System ", " Diagnostics " };
    int cols[TAB_COUNT] = { 2, 10, 19 };

    for (int i = 0; i < TAB_COUNT; i++) {
        uint8_t c = (i == g_tab)
            ? vga_make_color(VGA_COLOR_WHITE,      VGA_COLOR_DARK_GREY)
            : vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        tui_write_at(2, cols[i], labels[i], c);
    }
}

static void draw_about(void) {
    uint8_t key_c = vga_make_color(VGA_COLOR_LIGHT_CYAN,  VGA_COLOR_BLACK);
    uint8_t val_c = vga_make_color(VGA_COLOR_WHITE,       VGA_COLOR_BLACK);
    uint8_t dim_c = vga_make_color(VGA_COLOR_LIGHT_GREY,  VGA_COLOR_BLACK);

    int r = 5;
    tui_write_at(r++, 4, "OS",        key_c);
    tui_write_at(r-1, 18, ASWD_OS_BANNER, val_c);

    tui_write_at(r++, 4, "Arch",      key_c);
    tui_write_at(r-1, 18, "x86 32-bit protected mode", val_c);

    tui_write_at(r++, 4, "Display",   key_c);
    tui_write_at(r-1, 18, "VGA 80x25 text mode (CP437)", val_c);

    tui_write_at(r++, 4, "Storage",   key_c);
    tui_write_at(r-1, 18, vfs_available() ? "FAT32 (USB)" : "none (GRUB/QEMU)", val_c);

    r++;
    tui_write_at(r, 4, "Aswd OS is a hobby x86 OS project.", dim_c);
}

static void fmt_ram(char *out, size_t size) {
    if (!multiboot_has_mem_info()) {
        str_copy(out, "unknown", size);
        return;
    }
    /* upper memory starts at 1 MB; add 1024 KB for the first megabyte */
    uint32_t total_mb = (multiboot_mem_upper_kb() + 1024u) / 1024u;
    char tmp[12];
    u32_to_dec(total_mb, tmp, sizeof(tmp));
    str_copy(out, tmp, size);
    str_cat(out, " MB", size);
}

static void fmt_uptime(char *out, size_t size) {
    uint32_t secs = timer_uptime_secs();
    uint32_t h = secs / 3600u;
    uint32_t m = (secs % 3600u) / 60u;
    uint32_t s = secs % 60u;
    char tmp[12];
    out[0] = '\0';
    u32_to_dec(h, tmp, sizeof(tmp)); str_cat(out, tmp, size);
    str_cat(out, "h ", size);
    u32_to_dec(m, tmp, sizeof(tmp)); str_cat(out, tmp, size);
    str_cat(out, "m ", size);
    u32_to_dec(s, tmp, sizeof(tmp)); str_cat(out, tmp, size);
    str_cat(out, "s", size);
}

static void draw_system(void) {
    uint8_t key_c = vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    uint8_t val_c = vga_make_color(VGA_COLOR_WHITE,      VGA_COLOR_BLACK);

    char buf[32];

    int r = 5;
    tui_write_at(r++, 4, "Mode",   key_c);
    tui_write_at(r-1, 18, "x86 32-bit protected mode", val_c);

    fmt_ram(buf, sizeof(buf));
    tui_write_at(r++, 4, "Memory", key_c);
    tui_write_at(r-1, 18, buf, val_c);

    fmt_uptime(buf, sizeof(buf));
    tui_write_at(r++, 4, "Uptime", key_c);
    tui_write_at(r-1, 18, buf, val_c);

    tui_write_at(r++, 4, "Video",  key_c);
    tui_write_at(r-1, 18, "VGA text 0xB8000", val_c);

    tui_write_at(r++, 4, "Disk",   key_c);
    tui_write_at(r-1, 18, vfs_available() ? "INT 13h trampoline + FAT32" : "not available", val_c);
}

static void draw_diag_option(int row, const char *label, const char *value, int selected) {
    uint8_t bg = selected
        ? vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY)
        : vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    uint8_t label_c = selected
        ? vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY)
        : vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    uint8_t value_c = selected
        ? vga_make_color(VGA_COLOR_YELLOW, VGA_COLOR_DARK_GREY)
        : vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    vga_fill_row(row, ' ', bg);
    tui_write_at(row, 4, label, label_c);
    tui_write_at(row, 22, value, value_c);
}

static void draw_diagnostics(void) {
    uint8_t dim_c = vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    const char *smoke_desc = "Command dispatch + OS info";
    const char *temp_desc = vfs_available()
        ? "Create, read, and delete a disposable file"
        : "Requires filesystem access";
    const char *kbd_desc = "Show active keyboard path (PS/2 vs BIOS)";

    draw_diag_option(5,  "Smoke test",    smoke_desc, g_diag_sel == 0);
    draw_diag_option(7,  "Temp write",    temp_desc,  g_diag_sel == 1);
    draw_diag_option(9,  "Keyboard info", kbd_desc,   g_diag_sel == 2);

    tui_write_at(11, 4, "Up/Down choose a test. Enter runs it. ESC closes.", dim_c);
    tui_write_at(13, 4, g_diag_msg[0] ? g_diag_msg : "Ready.", dim_c);
}

static void draw_body(void) {
    uint8_t bg = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    for (int row = 3; row <= 23; row++) {
        vga_fill_row(row, ' ', bg);
    }

    draw_tabs();

    /* horizontal rule under tabs */
    uint8_t rule_c = vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    vga_fill_row(3, (char)0xC4, rule_c);

    if (g_tab == TAB_ABOUT) {
        draw_about();
    } else if (g_tab == TAB_SYSTEM) {
        draw_system();
    } else {
        draw_diagnostics();
    }
}

static void render(void) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();

    tui_header_bar(ASWD_OS_BANNER);
    tui_write_at(0, 63, "Settings",
                 vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLUE));

    draw_body();

    if (g_tab == TAB_DIAGNOSTICS) {
        tui_status_bar("\x1B\x1A  switch tab    \x18\x19  choose    Enter  run    ESC  close");
    } else {
        tui_status_bar("\x1B\x1A  switch tab    ESC  close");
    }
}

static void run_diagnostics(void) {
    diagnostic_test_mode_t mode;
    if (g_diag_sel == 0)      mode = DIAGNOSTIC_TEST_SMOKE;
    else if (g_diag_sel == 1) mode = DIAGNOSTIC_TEST_TEMP_WRITE;
    else                      mode = DIAGNOSTIC_TEST_KEYBOARD;

    set_diag_msg("Running test...");
    render();

    if (diagnostics_run_test(mode)) {
        set_diag_msg("Test passed.");
    } else {
        set_diag_msg("Test failed.");
    }
    render();
}

void settings_run(void) {
    g_tab = 0;
    g_diag_sel = 0;
    set_diag_msg("Ready.");
    render();

    for (;;) {
        char c = input_getchar();

        if (c == 0x1B || c == 0x11) break;   /* ESC or Ctrl+Q */
        if (c == '\r' || c == '\n') {
            if (g_tab == TAB_DIAGNOSTICS) {
                run_diagnostics();
                continue;
            }
            break;
        }

        if (c == KEY_LEFT && g_tab > 0) {
            g_tab--;
            render();
            continue;
        }
        if (c == KEY_RIGHT && g_tab < TAB_COUNT - 1) {
            g_tab++;
            render();
            continue;
        }

        if (g_tab == TAB_DIAGNOSTICS) {
            if (c == KEY_UP && g_diag_sel > 0) {
                g_diag_sel--;
                render();
                continue;
            }
            if (c == KEY_DOWN && g_diag_sel < 2) {
                g_diag_sel++;
                render();
                continue;
            }
        }
    }
}
