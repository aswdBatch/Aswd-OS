#include "gui/browser_gui.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "net/http.h"
#include "net/net.h"

#define BROWSER_W 760
#define BROWSER_H 500

#define URL_MAX       256
#define BODY_MAX      8192
#define TITLE_MAX     96
#define HISTORY_MAX   12
#define LINES_MAX     512
#define LINE_TEXT_MAX 192

#define COL_BG         gfx_rgb(248, 250, 254)
#define COL_CARD       gfx_rgb(255, 255, 255)
#define COL_TEXT       gfx_rgb(24, 35, 50)
#define COL_TEXT_DIM   gfx_rgb(100, 116, 139)
#define COL_LINK       gfx_rgb(37, 99, 235)
#define COL_CODE_BG    gfx_rgb(238, 244, 251)
#define COL_TABLE_BG   gfx_rgb(242, 247, 253)
#define COL_RULE       gfx_rgb(203, 213, 225)
#define COL_WARN       gfx_rgb(180, 52, 72)

typedef enum {
    BROWSER_LINE_TEXT = 0,
    BROWSER_LINE_H1,
    BROWSER_LINE_H2,
    BROWSER_LINE_H3,
    BROWSER_LINE_LINK,
    BROWSER_LINE_CODE,
    BROWSER_LINE_TABLE,
    BROWSER_LINE_NOTE,
    BROWSER_LINE_RULE,
} browser_line_style_t;

typedef struct {
    browser_line_style_t style;
    char text[LINE_TEXT_MAX];
} browser_line_t;

typedef struct {
    gui_rect_t tab;
    gui_rect_t back;
    gui_rect_t forward;
    gui_rect_t reload;
    gui_rect_t url;
    gui_rect_t go;
    gui_rect_t content;
    gui_rect_t status;
} browser_layout_t;

static int g_win_id = -1;
static char g_url[URL_MAX];
static int  g_url_len = 0;
static char g_raw_body[BODY_MAX];
static int  g_raw_len = 0;
static char g_status_msg[96];
static char g_page_title[TITLE_MAX];
static char g_page_note[96];
static int  g_loading = 0;
static int  g_scroll = 0;
static int  g_layout_width = 0;
static int  g_line_count = 0;
static browser_line_t g_lines[LINES_MAX];
static char g_history[HISTORY_MAX][URL_MAX];
static int  g_history_count = 0;
static int  g_history_index = -1;

static int browser_min_int(int a, int b) {
    return a < b ? a : b;
}

static void browser_clip_text(char *out, int out_size, const char *text, int max_chars) {
    int i = 0;

    if (out_size <= 0) return;
    if (max_chars < 1) max_chars = 1;
    while (text && text[i] && i < max_chars && i + 1 < out_size) {
        out[i] = text[i];
        i++;
    }
    out[i] = '\0';
}

static void browser_set_status(const char *msg) {
    str_copy(g_status_msg, msg ? msg : "", sizeof(g_status_msg));
}

static void browser_clear_lines(void) {
    g_line_count = 0;
    g_scroll = 0;
}

static int browser_line_font(browser_line_style_t style) {
    const th_metrics_t *tm = th_metrics();

    if (style == BROWSER_LINE_H1) return tm->font_hero;
    if (style == BROWSER_LINE_H2) return tm->font_title;
    if (style == BROWSER_LINE_H3) return tm->font_body;
    if (style == BROWSER_LINE_NOTE) return tm->font_small;
    return tm->font_body;
}

static font_role_t browser_line_role(browser_line_style_t style) {
    if (style == BROWSER_LINE_CODE) return FONT_ROLE_MONO;
    return FONT_ROLE_UI;
}

static uint32_t browser_line_fg(browser_line_style_t style) {
    if (style == BROWSER_LINE_LINK) return COL_LINK;
    if (style == BROWSER_LINE_NOTE) return COL_TEXT_DIM;
    return COL_TEXT;
}

static int browser_line_height(browser_line_style_t style) {
    int font_px = browser_line_font(style);
    int line_h;

    if (style == BROWSER_LINE_RULE) return 14;
    line_h = gfx_font_line_height(browser_line_role(style), font_px);
    if (style == BROWSER_LINE_H1) line_h += 8;
    else if (style == BROWSER_LINE_H2) line_h += 6;
    else if (style == BROWSER_LINE_H3) line_h += 4;
    else line_h += 3;
    return line_h;
}

static void browser_push_line(browser_line_style_t style, const char *text) {
    if (g_line_count >= LINES_MAX) return;
    g_lines[g_line_count].style = style;
    str_copy(g_lines[g_line_count].text, text ? text : "", sizeof(g_lines[g_line_count].text));
    g_line_count++;
}

static void browser_push_blank(void) {
    if (g_line_count > 0 && g_lines[g_line_count - 1].text[0] == '\0' &&
        g_lines[g_line_count - 1].style == BROWSER_LINE_TEXT) {
        return;
    }
    browser_push_line(BROWSER_LINE_TEXT, "");
}

static void browser_push_wrapped(browser_line_style_t style, const char *text,
                                 int width_px, const char *prefix) {
    char normalized[768];
    char line[LINE_TEXT_MAX];
    int pos = 0;
    int out = 0;
    int prefix_len = 0;
    int font_px = browser_line_font(style);
    font_role_t role = browser_line_role(style);
    int char_cap = LINE_TEXT_MAX - 1;

    if (!text || !text[0]) return;

    for (int i = 0; text[i] && out + 1 < (int)sizeof(normalized); i++) {
        char ch = text[i];
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if (ch == ' ') {
            if (out == 0 || normalized[out - 1] == ' ') continue;
        }
        normalized[out++] = ch;
    }
    while (out > 0 && normalized[out - 1] == ' ') out--;
    normalized[out] = '\0';
    if (out == 0) return;

    line[0] = '\0';
    if (prefix && prefix[0]) {
        str_copy(line, prefix, sizeof(line));
        prefix_len = (int)str_len(line);
        pos = prefix_len;
    }

    {
        const char *p = normalized;
        while (*p) {
            char word[96];
            int wi = 0;
            int test_w;

            while (*p == ' ') p++;
            if (!*p) break;
            while (*p && *p != ' ' && wi + 1 < (int)sizeof(word)) {
                word[wi++] = *p++;
            }
            word[wi] = '\0';

            if (pos > prefix_len && pos + 1 < char_cap) {
                line[pos++] = ' ';
                line[pos] = '\0';
            }
            str_cat(line, word, sizeof(line));
            pos = (int)str_len(line);
            test_w = gfx_measure_text(role, font_px, line);
            if (test_w > width_px && pos > wi + prefix_len) {
                line[pos - wi - ((pos - wi - 1 > prefix_len) ? 1 : 0)] = '\0';
                browser_push_line(style, line);
                line[0] = '\0';
                if (prefix && prefix[0]) {
                    str_copy(line, prefix, sizeof(line));
                    prefix_len = (int)str_len(line);
                } else {
                    prefix_len = 0;
                }
                str_cat(line, word, sizeof(line));
                pos = (int)str_len(line);
            }
        }
        if (line[0]) browser_push_line(style, line);
    }
}

static void browser_decode_entity(const char **src, char *out) {
    if (str_ncmp(*src, "&amp;", 5) == 0) {
        *out = '&';
        *src += 5;
        return;
    }
    if (str_ncmp(*src, "&lt;", 4) == 0) {
        *out = '<';
        *src += 4;
        return;
    }
    if (str_ncmp(*src, "&gt;", 4) == 0) {
        *out = '>';
        *src += 4;
        return;
    }
    if (str_ncmp(*src, "&nbsp;", 6) == 0) {
        *out = ' ';
        *src += 6;
        return;
    }
    *out = **src;
    (*src)++;
}

static void browser_flush_block(char *buf, int *len, browser_line_style_t style,
                                int width_px, const char *prefix, int capture_title) {
    char tmp[768];

    if (*len <= 0) return;
    if (*len >= (int)sizeof(tmp)) *len = (int)sizeof(tmp) - 1;
    mem_copy(tmp, buf, (uint32_t)(*len));
    tmp[*len] = '\0';
    buf[0] = '\0';
    *len = 0;

    if (capture_title) {
        str_copy(g_page_title, tmp, sizeof(g_page_title));
        return;
    }

    if (style == BROWSER_LINE_RULE) {
        browser_push_line(BROWSER_LINE_RULE, "");
        return;
    }
    browser_push_wrapped(style, tmp, width_px, prefix);
}

static void browser_layout_document(int width_px) {
    const char *p = g_raw_body;
    char buf[768];
    int len = 0;
    int in_title = 0;
    int in_pre = 0;
    int in_code = 0;
    int in_link = 0;
    browser_line_style_t style = BROWSER_LINE_TEXT;
    browser_line_style_t pending_style = BROWSER_LINE_TEXT;
    int unsupported = 0;

    if (g_layout_width == width_px && g_line_count > 0) return;

    browser_clear_lines();
    g_page_title[0] = '\0';
    g_page_note[0] = '\0';
    g_layout_width = width_px;
    buf[0] = '\0';

    if (g_raw_len <= 0) return;

    while (*p) {
        if (*p == '<') {
            char tag[40];
            int closing = 0;
            int self_closing = 0;
            int ti = 0;

            p++;
            if (*p == '/') {
                closing = 1;
                p++;
            }
            while (*p && *p != '>' && *p != ' ' && ti + 1 < (int)sizeof(tag)) {
                char ch = *p++;
                if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                tag[ti++] = ch;
            }
            tag[ti] = '\0';
            while (*p && *p != '>') {
                if (*p == '/') self_closing = 1;
                p++;
            }
            if (*p == '>') p++;

            if (!closing) {
                if (str_eq(tag, "script") || str_eq(tag, "style") || str_eq(tag, "form") ||
                    str_eq(tag, "iframe") || str_eq(tag, "svg") || str_eq(tag, "canvas")) {
                    unsupported = 1;
                }
                if (str_eq(tag, "title")) {
                    browser_flush_block(buf, &len, style, width_px, 0, 0);
                    in_title = 1;
                    continue;
                }
                if (str_eq(tag, "h1") || str_eq(tag, "h2") || str_eq(tag, "h3") ||
                    str_eq(tag, "p") || str_eq(tag, "li") || str_eq(tag, "pre") ||
                    str_eq(tag, "code") || str_eq(tag, "tr")) {
                    browser_flush_block(buf, &len, style, width_px, 0, 0);
                    browser_push_blank();
                }
                if (str_eq(tag, "h1")) pending_style = BROWSER_LINE_H1;
                else if (str_eq(tag, "h2")) pending_style = BROWSER_LINE_H2;
                else if (str_eq(tag, "h3")) pending_style = BROWSER_LINE_H3;
                else if (str_eq(tag, "pre") || str_eq(tag, "code")) pending_style = BROWSER_LINE_CODE;
                else if (str_eq(tag, "th") || str_eq(tag, "td") || str_eq(tag, "tr")) pending_style = BROWSER_LINE_TABLE;
                else pending_style = BROWSER_LINE_TEXT;

                if (str_eq(tag, "hr")) {
                    browser_flush_block(buf, &len, style, width_px, 0, 0);
                    browser_push_blank();
                    browser_push_line(BROWSER_LINE_RULE, "");
                    browser_push_blank();
                    continue;
                }
                if (str_eq(tag, "br")) {
                    browser_flush_block(buf, &len, style, width_px, 0, 0);
                    continue;
                }
                if (str_eq(tag, "li")) {
                    style = BROWSER_LINE_TEXT;
                    continue;
                }
                if (str_eq(tag, "pre")) in_pre = 1;
                if (str_eq(tag, "code")) in_code = 1;
                if (str_eq(tag, "a")) in_link = 1;
                if (str_eq(tag, "th") || str_eq(tag, "td")) {
                    if (len > 0) {
                        buf[len++] = ' ';
                        buf[len++] = '|';
                        buf[len++] = ' ';
                    }
                    style = BROWSER_LINE_TABLE;
                    continue;
                }
                style = in_link ? BROWSER_LINE_LINK : pending_style;
                if (self_closing) {
                    browser_flush_block(buf, &len, style, width_px, 0, 0);
                    style = BROWSER_LINE_TEXT;
                }
                continue;
            }

            if (str_eq(tag, "title")) {
                browser_flush_block(buf, &len, style, width_px, 0, in_title);
                in_title = 0;
                style = BROWSER_LINE_TEXT;
                continue;
            }
            if (str_eq(tag, "a")) in_link = 0;
            if (str_eq(tag, "pre")) in_pre = 0;
            if (str_eq(tag, "code")) in_code = 0;

            if (str_eq(tag, "h1") || str_eq(tag, "h2") || str_eq(tag, "h3") ||
                str_eq(tag, "p") || str_eq(tag, "li") || str_eq(tag, "pre") ||
                str_eq(tag, "code") || str_eq(tag, "tr") || str_eq(tag, "td") ||
                str_eq(tag, "th")) {
                browser_flush_block(buf, &len, style, width_px,
                                    str_eq(tag, "li") ? "- " : 0, 0);
                if (str_eq(tag, "li") || str_eq(tag, "p") || str_eq(tag, "pre") ||
                    str_eq(tag, "code") || str_eq(tag, "tr")) {
                    browser_push_blank();
                }
            }
            style = in_link ? BROWSER_LINE_LINK : (in_pre || in_code ? BROWSER_LINE_CODE : BROWSER_LINE_TEXT);
            continue;
        }

        if (len + 2 >= (int)sizeof(buf)) {
            browser_flush_block(buf, &len, style, width_px, 0, in_title);
        }

        if (*p == '&') {
            browser_decode_entity(&p, &buf[len++]);
            continue;
        }

        if (in_pre || in_code) {
            if (*p == '\r') {
                p++;
                continue;
            }
            if (*p == '\n') {
                buf[len] = '\0';
                browser_push_line(BROWSER_LINE_CODE, buf);
                len = 0;
                p++;
                continue;
            }
        }

        buf[len++] = *p++;
    }

    browser_flush_block(buf, &len, style, width_px, 0, in_title);
    if (unsupported) {
        str_copy(g_page_note, "This page uses features beyond the current browser renderer. Showing the supported content only.",
                 sizeof(g_page_note));
        browser_push_blank();
        browser_push_line(BROWSER_LINE_NOTE, g_page_note);
    }
    if (g_line_count == 0) {
        browser_push_line(BROWSER_LINE_NOTE, "This page did not expose content in the current HTML-lite renderer.");
    }
}

static void browser_compute_layout(int x, int y, int w, int h, browser_layout_t *out) {
    const th_metrics_t *tm = th_metrics();
    th_layout_bucket_t bucket = th_layout_bucket_for_width(w);
    int nav_y = y + 16 + tm->tab_h + tm->gap_sm;

    out->tab.x = x + 14;
    out->tab.y = y + 12;
    out->tab.w = browser_min_int(bucket == TH_LAYOUT_COMPACT ? 160 : 220, w - 28);
    out->tab.h = tm->tab_h;

    out->back.x = x + 14;
    out->back.y = nav_y;
    out->back.w = 28;
    out->back.h = tm->button_h;
    out->forward = out->back;
    out->forward.x += 34;
    out->reload = out->forward;
    out->reload.x += 34;
    out->reload.w = 44;
    if (bucket == TH_LAYOUT_COMPACT) {
        out->url.x = x + 14;
        out->url.y = nav_y + tm->button_h + tm->gap_sm;
        out->url.w = w - 92;
        if (out->url.w < 120) out->url.w = 120;
        out->url.h = tm->field_h;
        out->go.x = x + w - 54;
        out->go.y = out->url.y;
        out->go.w = 40;
        out->go.h = tm->button_h;
        out->status.x = x + 14;
        out->status.y = out->url.y + tm->field_h + tm->gap_sm;
    } else {
        out->url.x = out->reload.x + out->reload.w + 8;
        out->url.y = nav_y;
        out->url.w = w - (out->url.x - x) - 72;
        if (out->url.w < 140) out->url.w = 140;
        out->url.h = tm->field_h;
        out->go.x = x + w - 54;
        out->go.y = nav_y;
        out->go.w = 40;
        out->go.h = tm->button_h;
        out->status.x = x + 14;
        out->status.y = nav_y + tm->field_h + tm->gap_sm;
    }
    out->status.w = w - 28;
    out->status.h = tm->font_body + tm->gap_md;
    out->content.x = x + 12;
    out->content.y = out->status.y + out->status.h + tm->gap_sm;
    out->content.w = w - 24;
    out->content.h = h - (out->content.y - y) - tm->status_h - 12;
}

static void browser_commit_history(const char *url) {
    if (!url || !url[0]) return;
    if (g_history_index >= 0 && str_eq(g_history[g_history_index], url)) return;

    if (g_history_index + 1 < g_history_count) {
        g_history_count = g_history_index + 1;
    }
    if (g_history_count >= HISTORY_MAX) {
        for (int i = 1; i < HISTORY_MAX; i++) {
            str_copy(g_history[i - 1], g_history[i], sizeof(g_history[i - 1]));
        }
        g_history_count = HISTORY_MAX - 1;
        g_history_index = HISTORY_MAX - 2;
    }
    g_history_index++;
    str_copy(g_history[g_history_index], url, sizeof(g_history[g_history_index]));
    g_history_count = g_history_index + 1;
}

static void browser_normalize_url(const char *input, char *out, int out_size) {
    if (str_ncmp(input, "http://", 7) == 0 || str_ncmp(input, "https://", 8) == 0) {
        str_copy(out, input, (uint32_t)out_size);
        return;
    }
    str_copy(out, "http://", (uint32_t)out_size);
    str_cat(out, input, (uint32_t)out_size);
}

static void browser_fetch_current(int add_history) {
    char final_url[URL_MAX];
    int status = 0;
    const net_info_t *ni = net_get_info();

    if (ni->active_transport == NET_TRANSPORT_NONE) {
        browser_set_status(ni->wifi_detected
            ? "Wi-Fi adapter detected, but no online data path is ready yet"
            : "No network interface detected");
        g_raw_body[0] = '\0';
        g_raw_len = 0;
        browser_clear_lines();
        return;
    }
    if (ni->active_transport == NET_TRANSPORT_WIFI) {
        browser_set_status(ni->connection_state == NET_CONN_UNSUPPORTED
            ? "Wi-Fi adapter detected, but its driver/backend is not online yet"
            : "Wi-Fi transport is not ready for Browser yet");
        g_raw_body[0] = '\0';
        g_raw_len = 0;
        browser_clear_lines();
        return;
    }
    if (!(ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3])) {
        browser_set_status("Awaiting DHCP lease");
        g_raw_body[0] = '\0';
        g_raw_len = 0;
        browser_clear_lines();
        return;
    }

    browser_normalize_url(g_url, final_url, sizeof(final_url));
    if (str_ncmp(final_url, "https://", 8) == 0) {
        browser_set_status("HTTPS is not enabled in this browser build yet");
        g_raw_body[0] = '\0';
        g_raw_len = 0;
        browser_clear_lines();
        return;
    }

    str_copy(g_url, final_url, sizeof(g_url));
    g_url_len = (int)str_len(g_url);
    browser_set_status("Loading page...");
    g_loading = 1;
    g_raw_body[0] = '\0';
    g_raw_len = 0;
    g_page_title[0] = '\0';
    g_page_note[0] = '\0';
    browser_clear_lines();

    g_raw_len = http_get(g_url, g_raw_body, BODY_MAX - 1, &status);
    g_loading = 0;

    if (g_raw_len < 0) {
        browser_set_status(http_error_string(http_last_error()));
        g_raw_body[0] = '\0';
        g_raw_len = 0;
        browser_clear_lines();
        return;
    }

    g_raw_body[g_raw_len] = '\0';
    if (add_history) browser_commit_history(g_url);

    {
        char code[8];
        browser_set_status("Loaded");
        u32_to_dec((uint32_t)status, code, sizeof(code));
        str_copy(g_status_msg, "HTTP ", sizeof(g_status_msg));
        str_cat(g_status_msg, code, sizeof(g_status_msg));
    }
    g_layout_width = 0;
}

static void browser_go_history(int delta) {
    if (g_history_index + delta < 0 || g_history_index + delta >= g_history_count) return;
    g_history_index += delta;
    str_copy(g_url, g_history[g_history_index], sizeof(g_url));
    g_url_len = (int)str_len(g_url);
    browser_fetch_current(0);
}

static void browser_draw_line(int x, int y, int width, const browser_line_t *line) {
    int font_px = browser_line_font(line->style);
    font_role_t role = browser_line_role(line->style);
    uint32_t fg = browser_line_fg(line->style);

    if (line->style == BROWSER_LINE_RULE) {
        gfx_fill_rect(x, y + 6, width, 1, COL_RULE);
        return;
    }
    if (line->style == BROWSER_LINE_CODE) {
        gfx_fill_rect(x - 4, y - 1, width + 8, browser_line_height(line->style) - 2, COL_CODE_BG);
    } else if (line->style == BROWSER_LINE_TABLE) {
        gfx_fill_rect(x - 4, y - 1, width + 8, browser_line_height(line->style) - 2, COL_TABLE_BG);
    }
    gfx_draw_string_role_transparent(x, y, line->text, role, font_px, fg);
}

static void browser_network_label(char *out, int size) {
    const net_info_t *ni = net_get_info();

    if (ni->active_transport == NET_TRANSPORT_WIRED) {
        if (!(ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3])) {
            str_copy(out, "Wired DHCP", (uint32_t)size);
            return;
        }
        str_copy(out, "Wired online", (uint32_t)size);
        return;
    }
    if (ni->active_transport == NET_TRANSPORT_WIFI) {
        str_copy(out,
                 ni->connection_state == NET_CONN_UNSUPPORTED ? "Wi-Fi pending"
                 : (ni->connection_state == NET_CONN_CONNECTED ? "Wi-Fi online" : "Wi-Fi idle"),
                 (uint32_t)size);
        return;
    }
    str_copy(out, ni->wifi_detected ? "Wi-Fi found" : "No network", (uint32_t)size);
}

static void browser_on_paint(int win_id) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(win_id);
    browser_layout_t layout;
    char tab_title[64];
    char net_label[24];
    int content_w;
    int draw_y;

    browser_compute_layout(r.x, r.y, r.w, r.h, &layout);
    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    content_w = layout.content.w - 34;
    if (g_raw_len > 0) {
        browser_layout_document(content_w);
    }

    tab_title[0] = '\0';
    if (g_page_title[0]) {
        browser_clip_text(tab_title, sizeof(tab_title), g_page_title, 18);
    } else if (g_url[0]) {
        browser_clip_text(tab_title, sizeof(tab_title), g_url, 18);
    } else {
        str_copy(tab_title, "New Tab", sizeof(tab_title));
    }
    th_draw_tab(layout.tab.x, layout.tab.y, layout.tab.w, layout.tab.h, tab_title, 1);

    th_draw_button(layout.back.x, layout.back.y, layout.back.w, layout.back.h, "<", g_history_index > 0);
    th_draw_button(layout.forward.x, layout.forward.y, layout.forward.w, layout.forward.h, ">", g_history_index + 1 < g_history_count);
    th_draw_button(layout.reload.x, layout.reload.y, layout.reload.w, layout.reload.h, "Reload", 0);
    th_draw_field(layout.url.x, layout.url.y, layout.url.w, "", 1, 0);
    if (g_url[0]) {
        int max_chars = browser_min_int(URL_MAX - 1, (layout.url.w - 18) / gfx_font_char_width(FONT_ROLE_UI, tm->font_body));
        int start = (g_url_len > max_chars) ? (g_url_len - max_chars) : 0;
        char clipped[URL_MAX];
        str_copy(clipped, g_url + start, sizeof(clipped));
        gfx_draw_string_role_transparent(layout.url.x + 8,
                                         layout.url.y + (layout.url.h - tm->font_body) / 2,
                                         clipped, FONT_ROLE_UI, tm->font_body, COL_TEXT);
    } else {
        gfx_draw_string_role_transparent(layout.url.x + 8,
                                         layout.url.y + (layout.url.h - tm->font_body) / 2,
                                         "Enter a URL", FONT_ROLE_UI, tm->font_body, COL_TEXT_DIM);
    }
    th_draw_button(layout.go.x, layout.go.y, layout.go.w, layout.go.h, "Go", 0);

    browser_network_label(net_label, sizeof(net_label));
    th_draw_info_strip(layout.status.x, layout.status.y, layout.status.w,
                       g_loading ? "Loading..." : g_status_msg,
                       g_page_title[0] ? g_page_title : "Simple HTML renderer",
                       net_label);

    th_draw_card(layout.content.x, layout.content.y, layout.content.w, layout.content.h, 0, COL_CARD, 0);

    if (g_loading) {
        th_draw_empty_state(layout.content.x + 18, layout.content.y + 18,
                            layout.content.w - 36, layout.content.h - 36,
                            "Loading page",
                            "Fetching content and rebuilding the native layout...");
        return;
    }
    if (g_raw_len == 0) {
        th_draw_empty_state(layout.content.x + 18, layout.content.y + 18,
                            layout.content.w - 36, layout.content.h - 36,
                            "No page loaded",
                            g_status_msg[0] ? g_status_msg : "Enter a URL to open a page.");
        return;
    }

    draw_y = layout.content.y + 14;

    for (int i = g_scroll; i < g_line_count; i++) {
        int line_h = browser_line_height(g_lines[i].style);
        if (draw_y + line_h > layout.content.y + layout.content.h - 12) break;
        browser_draw_line(layout.content.x + 14, draw_y, content_w - 10, &g_lines[i]);
        draw_y += line_h;
    }

    th_draw_scrollbar(layout.content.x + layout.content.w - 10,
                      layout.content.y + 10,
                      layout.content.h - 20,
                      g_line_count,
                      browser_min_int(g_line_count, (layout.content.h - 20) / (tm->font_body + 4)),
                      g_scroll);
}

static void browser_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }

    if (key == KEY_UP && g_scroll > 0) { g_scroll--; return; }
    if (key == KEY_DOWN && g_scroll + 1 < g_line_count) { g_scroll++; return; }
    if (key == KEY_LEFT) { browser_go_history(-1); return; }
    if (key == KEY_RIGHT) { browser_go_history(1); return; }
    if (key == '\r' || key == '\n') {
        if (g_url_len > 0) browser_fetch_current(1);
        return;
    }
    if (key == '\b' || key == (char)0x7F) {
        if (g_url_len > 0) {
            g_url[--g_url_len] = '\0';
            g_layout_width = 0;
        }
        return;
    }
    if ((unsigned char)key >= 32 && (unsigned char)key < 127) {
        if (g_url_len + 1 < URL_MAX) {
            g_url[g_url_len++] = key;
            g_url[g_url_len] = '\0';
        }
    }
}

static int browser_rect_contains(gui_rect_t r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void browser_on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    gui_rect_t r = gui_window_content(win_id);
    browser_layout_t layout;
    gui_rect_t local;
    (void)win_id;

    if (!(buttons & 1)) return;

    browser_compute_layout(0, 0, r.w, r.h, &layout);
    local.x = mx;
    local.y = my;
    local.w = 0;
    local.h = 0;

    if (browser_rect_contains(layout.back, local.x, local.y)) {
        browser_go_history(-1);
        return;
    }
    if (browser_rect_contains(layout.forward, local.x, local.y)) {
        browser_go_history(1);
        return;
    }
    if (browser_rect_contains(layout.reload, local.x, local.y)) {
        browser_fetch_current(0);
        return;
    }
    if (browser_rect_contains(layout.go, local.x, local.y)) {
        if (g_url_len > 0) browser_fetch_current(1);
    }
}

static void browser_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void browser_gui_launch(void) {
    gui_rect_t r;
    gui_window_t *w;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(BROWSER_W, BROWSER_H, &r);
    g_win_id = gui_window_create("Browser", r.x, r.y, r.w, r.h);
    if (g_win_id < 0) return;
    gui_window_set_min_size(g_win_id, 500, 340);

    if (g_url_len == 0) {
        str_copy(g_url, "http://example.com", sizeof(g_url));
        g_url_len = (int)str_len(g_url);
    }
    browser_set_status("Ready");
    g_raw_body[0] = '\0';
    g_raw_len = 0;
    g_page_title[0] = '\0';
    g_page_note[0] = '\0';
    g_loading = 0;
    g_scroll = 0;
    g_layout_width = 0;

    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_BROWSER;
    w->on_paint = browser_on_paint;
    w->on_key = browser_on_key;
    w->on_mouse = browser_on_mouse;
    w->on_close = browser_on_close;
}
