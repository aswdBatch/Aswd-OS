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
#include "gui/theme.h"
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

#define PANEL_PAD       12

static int g_win_id = -1;
static int g_scroll = 0;

static void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    th_draw_text(x, y, text, fg, bg, th_metrics()->font_body);
}

static void draw_text_small(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    th_draw_text(x, y, text, fg, bg, th_metrics()->font_small);
}

static void draw_kv(int x, int *y, const char *key, const char *value, uint32_t bg) {
    const th_metrics_t *tm = th_metrics();
    draw_text_small(x, *y + 2, key, COL_TEXT_DIM, bg);
    draw_text(x + 120, *y, value, COL_TEXT, bg);
    *y += gfx_font_line_height(FONT_ROLE_UI, tm->font_body) + tm->gap_sm;
}

static void format_memory(char *out, size_t out_size) {
    if (!multiboot_has_mem_info()) {
        str_copy(out, "unknown", out_size);
        return;
    }

    u32_to_dec((multiboot_mem_upper_kb() + 1024u) / 1024u, out, out_size);
    str_cat(out, " MB", out_size);
}

static int wrap_line_count(const char *text, int width, int font_px) {
    char line[192];
    int pos = 0;
    int lines = 0;

    if (!text || !text[0]) return 0;

    line[0] = '\0';
    for (const char *p = text; ; ) {
        char word[80];
        int wi = 0;
        int trial_w;

        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ' && wi + 1 < (int)sizeof(word)) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';
        if (pos > 0 && pos + 1 < (int)sizeof(line)) {
            line[pos++] = ' ';
            line[pos] = '\0';
        }
        str_cat(line, word, sizeof(line));
        pos = (int)str_len(line);
        trial_w = gfx_measure_text(FONT_ROLE_UI, font_px, line);
        if (trial_w > width && pos > wi) {
            line[pos - wi - 1] = '\0';
            lines++;
            str_copy(line, word, sizeof(line));
            pos = (int)str_len(line);
        }
    }
    if (line[0]) lines++;
    return lines;
}

static int draw_wrapped_text(int x, int y, int width, const char *text,
                             uint32_t fg, uint32_t bg, int font_px, int bullet) {
    char line[192];
    int pos = 0;
    int line_h = gfx_font_line_height(FONT_ROLE_UI, font_px) + 2;

    if (!text || !text[0]) return y;

    line[0] = '\0';
    for (const char *p = text; ; ) {
        char word[80];
        int wi = 0;
        int trial_w;

        while (*p == ' ') p++;
        if (!*p) break;
        while (*p && *p != ' ' && wi + 1 < (int)sizeof(word)) {
            word[wi++] = *p++;
        }
        word[wi] = '\0';
        if (pos > 0 && pos + 1 < (int)sizeof(line)) {
            line[pos++] = ' ';
            line[pos] = '\0';
        }
        str_cat(line, word, sizeof(line));
        pos = (int)str_len(line);
        trial_w = gfx_measure_text(FONT_ROLE_UI, font_px, line);
        if (trial_w > width && pos > wi) {
            line[pos - wi - 1] = '\0';
            if (bullet) {
                draw_text_small(x, y + 2, "-", TH_ACCENT, bg);
                th_draw_text(x + 12, y, line, fg, bg, font_px);
            } else {
                th_draw_text(x, y, line, fg, bg, font_px);
            }
            y += line_h;
            str_copy(line, word, sizeof(line));
            pos = (int)str_len(line);
        }
    }
    if (line[0]) {
        if (bullet) {
            draw_text_small(x, y + 2, "-", TH_ACCENT, bg);
            th_draw_text(x + 12, y, line, fg, bg, font_px);
        } else {
            th_draw_text(x, y, line, fg, bg, font_px);
        }
        y += line_h;
    }
    return y;
}

static int release_card_height(const changelog_entry_t *entry, int inner_w) {
    const th_metrics_t *tm = th_metrics();
    int height = tm->gap_md + tm->font_body + tm->gap_sm;

    if (!entry) return height;
    if (entry->summary && entry->summary[0]) {
        height += wrap_line_count(entry->summary, inner_w, tm->font_body) *
                  (gfx_font_line_height(FONT_ROLE_UI, tm->font_body) + 2);
        height += tm->gap_sm;
    }
    for (int note = 0; note < entry->note_count; note++) {
        height += wrap_line_count(entry->notes[note], inner_w - 16, tm->font_small) *
                  (gfx_font_line_height(FONT_ROLE_UI, tm->font_small) + 2);
        height += 2;
    }
    return height + tm->gap_sm;
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
    const th_metrics_t *tm = th_metrics();
    th_layout_bucket_t bucket;
    gui_rect_t r = gui_window_content(win_id);
    const changelog_entry_t *latest = changelog_latest();
    char brand[49];
    char memory[24];
    gui_rect_t overview;
    gui_rect_t system;
    gui_rect_t release;
    int top_y;

    clamp_scroll();
    cpuid_get_brand(brand);
    format_memory(memory, sizeof(memory));
    bucket = th_layout_bucket_for_width(r.w);

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    th_draw_page_header(r.x + 8, r.y + 10, r.w - 16,
                        "System",
                        "AswdOS release trail",
                        ASWD_OS_HELLO);
    th_draw_statusbar(r.x, r.y + r.h - tm->status_h, r.w, tm->status_h,
                      "OS Info reads the built-in changelog used for release summaries.");

    top_y = r.y + 10 + th_page_header_height() + tm->gap_sm;
    if (bucket == TH_LAYOUT_WIDE) {
        int card_w = (r.w - PANEL_PAD * 3) / 2;
        overview.x = r.x + PANEL_PAD;
        overview.y = top_y;
        overview.w = card_w;
        overview.h = 126;
        system.x = overview.x + overview.w + PANEL_PAD;
        system.y = top_y;
        system.w = card_w;
        system.h = 126;
        release.x = r.x + PANEL_PAD;
        release.y = top_y + overview.h + PANEL_PAD;
        release.w = r.w - PANEL_PAD * 2;
        release.h = r.h - (release.y - r.y) - tm->status_h - PANEL_PAD;
    } else {
        overview.x = r.x + PANEL_PAD;
        overview.y = top_y;
        overview.w = r.w - PANEL_PAD * 2;
        overview.h = 110;
        system.x = overview.x;
        system.y = overview.y + overview.h + tm->gap_sm;
        system.w = overview.w;
        system.h = 118;
        release.x = r.x + PANEL_PAD;
        release.y = system.y + system.h + tm->gap_sm;
        release.w = r.w - PANEL_PAD * 2;
        release.h = r.h - (release.y - r.y) - tm->status_h - PANEL_PAD;
    }
    if (release.h < 120) release.h = 120;

    th_draw_card(overview.x, overview.y, overview.w, overview.h, 0, COL_PANEL, 0);
    draw_text_small(overview.x + 12, overview.y + 12, "Overview", TH_ACCENT, COL_PANEL);
    if (latest) {
        int y = overview.y + 34;

        draw_kv(overview.x + 12, &y, "Version", latest->version, COL_PANEL);
        if (latest->date && latest->date[0]) {
            draw_kv(overview.x + 12, &y, "Released", latest->date, COL_PANEL);
        }
        draw_wrapped_text(overview.x + 12, y + 2, overview.w - 24,
                          latest->summary, COL_TEXT, COL_PANEL, tm->font_body, 0);
    }

    th_draw_card(system.x, system.y, system.w, system.h, 0, COL_PANEL_ALT, 0);
    {
        int y = system.y + 34;
        draw_text_small(system.x + 12, system.y + 12, "System", TH_ACCENT, COL_PANEL_ALT);
        draw_kv(system.x + 12, &y, "CPU", brand, COL_PANEL_ALT);
        draw_kv(system.x + 12, &y, "Memory", memory, COL_PANEL_ALT);
        draw_kv(system.x + 12, &y, "Filesystem", vfs_available() ? "Mounted" : "Unavailable", COL_PANEL_ALT);
        draw_kv(system.x + 12, &y, "Workspace", "/ROOT only", COL_PANEL_ALT);
    }

    th_draw_card(release.x, release.y, release.w, release.h, 0, COL_PANEL, 0);
    draw_text_small(release.x + 12, release.y + 12, "Changelog", TH_ACCENT, COL_PANEL);
    draw_text_small(release.x + release.w - 164, release.y + 12,
                    "Up/Down scroll | Ctrl+Q close", COL_TEXT_DIM, COL_PANEL);

    {
        int y = release.y + 34;
        int bottom = release.y + release.h - 10;
        int card_x = release.x + 8;
        int inner_w = release.w - 16;

        for (int i = g_scroll; i < changelog_count(); i++) {
            const changelog_entry_t *entry = changelog_entry_at(i);
            int card_h = release_card_height(entry, inner_w - 20);
            uint32_t bg = (i == 0) ? COL_PANEL_ALT : COL_BG;

            if (!entry || y + card_h > bottom) break;

            gfx_fill_rect(card_x, y, inner_w, card_h, bg);
            gfx_fill_rect(card_x, y, inner_w, 1, COL_BORDER);
            gfx_fill_rect(card_x, y + card_h - 1, inner_w, 1, COL_BORDER);
            draw_text(card_x + 10, y + 8, entry->version, COL_TEXT, bg);
            if (entry->date && entry->date[0]) {
                draw_text_small(card_x + 92, y + 10, entry->date, COL_TEXT_DIM, bg);
            }

            {
                int line_y = y + 26;

                if (entry->summary && entry->summary[0]) {
                    line_y = draw_wrapped_text(card_x + 10, line_y, inner_w - 20,
                                               entry->summary, COL_TEXT, bg, tm->font_body, 0);
                    line_y += 2;
                }

                for (int note = 0; note < entry->note_count; note++) {
                    line_y = draw_wrapped_text(card_x + 10, line_y, inner_w - 20,
                                               entry->notes[note], COL_TEXT, bg, tm->font_small, 1);
                }
            }

            y += card_h + 8;
        }
    }

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
    gui_window_set_min_size(g_win_id, 520, 380);

    g_scroll = 0;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_OSINFO;
    w->on_paint = osinfo_on_paint;
    w->on_key = osinfo_on_key;
    w->on_close = osinfo_on_close;
}
