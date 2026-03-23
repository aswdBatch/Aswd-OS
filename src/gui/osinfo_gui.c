#include "gui/osinfo_gui.h"

#include <stddef.h>
#include <stdint.h>

#include "boot/multiboot.h"
#include "common/changelog.h"
#include "common/config.h"
#include "cpu/cpuid.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "lib/string.h"

#define COL_BG          gfx_rgb(243, 246, 252)
#define COL_HEADER      gfx_rgb(24, 72, 136)
#define COL_HEADER_TXT  gfx_rgb(255, 255, 255)
#define COL_PANEL       gfx_rgb(255, 255, 255)
#define COL_PANEL_ALT   gfx_rgb(233, 240, 250)
#define COL_BORDER      gfx_rgb(191, 204, 224)
#define COL_TEXT        gfx_rgb(35, 47, 67)
#define COL_TEXT_DIM    gfx_rgb(101, 115, 141)
#define COL_ACCENT      gfx_rgb(38, 99, 235)
#define COL_STATUS      gfx_rgb(229, 236, 246)
#define COL_STATUS_TXT  gfx_rgb(58, 74, 101)

#define HEADER_H        64
#define STATUS_H        24
#define PANEL_PAD       12
#define CARD_H          104

static int g_win_id = -1;
static int g_scroll = 0;

static void clip_text(char *out, size_t out_size, const char *text, int max_chars) {
    int i = 0;

    if (out_size == 0) return;
    if (max_chars < 1) max_chars = 1;
    while (text && text[i] && i < max_chars && i + 1 < (int)out_size) {
        out[i] = text[i];
        i++;
    }
    out[i] = '\0';
}

static void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    gfx_draw_string(x, y, text, fg, bg);
}

static void draw_kv(int x, int *y, const char *key, const char *value, uint32_t bg) {
    draw_text(x, *y, key, COL_TEXT_DIM, bg);
    draw_text(x + 112, *y, value, COL_TEXT, bg);
    *y += 18;
}

static void format_memory(char *out, size_t out_size) {
    if (!multiboot_has_mem_info()) {
        str_copy(out, "unknown", out_size);
        return;
    }

    u32_to_dec((multiboot_mem_upper_kb() + 1024u) / 1024u, out, out_size);
    str_cat(out, " MB", out_size);
}

static int release_card_height(const changelog_entry_t *entry) {
    int height = 36;

    if (!entry) return height;
    if (entry->summary && entry->summary[0]) height += 18;
    height += entry->note_count * 18;
    return height;
}

static void clamp_scroll(void) {
    int count = changelog_count();

    if (count <= 0) {
        g_scroll = 0;
        return;
    }
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll >= count) g_scroll = count - 1;
}

static void osinfo_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    const changelog_entry_t *latest = changelog_latest();
    char brand[49];
    char clip[40];
    char memory[24];
    int card_w;
    int left_x;
    int right_x;
    int top_y;
    int release_y;
    int release_h;

    clamp_scroll();
    cpuid_get_brand(brand);
    format_memory(memory, sizeof(memory));

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    gfx_fill_rect(r.x, r.y, r.w, HEADER_H, COL_HEADER);
    gfx_fill_rect(r.x, r.y + HEADER_H - 1, r.w, 1, COL_BORDER);
    gfx_fill_rect(r.x, r.y + r.h - STATUS_H, r.w, STATUS_H, COL_STATUS);
    gfx_fill_rect(r.x, r.y + r.h - STATUS_H, r.w, 1, COL_BORDER);

    draw_text(r.x + 14, r.y + 12, ASWD_OS_BANNER, COL_HEADER_TXT, COL_HEADER);
    draw_text(r.x + 14, r.y + 30, ASWD_OS_HELLO, gfx_rgb(214, 230, 255), COL_HEADER);

    card_w = (r.w - PANEL_PAD * 3) / 2;
    if (card_w < 180) card_w = r.w - PANEL_PAD * 2;
    left_x = r.x + PANEL_PAD;
    right_x = left_x + card_w + PANEL_PAD;
    top_y = r.y + HEADER_H + PANEL_PAD;

    gfx_fill_rect(left_x, top_y, card_w, CARD_H, COL_PANEL);
    gfx_fill_rect(left_x, top_y, card_w, 1, COL_BORDER);
    gfx_fill_rect(left_x, top_y + CARD_H - 1, card_w, 1, COL_BORDER);
    gfx_fill_rect(left_x, top_y, 1, CARD_H, COL_BORDER);
    gfx_fill_rect(left_x + card_w - 1, top_y, 1, CARD_H, COL_BORDER);

    draw_text(left_x + 12, top_y + 10, "Overview", COL_ACCENT, COL_PANEL);
    if (latest) {
        int y = top_y + 34;

        draw_kv(left_x + 12, &y, "Version", latest->version, COL_PANEL);
        if (latest->date && latest->date[0]) {
            draw_kv(left_x + 12, &y, "Released", latest->date, COL_PANEL);
        }
        clip_text(clip, sizeof(clip), latest->summary, (card_w - 24) / FONT_WIDTH);
        draw_text(left_x + 12, y + 4, clip, COL_TEXT, COL_PANEL);
    }

    if (right_x + card_w <= r.x + r.w - PANEL_PAD) {
        int y = top_y + 34;

        gfx_fill_rect(right_x, top_y, card_w, CARD_H, COL_PANEL_ALT);
        gfx_fill_rect(right_x, top_y, card_w, 1, COL_BORDER);
        gfx_fill_rect(right_x, top_y + CARD_H - 1, card_w, 1, COL_BORDER);
        gfx_fill_rect(right_x, top_y, 1, CARD_H, COL_BORDER);
        gfx_fill_rect(right_x + card_w - 1, top_y, 1, CARD_H, COL_BORDER);

        draw_text(right_x + 12, top_y + 10, "System", COL_ACCENT, COL_PANEL_ALT);
        clip_text(clip, sizeof(clip), brand, (card_w - 136) / FONT_WIDTH);
        draw_kv(right_x + 12, &y, "CPU", clip, COL_PANEL_ALT);
        draw_kv(right_x + 12, &y, "Memory", memory, COL_PANEL_ALT);
        draw_kv(right_x + 12, &y, "Filesystem",
                vfs_available() ? "Mounted" : "Unavailable", COL_PANEL_ALT);
        draw_kv(right_x + 12, &y, "Workspace", "/ROOT only", COL_PANEL_ALT);
    }

    release_y = top_y + CARD_H + PANEL_PAD;
    release_h = r.h - HEADER_H - CARD_H - STATUS_H - PANEL_PAD * 3;
    if (release_h < 80) release_h = 80;

    gfx_fill_rect(r.x + PANEL_PAD, release_y, r.w - PANEL_PAD * 2, release_h, COL_PANEL);
    gfx_fill_rect(r.x + PANEL_PAD, release_y, r.w - PANEL_PAD * 2, 1, COL_BORDER);
    gfx_fill_rect(r.x + PANEL_PAD, release_y + release_h - 1, r.w - PANEL_PAD * 2, 1,
                  COL_BORDER);
    gfx_fill_rect(r.x + PANEL_PAD, release_y, 1, release_h, COL_BORDER);
    gfx_fill_rect(r.x + r.w - PANEL_PAD - 1, release_y, 1, release_h, COL_BORDER);

    draw_text(r.x + PANEL_PAD + 12, release_y + 10, "Changelog", COL_ACCENT, COL_PANEL);
    draw_text(r.x + r.w - PANEL_PAD - 190, release_y + 10,
              "Up/Down scroll | Ctrl+Q close", COL_TEXT_DIM, COL_PANEL);

    {
        int y = release_y + 34;
        int bottom = release_y + release_h - 8;
        int card_x = r.x + PANEL_PAD + 8;
        int inner_w = r.w - PANEL_PAD * 2 - 16;

        for (int i = g_scroll; i < changelog_count(); i++) {
            const changelog_entry_t *entry = changelog_entry_at(i);
            int card_h = release_card_height(entry);
            uint32_t bg = (i == 0) ? COL_PANEL_ALT : COL_BG;

            if (!entry || y + card_h > bottom) break;

            gfx_fill_rect(card_x, y, inner_w, card_h, bg);
            gfx_fill_rect(card_x, y, inner_w, 1, COL_BORDER);
            gfx_fill_rect(card_x, y + card_h - 1, inner_w, 1, COL_BORDER);
            draw_text(card_x + 10, y + 8, entry->version, COL_TEXT, bg);
            if (entry->date && entry->date[0]) {
                draw_text(card_x + 86, y + 8, entry->date, COL_TEXT_DIM, bg);
            }

            {
                int line_y = y + 26;

                if (entry->summary && entry->summary[0]) {
                    clip_text(clip, sizeof(clip), entry->summary, (inner_w - 20) / FONT_WIDTH);
                    draw_text(card_x + 10, line_y, clip, COL_TEXT, bg);
                    line_y += 18;
                }

                for (int note = 0; note < entry->note_count; note++) {
                    clip_text(clip, sizeof(clip), entry->notes[note],
                              (inner_w - 34) / FONT_WIDTH);
                    draw_text(card_x + 10, line_y, "-", COL_ACCENT, bg);
                    draw_text(card_x + 22, line_y, clip, COL_TEXT, bg);
                    line_y += 18;
                }
            }

            y += card_h + 8;
        }
    }

    draw_text(r.x + 10, r.y + r.h - STATUS_H + 4,
              "OS Info reads the built-in changelog used for release summaries.",
              COL_STATUS_TXT, COL_STATUS);
}

static void osinfo_on_key(int win_id, char key) {
    (void)win_id;

    if (key == 0x11) {
        gui_window_close(g_win_id);
        return;
    }
    if (key == KEY_UP) {
        g_scroll--;
        clamp_scroll();
        return;
    }
    if (key == KEY_DOWN) {
        g_scroll++;
        clamp_scroll();
        return;
    }
    if (key == KEY_PAGEUP) {
        g_scroll -= 2;
        clamp_scroll();
        return;
    }
    if (key == KEY_PAGEDOWN) {
        g_scroll += 2;
        clamp_scroll();
        return;
    }
    if (key == KEY_HOME) {
        g_scroll = 0;
        return;
    }
    if (key == KEY_END) {
        g_scroll = changelog_count() - 1;
        clamp_scroll();
    }
}

static void osinfo_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
    g_scroll = 0;
}

void osinfo_gui_launch(void) {
    gui_rect_t rect;
    gui_window_t *w;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(700, 500, &rect);
    g_win_id = gui_window_create("OS Info", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;

    g_scroll = 0;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_OSINFO;
    w->on_paint = osinfo_on_paint;
    w->on_key = osinfo_on_key;
    w->on_close = osinfo_on_close;
}
