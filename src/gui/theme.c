#include "gui/theme.h"

#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "lib/string.h"

static th_metrics_t g_metrics;
static int g_metrics_ready = 0;

static int th_corner_radius(int w, int h, int radius) {
    int limit = w < h ? w : h;
    limit /= 2;
    if (radius < 0) radius = 0;
    if (radius > limit) radius = limit;
    return radius;
}

void th_fill_rounded(int x, int y, int w, int h, int radius, uint32_t color) {
    int r = th_corner_radius(w, h, radius);

    if (w <= 0 || h <= 0) return;
    if (r <= 0) {
        gfx_fill_rect(x, y, w, h, color);
        return;
    }

    gfx_fill_rect(x + r, y, w - r * 2, h, color);
    gfx_fill_rect(x, y + r, r, h - r * 2, color);
    gfx_fill_rect(x + w - r, y + r, r, h - r * 2, color);

    for (int dy = 0; dy < r; dy++) {
        int remain = r * r - (r - dy - 1) * (r - dy - 1);
        int inset = 0;
        while (inset * inset < remain) inset++;
        inset = r - inset;
        gfx_fill_rect(x + inset, y + dy, w - inset * 2, 1, color);
        gfx_fill_rect(x + inset, y + h - 1 - dy, w - inset * 2, 1, color);
    }
}

void th_fill_rounded_alpha(int x, int y, int w, int h, int radius, uint32_t color, uint8_t alpha) {
    int r = th_corner_radius(w, h, radius);

    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (r <= 0) {
        gfx_fill_rect_alpha(x, y, w, h, color, alpha);
        return;
    }

    gfx_fill_rect_alpha(x + r, y, w - r * 2, h, color, alpha);
    gfx_fill_rect_alpha(x, y + r, r, h - r * 2, color, alpha);
    gfx_fill_rect_alpha(x + w - r, y + r, r, h - r * 2, color, alpha);

    for (int dy = 0; dy < r; dy++) {
        int remain = r * r - (r - dy - 1) * (r - dy - 1);
        int inset = 0;
        while (inset * inset < remain) inset++;
        inset = r - inset;
        gfx_fill_rect_alpha(x + inset, y + dy, w - inset * 2, 1, color, alpha);
        gfx_fill_rect_alpha(x + inset, y + h - 1 - dy, w - inset * 2, 1, color, alpha);
    }
}

void th_draw_rounded_outline(int x, int y, int w, int h, int radius, uint32_t color) {
    int r = th_corner_radius(w, h, radius);

    if (w <= 1 || h <= 1) return;
    if (r <= 0) {
        gfx_draw_rect(x, y, w, h, color);
        return;
    }

    gfx_fill_rect(x + r, y, w - r * 2, 1, color);
    gfx_fill_rect(x + r, y + h - 1, w - r * 2, 1, color);
    gfx_fill_rect(x, y + r, 1, h - r * 2, color);
    gfx_fill_rect(x + w - 1, y + r, 1, h - r * 2, color);

    for (int dy = 0; dy < r; dy++) {
        int remain = r * r - (r - dy - 1) * (r - dy - 1);
        int inset = 0;
        while (inset * inset < remain) inset++;
        inset = r - inset;
        gfx_fill_rect(x + inset, y + dy, 1, 1, color);
        gfx_fill_rect(x + w - 1 - inset, y + dy, 1, 1, color);
        gfx_fill_rect(x + inset, y + h - 1 - dy, 1, 1, color);
        gfx_fill_rect(x + w - 1 - inset, y + h - 1 - dy, 1, 1, color);
    }
}

void th_draw_soft_shadow(int x, int y, int w, int h, int radius) {
    if (w < 2 || h < 2) return;
    th_fill_rounded_alpha(x + 1, y + 2, w, h, radius, gfx_rgb(15, 23, 42), 5);
    th_fill_rounded_alpha(x + 3, y + 6, w - 2, h - 2, radius + 1, gfx_rgb(15, 23, 42), 6);
}

static int th_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static void th_fit_line(char *dst, int dst_size, const char *src, int width, int font_px) {
    int i;

    if (!dst || dst_size < 2) return;
    dst[0] = '\0';
    if (!src) return;

    for (i = 0; src[i] && i + 1 < dst_size; i++) {
        char trial[192];
        int k;

        for (k = 0; k < i + 1 && k + 1 < (int)sizeof(trial); k++) {
            trial[k] = src[k];
        }
        trial[k] = '\0';
        if (th_text_width(trial, font_px) > width) {
            break;
        }
        dst[i] = src[i];
        dst[i + 1] = '\0';
    }
}

static void th_ensure_metrics(void) {
    const gfx_display_profile_t *dp = gfx_display_profile();

    if (g_metrics_ready) return;

    if (dp->density == GFX_DENSITY_COMPACT) {
        g_metrics.gap_xs = 5;
        g_metrics.gap_sm = 7;
        g_metrics.gap_md = 11;
        g_metrics.gap_lg = 15;
        g_metrics.gap_xl = 20;
        g_metrics.min_hit = 22;
        g_metrics.button_h = 24;
        g_metrics.field_h = 28;
        g_metrics.list_row_h = 26;
        g_metrics.toolbar_h = 36;
        g_metrics.header_h = 42;
        g_metrics.status_h = 24;
        g_metrics.sidebar_w = 140;
        g_metrics.tab_h = 26;
        g_metrics.card_pad = 11;
        g_metrics.font_small = 12;
        g_metrics.font_body = 16;
        g_metrics.font_title = 20;
        g_metrics.font_hero = 28;
        g_metrics.mild_stretch_pct = 5;
    } else if (dp->density == GFX_DENSITY_NORMAL) {
        g_metrics.gap_xs = 5;
        g_metrics.gap_sm = 8;
        g_metrics.gap_md = 12;
        g_metrics.gap_lg = 16;
        g_metrics.gap_xl = 22;
        g_metrics.min_hit = 24;
        g_metrics.button_h = 26;
        g_metrics.field_h = 30;
        g_metrics.list_row_h = 28;
        g_metrics.toolbar_h = 40;
        g_metrics.header_h = 46;
        g_metrics.status_h = 24;
        g_metrics.sidebar_w = 156;
        g_metrics.tab_h = 28;
        g_metrics.card_pad = 14;
        g_metrics.font_small = 12;
        g_metrics.font_body = 16;
        g_metrics.font_title = 22;
        g_metrics.font_hero = 32;
        g_metrics.mild_stretch_pct = 6;
    } else {
        g_metrics.gap_xs = 6;
        g_metrics.gap_sm = 10;
        g_metrics.gap_md = 16;
        g_metrics.gap_lg = 20;
        g_metrics.gap_xl = 28;
        g_metrics.min_hit = 28;
        g_metrics.button_h = 28;
        g_metrics.field_h = 32;
        g_metrics.list_row_h = 30;
        g_metrics.toolbar_h = 42;
        g_metrics.header_h = 50;
        g_metrics.status_h = 26;
        g_metrics.sidebar_w = 172;
        g_metrics.tab_h = 30;
        g_metrics.card_pad = 16;
        g_metrics.font_small = 12;
        g_metrics.font_body = 16;
        g_metrics.font_title = 24;
        g_metrics.font_hero = 34;
        g_metrics.mild_stretch_pct = 8;
    }

    g_metrics_ready = 1;
}

const th_metrics_t *th_metrics(void) {
    th_ensure_metrics();
    return &g_metrics;
}

void th_refresh_metrics(void) {
    g_metrics_ready = 0;
    th_ensure_metrics();
}

th_layout_bucket_t th_layout_bucket_for_width(int width) {
    if (width < 420) return TH_LAYOUT_COMPACT;
    if (width < 760) return TH_LAYOUT_COMFORTABLE;
    return TH_LAYOUT_WIDE;
}

int th_page_header_height(void) {
    const th_metrics_t *m = th_metrics();
    return m->header_h + m->gap_lg;
}

int th_info_strip_height(void) {
    const th_metrics_t *m = th_metrics();
    return m->font_body + m->gap_md;
}

void th_measure_grid(int width, int min_cell_w, int gap, int max_cols,
                     int *out_cols, int *out_cell_w) {
    int cols = 1;
    int cell_w = width;

    if (gap < 0) gap = 0;
    if (min_cell_w < 1) min_cell_w = width;
    if (max_cols < 1) max_cols = 1;
    if (width < min_cell_w) min_cell_w = width;

    for (int candidate = max_cols; candidate >= 1; candidate--) {
        int candidate_w = (width - gap * (candidate - 1)) / candidate;
        if (candidate_w >= min_cell_w || candidate == 1) {
            cols = candidate;
            cell_w = candidate_w;
            break;
        }
    }

    if (cell_w < 1) cell_w = 1;
    if (out_cols) *out_cols = cols;
    if (out_cell_w) *out_cell_w = cell_w;
}

uint8_t th_anim_progress(uint32_t start_tick, uint32_t duration_ticks, int opening) {
    uint32_t now = timer_get_ticks();
    uint32_t elapsed = now - start_tick;
    uint32_t progress;

    if (duration_ticks == 0) {
        return opening ? 255u : 0u;
    }

    if (elapsed >= duration_ticks) {
        return opening ? 255u : 0u;
    }

    progress = (elapsed * 255u) / duration_ticks;
    if (!opening) progress = 255u - progress;
    if (progress > 255u) progress = 255u;
    return (uint8_t)progress;
}

uint8_t th_anim_ease(uint8_t progress) {
    uint32_t p = progress;
    uint32_t eased = 255u - (((255u - p) * (255u - p)) / 255u);
    if (eased > 255u) eased = 255u;
    return (uint8_t)eased;
}

int th_lerp_int(int from, int to, uint8_t progress) {
    int delta = to - from;
    return from + (delta * (int)progress) / 255;
}

uint32_t th_lerp_color(uint32_t from, uint32_t to, uint8_t progress) {
    uint8_t fr = (uint8_t)((from >> 16) & 0xFFu);
    uint8_t fg = (uint8_t)((from >> 8) & 0xFFu);
    uint8_t fb = (uint8_t)(from & 0xFFu);
    uint8_t tr = (uint8_t)((to >> 16) & 0xFFu);
    uint8_t tg = (uint8_t)((to >> 8) & 0xFFu);
    uint8_t tb = (uint8_t)(to & 0xFFu);
    uint32_t rr = (uint32_t)th_lerp_int(fr, tr, progress);
    uint32_t rg = (uint32_t)th_lerp_int(fg, tg, progress);
    uint32_t rb = (uint32_t)th_lerp_int(fb, tb, progress);

    return (rr << 16) | (rg << 8) | rb;
}

int th_text_width(const char *text, int font_px) {
    if (font_px <= 0) font_px = th_metrics()->font_body;
    return gfx_measure_text(FONT_ROLE_UI, font_px, text);
}

void th_draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg, int font_px) {
    if (font_px <= 0) font_px = th_metrics()->font_body;
    gfx_draw_string_role(x, y, text, FONT_ROLE_UI, font_px, fg, bg);
}

void th_draw_text_center(int x, int y, int w, const char *text, uint32_t fg, uint32_t bg, int font_px) {
    int tw;
    int tx;

    if (font_px <= 0) font_px = th_metrics()->font_body;
    tw = th_text_width(text, font_px);
    tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    th_draw_text(tx, y, text, fg, bg, font_px);
}

int th_measure_wrapped_height(const char *text, int width, int font_px, int max_lines) {
    char line[192];
    int len = 0;
    int lines = 0;
    int line_h;
    const char *p = text ? text : "";

    if (font_px <= 0) font_px = th_metrics()->font_body;
    if (width < 8) width = 8;
    if (max_lines <= 0) max_lines = 1024;
    line_h = gfx_font_line_height(FONT_ROLE_UI, font_px);
    if (line_h < 1) line_h = font_px;

    while (*p && lines < max_lines) {
        if (*p == '\n') {
            lines++;
            len = 0;
            p++;
            continue;
        }

        if (th_is_space(*p)) {
            if (len > 0 && len + 1 < (int)sizeof(line)) {
                line[len++] = ' ';
                line[len] = '\0';
            }
            p++;
            continue;
        }

        {
            char word[96];
            int wl = 0;
            while (*p && !th_is_space(*p) && *p != '\n' && wl + 1 < (int)sizeof(word)) {
                word[wl++] = *p++;
            }
            word[wl] = '\0';

            if (len == 0) {
                th_fit_line(line, sizeof(line), word, width, font_px);
                len = (int)str_len(line);
            } else {
                char trial[192];
                str_copy(trial, line, sizeof(trial));
                str_cat(trial, " ", sizeof(trial));
                str_cat(trial, word, sizeof(trial));
                if (th_text_width(trial, font_px) > width) {
                    lines++;
                    th_fit_line(line, sizeof(line), word, width, font_px);
                    len = (int)str_len(line);
                } else {
                    str_copy(line, trial, sizeof(line));
                    len = (int)str_len(line);
                }
            }
        }
    }

    if (len > 0 && lines < max_lines) lines++;
    if (lines < 1) lines = 1;
    return lines * line_h;
}

int th_draw_text_box(int x, int y, int w, int h, const char *text,
                     uint32_t fg, uint32_t bg, int font_px, int max_lines, int center) {
    char line[192];
    int len = 0;
    int drawn = 0;
    int line_h;
    int y_cursor = y;
    int y_limit = y + h;
    const char *p = text ? text : "";

    if (font_px <= 0) font_px = th_metrics()->font_body;
    if (w < 8 || h < 1) return 0;
    if (max_lines <= 0) max_lines = 1024;
    line_h = gfx_font_line_height(FONT_ROLE_UI, font_px);
    if (line_h < 1) line_h = font_px;

    while (*p && drawn < max_lines && y_cursor + line_h <= y_limit) {
        if (*p == '\n') {
            if (len > 0) {
                line[len] = '\0';
                if (center) th_draw_text_center(x, y_cursor, w, line, fg, bg, font_px);
                else th_draw_text(x, y_cursor, line, fg, bg, font_px);
                y_cursor += line_h;
                drawn++;
            } else {
                y_cursor += line_h;
                drawn++;
            }
            len = 0;
            p++;
            continue;
        }

        if (th_is_space(*p)) {
            if (len > 0 && len + 1 < (int)sizeof(line)) {
                line[len++] = ' ';
            }
            p++;
            continue;
        }

        {
            char word[96];
            int wl = 0;
            while (*p && !th_is_space(*p) && *p != '\n' && wl + 1 < (int)sizeof(word)) {
                word[wl++] = *p++;
            }
            word[wl] = '\0';

            if (len == 0) {
                th_fit_line(line, sizeof(line), word, w, font_px);
                len = (int)str_len(line);
            } else {
                char trial[192];
                str_copy(trial, line, sizeof(trial));
                str_cat(trial, " ", sizeof(trial));
                str_cat(trial, word, sizeof(trial));
                if (th_text_width(trial, font_px) > w) {
                    line[len] = '\0';
                    if (center) th_draw_text_center(x, y_cursor, w, line, fg, bg, font_px);
                    else th_draw_text(x, y_cursor, line, fg, bg, font_px);
                    y_cursor += line_h;
                    drawn++;
                    if (drawn >= max_lines || y_cursor + line_h > y_limit) break;
                    th_fit_line(line, sizeof(line), word, w, font_px);
                    len = (int)str_len(line);
                } else {
                    str_copy(line, trial, sizeof(line));
                    len = (int)str_len(line);
                }
            }
        }
    }

    if (len > 0 && drawn < max_lines && y_cursor + line_h <= y_limit) {
        line[len] = '\0';
        if (center) th_draw_text_center(x, y_cursor, w, line, fg, bg, font_px);
        else th_draw_text(x, y_cursor, line, fg, bg, font_px);
        drawn++;
    }

    return drawn;
}

int th_fit_aspect_rect(int outer_x, int outer_y, int outer_w, int outer_h,
                       int design_w, int design_h, int allow_stretch_pct,
                       int *out_x, int *out_y, int *out_w, int *out_h) {
    if (allow_stretch_pct < 0) allow_stretch_pct = th_metrics()->mild_stretch_pct;
    return gfx_fit_rect_aspect(outer_x, outer_y, outer_w, outer_h,
                               design_w, design_h, allow_stretch_pct,
                               out_x, out_y, out_w, out_h);
}

void th_draw_surface(int x, int y, int w, int h, uint32_t bg) {
    th_fill_rounded(x, y, w, h, 12, TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, 11, bg);
}

void th_draw_panel(int x, int y, int w, int h, const char *header) {
    const th_metrics_t *m = th_metrics();
    int hh = m->header_h;
    int radius = 16;

    th_draw_soft_shadow(x, y, w, h, radius);
    th_fill_rounded(x, y, w, h, radius, TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, radius - 1, TH_BG_PANEL);
    th_fill_rounded(x + 2, y + 2, w - 4, hh, radius - 2, TH_BG_TOOLBAR);
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, hh / 2, gfx_rgb(255, 255, 255), 72);
    gfx_fill_rect(x + 18, y + hh + 1, w - 36, 1, TH_RULE);
    if (header && header[0]) {
        th_draw_text(x + m->gap_md, y + (hh - m->font_title) / 2,
                     header, TH_TEXT, TH_BG_TOOLBAR, m->font_title);
    }
}

void th_draw_dialog(int x, int y, int w, int h, const char *title) {
    const th_metrics_t *m = th_metrics();
    int hh = m->header_h;
    int radius = 18;

    th_draw_soft_shadow(x, y, w, h, radius);
    th_fill_rounded(x, y, w, h, radius, TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, radius - 1, TH_BG_CARD);
    th_fill_rounded(x + 2, y + 2, w - 4, hh, radius - 2, TH_BG_TOOLBAR);
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, hh / 2, gfx_rgb(255, 255, 255), 68);
    gfx_fill_rect(x + 18, y + hh + 1, w - 36, 1, TH_RULE);
    if (title && title[0]) {
        th_draw_text(x + m->gap_md, y + (hh - m->font_title) / 2,
                     title, TH_TEXT, TH_BG_TOOLBAR, m->font_title);
    }
}

void th_draw_card(int x, int y, int w, int h, const char *title, uint32_t bg, int active) {
    const th_metrics_t *m = th_metrics();
    int radius = 14;

    th_draw_soft_shadow(x, y, w, h, radius);
    th_fill_rounded(x, y, w, h, radius, active ? TH_ACCENT_HOT : TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, radius - 1, bg);
    if (active) {
        th_fill_rounded_alpha(x + 1, y + 1, w - 2, 5, radius - 1, gfx_rgb(120, 176, 255), 88);
    }
    if (title && title[0]) {
        th_draw_text(x + m->card_pad, y + m->gap_sm, title, TH_TEXT, bg, m->font_body);
    }
}

void th_draw_page_header(int x, int y, int w,
                         const char *eyebrow,
                         const char *title,
                         const char *subtitle) {
    const th_metrics_t *m = th_metrics();
    int head_h = m->header_h + m->gap_lg;

    th_fill_rounded(x, y, w, head_h, 18, TH_BG_CARD);
    th_fill_rounded_alpha(x + 1, y + 1, w - 2, head_h - 2, 17, gfx_rgb(255, 255, 255), 92);
    th_fill_rounded(x, y, w, 6, 18, TH_ACCENT_HOT);
    gfx_fill_rect(x + 18, y + head_h - 1, w - 36, 1, TH_RULE);

    if (eyebrow && eyebrow[0]) {
        th_draw_text(x + m->gap_md, y + m->gap_sm, eyebrow, TH_ACCENT_DARK, TH_BG_CARD, m->font_small);
    }
    if (title && title[0]) {
        th_draw_text(x + m->gap_md, y + m->gap_sm + m->font_small + 3,
                     title, TH_TEXT, TH_BG_CARD, m->font_title);
    }
    if (subtitle && subtitle[0]) {
        th_draw_text(x + m->gap_md, y + head_h - m->font_body - m->gap_sm,
                     subtitle, TH_TEXT_DIM, TH_BG_CARD, m->font_body);
    }
}

void th_draw_info_strip(int x, int y, int w,
                        const char *left,
                        const char *center,
                        const char *right) {
    const th_metrics_t *m = th_metrics();
    int strip_h = m->font_body + m->gap_md;
    int center_w = center ? th_text_width(center, m->font_small) : 0;
    int right_w = right ? th_text_width(right, m->font_small) : 0;

    th_fill_rounded(x, y, w, strip_h, 10, TH_BG_STATUS);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, strip_h / 2, gfx_rgb(255, 255, 255), 56);
    gfx_fill_rect(x + 12, y + strip_h - 1, w - 24, 1, TH_RULE);

    if (left && left[0]) {
        th_draw_text(x + m->gap_md, y + (strip_h - m->font_small) / 2,
                     left, TH_TEXT_DIM, TH_BG_STATUS, m->font_small);
    }
    if (center && center[0]) {
        th_draw_text(x + (w - center_w) / 2, y + (strip_h - m->font_small) / 2,
                     center, TH_TEXT_DIM, TH_BG_STATUS, m->font_small);
    }
    if (right && right[0]) {
        th_draw_text(x + w - right_w - m->gap_md, y + (strip_h - m->font_small) / 2,
                     right, TH_TEXT_DIM, TH_BG_STATUS, m->font_small);
    }
}

void th_draw_empty_state(int x, int y, int w, int h,
                         const char *title,
                         const char *body) {
    const th_metrics_t *m = th_metrics();
    int title_y;
    int body_y;

    th_draw_card(x, y, w, h, 0, gfx_rgb(251, 252, 255), 0);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(255, 255, 255), TH_BG_CARD_ALT);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, h / 2, gfx_rgb(255, 255, 255), 24);

    title_y = y + h / 2 - m->font_title;
    body_y = title_y + m->font_title + m->gap_sm;
    if (title && title[0]) {
        th_draw_text_center(x + m->gap_md, title_y, w - m->gap_md * 2,
                            title, TH_TEXT, TH_BG_CARD_ALT, m->font_title);
    }
    if (body && body[0]) {
        th_draw_text_center(x + m->gap_md, body_y, w - m->gap_md * 2,
                            body, TH_TEXT_DIM, TH_BG_CARD_ALT, m->font_body);
    }
}

void th_draw_auth_card(int x, int y, int w, int h,
                       const char *title,
                       const char *subtitle) {
    const th_metrics_t *m = th_metrics();

    th_draw_soft_shadow(x, y, w, h, 22);
    th_fill_rounded(x, y, w, h, 22, TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, 21, TH_BG_CARD);
    th_fill_rounded(x + 1, y + 1, w - 2, m->header_h + m->gap_sm, 21, TH_BG_TOOLBAR);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), 120);
    th_draw_separator(x + 18, y + m->header_h + m->gap_sm, w - 36);

    if (title && title[0]) {
        th_draw_text(x + m->gap_lg, y + m->gap_md, title, TH_TEXT, TH_BG_TOOLBAR, m->font_title);
    }
    if (subtitle && subtitle[0]) {
        th_draw_text(x + m->gap_lg, y + m->gap_md + m->font_title + 2,
                     subtitle, TH_TEXT_DIM, TH_BG_TOOLBAR, m->font_small);
    }
}

void th_draw_sidebar(int x, int y, int w, int h, const char *title) {
    const th_metrics_t *m = th_metrics();

    th_fill_rounded(x, y, w, h, 14, TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, h - 2, 13, TH_BG_SIDEBAR);
    if (title && title[0]) {
        th_draw_text(x + m->gap_md, y + m->gap_md, title, TH_TEXT_DIM, TH_BG_SIDEBAR, m->font_body);
    }
}

void th_draw_toolbar(int x, int y, int w, const char *title) {
    const th_metrics_t *m = th_metrics();

    th_fill_rounded(x, y, w, m->toolbar_h, 12, TH_BG_TOOLBAR);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, m->toolbar_h / 2, gfx_rgb(255, 255, 255), 64);
    gfx_fill_rect(x + 12, y + m->toolbar_h - 1, w - 24, 1, TH_RULE);
    if (title && title[0]) {
        th_draw_text(x + m->gap_md, y + (m->toolbar_h - m->font_title) / 2,
                     title, TH_TEXT, TH_BG_TOOLBAR, m->font_title);
    }
}

void th_draw_statusbar(int x, int y, int w, int h, const char *text) {
    const th_metrics_t *m = th_metrics();
    int sh = h > 0 ? h : m->status_h;

    th_fill_rounded(x, y, w, sh, 10, TH_BG_STATUS);
    gfx_fill_rect(x + 10, y, w - 20, 1, TH_RULE);
    if (text && text[0]) {
        th_draw_text(x + m->gap_md, y + (sh - m->font_small) / 2,
                     text, TH_TEXT_DIM, TH_BG_STATUS, m->font_small);
    }
}

void th_draw_tab(int x, int y, int w, int h, const char *label, int active) {
    const th_metrics_t *m = th_metrics();
    uint32_t bottom = active ? TH_ACCENT : TH_BG_CARD_ALT;
    uint32_t fg = active ? TH_TEXT_INVERT : TH_TEXT;
    int th = h > 0 ? h : m->tab_h;

    th_fill_rounded(x, y, w, th, 12, active ? TH_ACCENT_DARK : TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, th - 2, 11, bottom);
    th_draw_text_box(x + m->gap_sm, y + (th - m->font_body) / 2,
                     w - m->gap_sm * 2, m->font_body + 2, label,
                     fg, bottom, m->font_body, 1, 1);
}

void th_draw_list_row(int x, int y, int w, int h, const char *text, int selected) {
    const th_metrics_t *m = th_metrics();
    uint32_t bg = selected ? TH_SEL_BG : TH_BG_CONTENT;
    uint32_t fg = selected ? TH_SEL_TXT : TH_TEXT;
    int rh = h > 0 ? h : m->list_row_h;

    th_fill_rounded(x, y, w, rh, 12, selected ? TH_ACCENT_HOT : TH_BORDER);
    th_fill_rounded(x + 1, y + 1, w - 2, rh - 2, 11, bg);
    if (!selected) {
        gfx_fill_rect_alpha(x + 1, y + 1, w - 2, rh / 2, gfx_rgb(255, 255, 255), 20);
    }
    if (text && text[0]) {
        th_draw_text_box(x + m->gap_md, y + m->gap_xs,
                         w - m->gap_md * 2, rh - m->gap_xs * 2,
                         text, fg, bg, m->font_body, 2, 0);
    }
}

void th_draw_button(int x, int y, int w, int h, const char *label, int hot) {
    const th_metrics_t *m = th_metrics();
    uint32_t bottom = hot ? TH_ACCENT_HOT : TH_ACCENT;
    int bh = h > 0 ? h : m->button_h;

    th_fill_rounded(x, y, w, bh, 12, hot ? TH_ACCENT_HOT : TH_ACCENT_DARK);
    th_fill_rounded(x + 1, y + 1, w - 2, bh - 2, 11, bottom);
    if (label && label[0]) {
        th_draw_text_box(x + m->gap_sm, y + (bh - m->font_body) / 2,
                         w - m->gap_sm * 2, m->font_body + 2, label,
                         TH_TEXT_INVERT, bottom, m->font_body, 1, 1);
    }
}

void th_draw_section_header(int x, int y, int w, const char *label, uint32_t bg) {
    const th_metrics_t *m = th_metrics();
    int hh = m->font_body + m->gap_sm;

    th_fill_rounded(x, y, w, hh, 10, bg);
    if (label && label[0]) {
        th_draw_text_box(x + m->gap_sm, y + (hh - m->font_body) / 2,
                         w - m->gap_sm * 2, m->font_body + 2, label,
                         TH_TEXT_INVERT, bg, m->font_body, 1, 0);
    }
}

void th_draw_separator(int x, int y, int w) {
    gfx_fill_rect(x, y, w, 1, TH_RULE);
}

void th_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg) {
    const th_metrics_t *m = th_metrics();
    int bw = th_text_width(text, m->font_small) + m->gap_md;
    int bh = m->font_small + m->gap_sm;

    th_fill_rounded(x, y, bw, bh, 9, bg);
    th_draw_text(x + m->gap_sm / 2, y + (bh - m->font_small) / 2,
                 text, fg, bg, m->font_small);
}

void th_draw_field(int x, int y, int w, const char *text, int focused, int masked) {
    const th_metrics_t *m = th_metrics();
    uint32_t edge = focused ? TH_FIELD_FOCUS : TH_FIELD_EDGE;
    int fh = m->field_h;
    int char_w = th_text_width("W", m->font_body);
    int cx = x + m->gap_sm;
    int cy = y + (fh - m->font_body) / 2;
    int len = text ? (int)str_len(text) : 0;

    th_fill_rounded(x, y, w, fh, 12, edge);
    th_fill_rounded(x + 1, y + 1, w - 2, fh - 2, 11, TH_BG_FIELD);

    for (int i = 0; i < len; i++) {
        char c = masked ? '*' : text[i];
        if (cx + char_w > x + w - m->gap_sm) break;
        gfx_draw_char_role(cx, cy, c, FONT_ROLE_UI, m->font_body, TH_TEXT, TH_BG_FIELD);
        cx += char_w;
    }
    if (focused) {
        gfx_fill_rect(cx, y + m->gap_sm / 2, 2, fh - m->gap_sm, TH_FIELD_FOCUS);
    }
}

void th_draw_table_header(int x, int y, int w, int h) {
    const th_metrics_t *m = th_metrics();
    int hh = h > 0 ? h : m->list_row_h;

    th_fill_rounded(x, y, w, hh, 12, TH_BG_CARD_ALT);
    gfx_fill_rect(x + 10, y + hh - 1, w - 20, 1, TH_RULE);
}

void th_draw_scrollbar(int x, int y, int h, int content_extent, int view_extent, int scroll) {
    int bar_h;
    int bar_y;

    if (h < 8) return;
    th_fill_rounded(x, y, 8, h, 4, gfx_rgb(236, 242, 248));
    if (content_extent <= 0 || view_extent <= 0 || content_extent <= view_extent) {
        return;
    }

    bar_h = (view_extent * h) / content_extent;
    if (bar_h < 16) bar_h = 16;
    if (bar_h > h) bar_h = h;

    bar_y = y + (scroll * (h - bar_h)) / (content_extent - view_extent);
    th_fill_rounded_alpha(x + 1, bar_y + 2, 6, bar_h, 3, gfx_rgb(15, 23, 42), 10);
    th_fill_rounded(x + 1, bar_y, 6, bar_h, 3, TH_ACCENT_HOT);
}
