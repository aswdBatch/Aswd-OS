#include "gui/theme.h"

#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "lib/string.h"

static th_metrics_t g_metrics;
static int g_metrics_ready = 0;

static void th_fill_border_box(int x, int y, int w, int h, uint32_t outer, uint32_t inner) {
    if (w < 2 || h < 2) return;
    gfx_fill_rect(x, y, w, h, outer);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, inner);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), 44);
}

static void th_shadow_box(int x, int y, int w, int h) {
    if (w < 2 || h < 2) return;
    gfx_fill_rect_alpha(x + 1, y + 2, w, h, gfx_rgb(15, 23, 42), 18);
    gfx_fill_rect_alpha(x + 3, y + 5, w, h, gfx_rgb(15, 23, 42), 14);
    gfx_fill_rect_alpha(x + 8, y + 12, w - 4, h - 4, gfx_rgb(15, 23, 42), 10);
    gfx_fill_rect_alpha(x + 2, y + h, w + 4, 2, gfx_rgb(15, 23, 42), 18);
}

static void th_draw_text_overlay(int x, int y, const char *text, uint32_t fg, int font_px) {
    if (font_px <= 0) font_px = th_metrics()->font_body;
    gfx_draw_string_role_transparent(x, y, text, FONT_ROLE_UI, font_px, fg);
}

static void th_draw_text_center_overlay(int x, int y, int w, const char *text, uint32_t fg, int font_px) {
    int tx;

    if (font_px <= 0) font_px = th_metrics()->font_body;
    tx = x + (w - th_text_width(text, font_px)) / 2;
    if (tx < x) tx = x;
    th_draw_text_overlay(tx, y, text, fg, font_px);
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

int th_fit_aspect_rect(int outer_x, int outer_y, int outer_w, int outer_h,
                       int design_w, int design_h, int allow_stretch_pct,
                       int *out_x, int *out_y, int *out_w, int *out_h) {
    if (allow_stretch_pct < 0) allow_stretch_pct = th_metrics()->mild_stretch_pct;
    return gfx_fit_rect_aspect(outer_x, outer_y, outer_w, outer_h,
                               design_w, design_h, allow_stretch_pct,
                               out_x, out_y, out_w, out_h);
}

void th_draw_surface(int x, int y, int w, int h, uint32_t bg) {
    th_fill_border_box(x, y, w, h, TH_BORDER, bg);
}

void th_draw_panel(int x, int y, int w, int h, const char *header) {
    const th_metrics_t *m = th_metrics();
    int hh = m->header_h;

    th_shadow_box(x, y, w, h);
    gfx_fill_rect(x, y, w, h, TH_BORDER);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(252, 254, 255), TH_BG_PANEL);
    gfx_fill_rect_gradient_v(x + 2, y + 2, w - 4, hh, gfx_rgb(56, 132, 226), TH_ACCENT_DARK);
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, hh / 2, gfx_rgb(255, 255, 255), 64);
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, 1, gfx_rgb(255, 255, 255), 120);
    gfx_fill_rect_alpha(x + 1, y + hh + 2, w - 2, h - hh - 3, gfx_rgb(255, 255, 255), 10);
    if (header && header[0]) {
        th_draw_text_overlay(x + m->gap_md, y + (hh - m->font_title) / 2,
                             header, TH_TEXT_INVERT, m->font_title);
    }
}

void th_draw_dialog(int x, int y, int w, int h, const char *title) {
    const th_metrics_t *m = th_metrics();
    int hh = m->header_h;

    th_shadow_box(x, y, w, h);
    gfx_fill_rect(x, y, w, h, TH_BORDER);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(253, 254, 255), TH_BG_PANEL);
    gfx_fill_rect_gradient_v(x + 2, y + 2, w - 4, hh, gfx_rgb(46, 113, 196), gfx_rgb(20, 66, 118));
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, hh / 2, gfx_rgb(255, 255, 255), 56);
    gfx_fill_rect_alpha(x + 2, y + 2, w - 4, 1, gfx_rgb(255, 255, 255), 110);
    th_draw_separator(x + 2, y + hh + 1, w - 4);
    if (title && title[0]) {
        th_draw_text_overlay(x + m->gap_md, y + (hh - m->font_title) / 2,
                             title, TH_TEXT_INVERT, m->font_title);
    }
}

void th_draw_card(int x, int y, int w, int h, const char *title, uint32_t bg, int active) {
    const th_metrics_t *m = th_metrics();

    th_shadow_box(x, y, w, h);
    gfx_fill_rect(x, y, w, h, active ? TH_ACCENT_HOT : TH_RULE);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, bg);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, h / 3, gfx_rgb(255, 255, 255), 30);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), 44);
    if (active) {
        gfx_fill_rect_gradient_h(x + 1, y + 1, w - 2, 4, gfx_rgb(120, 176, 255), TH_ACCENT_HOT);
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

    gfx_fill_rect_gradient_v(x, y, w, head_h, gfx_rgb(249, 251, 255), gfx_rgb(233, 240, 250));
    gfx_fill_rect_gradient_h(x, y, w, 4, gfx_rgb(126, 177, 255), TH_ACCENT_HOT);
    gfx_fill_rect_alpha(x, y + 4, w, head_h / 2, gfx_rgb(255, 255, 255), 38);
    gfx_fill_rect(x, y + head_h - 1, w, 1, TH_RULE);

    if (eyebrow && eyebrow[0]) {
        th_draw_text_overlay(x + m->gap_md, y + m->gap_sm, eyebrow, TH_ACCENT_DARK, m->font_small);
    }
    if (title && title[0]) {
        th_draw_text_overlay(x + m->gap_md, y + m->gap_sm + m->font_small + 3,
                             title, TH_TEXT, m->font_title);
    }
    if (subtitle && subtitle[0]) {
        th_draw_text_overlay(x + m->gap_md, y + head_h - m->font_body - m->gap_sm,
                             subtitle, TH_TEXT_DIM, m->font_body);
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

    gfx_fill_rect_gradient_v(x, y, w, strip_h, gfx_rgb(242, 246, 252), gfx_rgb(230, 237, 246));
    gfx_fill_rect_alpha(x, y, w, 1, gfx_rgb(255, 255, 255), 76);
    gfx_fill_rect(x, y + strip_h - 1, w, 1, TH_RULE);

    if (left && left[0]) {
        th_draw_text_overlay(x + m->gap_md, y + (strip_h - m->font_small) / 2,
                             left, TH_TEXT_DIM, m->font_small);
    }
    if (center && center[0]) {
        th_draw_text_overlay(x + (w - center_w) / 2, y + (strip_h - m->font_small) / 2,
                             center, TH_TEXT_DIM, m->font_small);
    }
    if (right && right[0]) {
        th_draw_text_overlay(x + w - right_w - m->gap_md, y + (strip_h - m->font_small) / 2,
                             right, TH_TEXT_DIM, m->font_small);
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
        th_draw_text_center_overlay(x + m->gap_md, title_y, w - m->gap_md * 2,
                                    title, TH_TEXT, m->font_title);
    }
    if (body && body[0]) {
        th_draw_text_center_overlay(x + m->gap_md, body_y, w - m->gap_md * 2,
                                    body, TH_TEXT_DIM, m->font_body);
    }
}

void th_draw_auth_card(int x, int y, int w, int h,
                       const char *title,
                       const char *subtitle) {
    const th_metrics_t *m = th_metrics();

    th_shadow_box(x, y, w, h);
    gfx_fill_rect(x, y, w, h, TH_BORDER);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(252, 254, 255), gfx_rgb(237, 243, 251));
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, m->header_h + m->gap_sm,
                             gfx_rgb(49, 117, 202), gfx_rgb(20, 68, 122));
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), 100);
    gfx_fill_rect_alpha(x + 1, y + m->header_h, w - 2, h - m->header_h - 1, gfx_rgb(255, 255, 255), 8);
    th_draw_separator(x + 1, y + m->header_h + m->gap_sm, w - 2);

    if (title && title[0]) {
        th_draw_text_overlay(x + m->gap_lg, y + m->gap_md, title, TH_TEXT_INVERT, m->font_title);
    }
    if (subtitle && subtitle[0]) {
        th_draw_text_overlay(x + m->gap_lg, y + m->gap_md + m->font_title + 2,
                             subtitle, gfx_rgb(204, 218, 241), m->font_small);
    }
}

void th_draw_sidebar(int x, int y, int w, int h, const char *title) {
    const th_metrics_t *m = th_metrics();

    gfx_fill_rect(x, y, w, h, TH_RULE);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2, gfx_rgb(244, 248, 252), TH_BG_SIDEBAR);
    if (title && title[0]) {
        th_draw_text(x + m->gap_md, y + m->gap_md, title, TH_TEXT_DIM, TH_BG_SIDEBAR, m->font_body);
    }
}

void th_draw_toolbar(int x, int y, int w, const char *title) {
    const th_metrics_t *m = th_metrics();

    gfx_fill_rect_gradient_v(x, y, w, m->toolbar_h, gfx_rgb(50, 120, 206), gfx_rgb(22, 72, 132));
    gfx_fill_rect_alpha(x, y, w, 1, gfx_rgb(255, 255, 255), 100);
    gfx_fill_rect(x, y + m->toolbar_h - 1, w, 1, gfx_rgb(53, 96, 165));
    if (title && title[0]) {
        th_draw_text_overlay(x + m->gap_md, y + (m->toolbar_h - m->font_title) / 2,
                             title, TH_TEXT_INVERT, m->font_title);
    }
}

void th_draw_statusbar(int x, int y, int w, int h, const char *text) {
    const th_metrics_t *m = th_metrics();
    int sh = h > 0 ? h : m->status_h;

    gfx_fill_rect_gradient_v(x, y, w, sh, gfx_rgb(238, 243, 249), TH_BG_STATUS);
    gfx_fill_rect(x, y, w, 1, TH_RULE);
    if (text && text[0]) {
        th_draw_text(x + m->gap_md, y + (sh - m->font_small) / 2,
                     text, TH_TEXT_DIM, TH_BG_STATUS, m->font_small);
    }
}

void th_draw_tab(int x, int y, int w, int h, const char *label, int active) {
    const th_metrics_t *m = th_metrics();
    uint32_t top = active ? gfx_rgb(88, 155, 255) : gfx_rgb(233, 239, 247);
    uint32_t bottom = active ? TH_ACCENT_HOT : gfx_rgb(210, 220, 235);
    uint32_t fg = active ? TH_TEXT_INVERT : TH_TEXT;
    int th = h > 0 ? h : m->tab_h;

    gfx_fill_rect(x, y, w, th, active ? TH_ACCENT_DARK : TH_RULE);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, th - 2, top, bottom);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, 1, gfx_rgb(255, 255, 255), active ? 100 : 56);
    th_draw_text_center_overlay(x, y + (th - m->font_body) / 2, w, label, fg, m->font_body);
}

void th_draw_list_row(int x, int y, int w, int h, const char *text, int selected) {
    const th_metrics_t *m = th_metrics();
    uint32_t bg = selected ? TH_SEL_BG : TH_BG_CONTENT;
    uint32_t fg = selected ? TH_SEL_TXT : TH_TEXT;
    int rh = h > 0 ? h : m->list_row_h;

    gfx_fill_rect(x, y, w, rh, bg);
    if (!selected) {
        gfx_fill_rect_alpha(x, y, w, rh / 2, gfx_rgb(255, 255, 255), 14);
    }
    gfx_fill_rect(x, y + rh - 1, w, 1, selected ? TH_ACCENT_HOT : TH_RULE);
    if (text && text[0]) {
        th_draw_text(x + m->gap_md, y + (rh - m->font_body) / 2, text, fg, bg, m->font_body);
    }
}

void th_draw_button(int x, int y, int w, int h, const char *label, int hot) {
    const th_metrics_t *m = th_metrics();
    uint32_t top = hot ? gfx_rgb(97, 163, 255) : gfx_rgb(57, 134, 229);
    uint32_t bottom = hot ? TH_ACCENT_HOT : TH_ACCENT;
    int bh = h > 0 ? h : m->button_h;

    th_shadow_box(x, y, w, bh);
    gfx_fill_rect(x, y, w, bh, TH_ACCENT_DARK);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, bh - 2, top, bottom);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, bh / 2, gfx_rgb(255, 255, 255), 34);
    if (label && label[0]) {
        th_draw_text_center_overlay(x, y + (bh - m->font_body) / 2, w,
                                    label, TH_TEXT_INVERT, m->font_body);
    }
}

void th_draw_section_header(int x, int y, int w, const char *label, uint32_t bg) {
    const th_metrics_t *m = th_metrics();
    int hh = m->font_body + m->gap_sm;

    gfx_fill_rect(x, y, w, hh, bg);
    if (label && label[0]) {
        th_draw_text(x + m->gap_sm, y + (hh - m->font_body) / 2,
                     label, TH_TEXT_INVERT, bg, m->font_body);
    }
}

void th_draw_separator(int x, int y, int w) {
    gfx_fill_rect(x, y, w, 1, TH_RULE);
}

void th_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg) {
    const th_metrics_t *m = th_metrics();
    int bw = th_text_width(text, m->font_small) + m->gap_md;
    int bh = m->font_small + m->gap_sm;

    th_fill_border_box(x, y, bw, bh, bg, bg);
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

    gfx_fill_rect_alpha(x + 2, y + 2, w, fh, gfx_rgb(15, 23, 42), 12);
    th_fill_border_box(x, y, w, fh, edge, TH_BG_FIELD);
    gfx_fill_rect_alpha(x + 1, y + 1, w - 2, fh / 2, gfx_rgb(255, 255, 255), 22);

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

    gfx_fill_rect_gradient_v(x, y, w, hh, gfx_rgb(241, 245, 252), gfx_rgb(226, 234, 246));
    gfx_fill_rect(x, y + hh - 1, w, 1, TH_RULE);
}

void th_draw_scrollbar(int x, int y, int h, int content_extent, int view_extent, int scroll) {
    int bar_h;
    int bar_y;

    if (h < 8) return;
    gfx_fill_rect_gradient_v(x, y, 8, h, gfx_rgb(242, 247, 252), gfx_rgb(227, 234, 244));
    if (content_extent <= 0 || view_extent <= 0 || content_extent <= view_extent) {
        return;
    }

    bar_h = (view_extent * h) / content_extent;
    if (bar_h < 16) bar_h = 16;
    if (bar_h > h) bar_h = h;

    bar_y = y + (scroll * (h - bar_h)) / (content_extent - view_extent);
    gfx_fill_rect_alpha(x + 2, bar_y + 2, 6, bar_h, gfx_rgb(15, 23, 42), 18);
    gfx_fill_rect_gradient_v(x + 1, bar_y, 6, bar_h, gfx_rgb(90, 155, 255), TH_ACCENT_HOT);
}
