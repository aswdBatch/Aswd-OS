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

#define BROWSER_W   560
#define BROWSER_H   400
#define URL_BAR_H   30
#define STATUS_H    18
#define CONTENT_PAD 8

#define COL_BG          gfx_rgb(252, 253, 255)
#define COL_URL_BG      gfx_rgb(245, 247, 252)
#define COL_URL_BORDER  gfx_rgb(160, 180, 220)
#define COL_URL_TXT     gfx_rgb(22,  34,  60)
#define COL_STATUS_BG   gfx_rgb(230, 235, 245)
#define COL_STATUS_TXT  gfx_rgb(80, 100, 140)
#define COL_BODY_TXT    gfx_rgb(30,  38,  60)
#define COL_GO_BG       gfx_rgb(37,  99, 235)
#define COL_GO_TXT      gfx_rgb(255, 255, 255)
#define COL_HDR_BG      gfx_rgb(22,  34,  60)
#define COL_HDR_TXT     gfx_rgb(255, 255, 255)
#define COL_NO_NET      gfx_rgb(200, 60, 60)

#define URL_MAX  256
#define BODY_MAX 8192

static int    g_win_id = -1;
static char   g_url[URL_MAX];
static int    g_url_len = 0;
static char   g_body[BODY_MAX];
static int    g_body_len = 0;
static char   g_status_msg[80];
static int    g_loading = 0;
static int    g_scroll  = 0;  /* lines scrolled */

/* Strip HTML tags and collapse whitespace for display */
static void strip_html(const char *src, char *dst, int max) {
    int in_tag = 0;
    int pos = 0;
    int last_space = 0;
    const char *p = src;

    while (*p && pos < max - 1) {
        if (*p == '<') { in_tag = 1; p++; continue; }
        if (*p == '>') { in_tag = 0; p++;
            /* Add a newline after block elements */
            if (pos > 0 && dst[pos-1] != '\n') {
                dst[pos++] = '\n';
                last_space = 0;
            }
            continue;
        }
        if (in_tag) { p++; continue; }

        /* Decode simple HTML entities */
        if (*p == '&') {
            if (str_ncmp(p, "&amp;",  5) == 0) { dst[pos++] = '&'; p += 5; continue; }
            if (str_ncmp(p, "&lt;",   4) == 0) { dst[pos++] = '<'; p += 4; continue; }
            if (str_ncmp(p, "&gt;",   4) == 0) { dst[pos++] = '>'; p += 4; continue; }
            if (str_ncmp(p, "&nbsp;", 6) == 0) { dst[pos++] = ' '; p += 6; continue; }
        }

        char c = *p++;
        if (c == '\r') continue;
        if (c == '\t') c = ' ';
        if (c == ' ' || c == '\n') {
            if (!last_space && pos > 0) { dst[pos++] = c; last_space = 1; }
            continue;
        }
        last_space = 0;
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static void do_fetch(void) {
    const net_info_t *ni = net_get_info();

    if (!ni->link_up) {
        str_copy(g_status_msg, "No network interface", sizeof(g_status_msg));
        g_body[0] = '\0'; g_body_len = 0;
        return;
    }
    if (!(ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3])) {
        str_copy(g_status_msg, "No IP address (DHCP pending)", sizeof(g_status_msg));
        g_body[0] = '\0'; g_body_len = 0;
        return;
    }

    str_copy(g_status_msg, "Connecting...", sizeof(g_status_msg));
    g_loading = 1;
    g_body[0] = '\0'; g_body_len = 0;
    g_scroll = 0;

    char raw_body[BODY_MAX];
    int status = 0;
    int n = http_get(g_url, raw_body, BODY_MAX - 1, &status);

    if (n < 0) {
        str_copy(g_status_msg, "Connection failed", sizeof(g_status_msg));
    } else {
        char code[8];
        u32_to_dec((uint32_t)status, code, sizeof(code));
        g_status_msg[0] = '\0';
        str_cat(g_status_msg, "HTTP ", sizeof(g_status_msg));
        str_cat(g_status_msg, code, sizeof(g_status_msg));
        raw_body[n] = '\0';
        strip_html(raw_body, g_body, BODY_MAX);
        g_body_len = (int)str_len(g_body);
    }
    g_loading = 0;
}

/* Draw word-wrapped text in a region. Returns number of lines drawn. */
static int draw_wrapped_text(int x, int y, int w, int h, const char *text, int skip_lines) {
    int line_h   = FONT_HEIGHT + 2;
    int chars_per_line = w / FONT_WIDTH;
    int max_lines = h / line_h;
    int line_drawn = 0;
    int line_idx   = 0;
    const char *p  = text;

    if (chars_per_line < 1) chars_per_line = 1;

    while (*p) {
        /* Find end of this visual line */
        const char *line_start = p;
        int len = 0;
        const char *last_space = 0;
        int last_space_len = 0;

        while (*p && *p != '\n' && len < chars_per_line) {
            if (*p == ' ') { last_space = p; last_space_len = len; }
            p++; len++;
        }

        if (*p && *p != '\n' && last_space) {
            /* Wrap at last space */
            len = last_space_len;
            p = last_space + 1;
        } else if (*p == '\n') {
            p++;  /* consume newline */
        }

        if (line_idx >= skip_lines) {
            if (line_drawn < max_lines) {
                /* Draw this line */
                int i;
                char linebuf[128];
                int copy_len = len < 127 ? len : 127;
                for (i = 0; i < copy_len; i++) linebuf[i] = line_start[i];
                linebuf[copy_len] = '\0';
                gfx_draw_string(x, y + line_drawn * line_h, linebuf, COL_BODY_TXT, COL_BG);
                line_drawn++;
            }
        }
        line_idx++;
    }
    return line_idx;  /* total lines */
}

static void browser_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int cx = r.x, ry = r.y;
    int w = r.w;

    /* Background */
    gfx_fill_rect(cx, ry, w, r.h, COL_BG);

    /* Header bar */
    gfx_fill_rect(cx, ry, w, URL_BAR_H, COL_HDR_BG);
    gfx_draw_string(cx + 8, ry + (URL_BAR_H - FONT_HEIGHT) / 2,
                    "Browser", COL_HDR_TXT, COL_HDR_BG);

    /* URL bar */
    int ub_y = ry + URL_BAR_H;
    int ub_x = cx + 8;
    int ub_w = w - 16 - 44;
    gfx_fill_rect(ub_x - 1, ub_y + 3, ub_w + 2, URL_BAR_H - 6, COL_URL_BORDER);
    gfx_fill_rect(ub_x, ub_y + 4, ub_w, URL_BAR_H - 8, COL_URL_BG);

    /* Truncate URL for display */
    char disp_url[64];
    int max_chars = ub_w / FONT_WIDTH - 1;
    if (max_chars < 1) max_chars = 1;
    if (max_chars > 63) max_chars = 63;
    int url_disp_start = (g_url_len > max_chars) ? (g_url_len - max_chars) : 0;
    str_copy(disp_url, g_url + url_disp_start, (uint32_t)(max_chars + 1));
    gfx_draw_string(ub_x + 4, ub_y + (URL_BAR_H - FONT_HEIGHT) / 2,
                    disp_url, COL_URL_TXT, COL_URL_BG);

    /* Go button */
    int go_x = cx + w - 48;
    gfx_fill_rect(go_x, ub_y + 4, 40, URL_BAR_H - 8, COL_GO_BG);
    gfx_draw_string(go_x + 10, ub_y + (URL_BAR_H - FONT_HEIGHT) / 2,
                    "Go", COL_GO_TXT, COL_GO_BG);

    /* Status bar */
    int sb_y = ry + r.h - STATUS_H;
    gfx_fill_rect(cx, sb_y, w, STATUS_H, COL_STATUS_BG);
    gfx_draw_string(cx + CONTENT_PAD, sb_y + (STATUS_H - FONT_HEIGHT) / 2,
                    g_loading ? "Loading..." : g_status_msg,
                    COL_STATUS_TXT, COL_STATUS_BG);

    /* Content area */
    int content_y = ub_y + URL_BAR_H + CONTENT_PAD;
    int content_h = sb_y - content_y - CONTENT_PAD;

    if (g_body_len > 0) {
        draw_wrapped_text(cx + CONTENT_PAD, content_y,
                          w - CONTENT_PAD * 2, content_h,
                          g_body, g_scroll);
    } else if (!g_loading && g_status_msg[0] == '\0') {
        gfx_draw_string(cx + CONTENT_PAD, content_y,
                        "Enter a URL above and press Go or Enter.",
                        COL_STATUS_TXT, COL_BG);
    }
}

static void browser_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }

    if (key == KEY_UP   && g_scroll > 0) { g_scroll--; return; }
    if (key == KEY_DOWN)                  { g_scroll++; return; }

    if (key == '\r' || key == '\n') {
        if (g_url_len > 0) { do_fetch(); }
        return;
    }
    if (key == '\b' || key == (char)0x7F) {
        if (g_url_len > 0) { g_url[--g_url_len] = '\0'; }
        return;
    }
    if ((unsigned char)key >= 32 && (unsigned char)key < 127) {
        if (g_url_len < URL_MAX - 1) {
            g_url[g_url_len++] = key;
            g_url[g_url_len]   = '\0';
        }
    }
}

static void browser_on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    (void)win_id;
    if (!(buttons & 1)) return;

    gui_rect_t r = gui_window_content(g_win_id);

    /* Go button click */
    int ub_y = URL_BAR_H;
    int go_x = r.w - 48;
    if (mx >= go_x && mx < go_x + 40 &&
        my >= ub_y + 4 && my < ub_y + URL_BAR_H - 4) {
        if (g_url_len > 0) do_fetch();
    }
}

static void browser_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void browser_gui_launch(void) {
    gui_rect_t r;
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_window_suggest_rect(BROWSER_W, BROWSER_H, &r);
    g_win_id = gui_window_create("Browser", r.x, r.y, r.w, r.h);
    if (g_win_id < 0) return;

    if (g_url_len == 0) {
        str_copy(g_url, "http://", URL_MAX);
        g_url_len = 7;
    }
    g_body[0] = '\0';
    g_body_len = 0;
    g_status_msg[0] = '\0';
    g_loading = 0;
    g_scroll  = 0;

    gui_window_t *w = gui_get_window(g_win_id);
    w->on_paint = browser_on_paint;
    w->on_key   = browser_on_key;
    w->on_mouse = browser_on_mouse;
    w->on_close = browser_on_close;
}
