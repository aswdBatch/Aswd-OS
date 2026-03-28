#include "auth/auth_gui.h"

#include <stdint.h>

#include "auth/auth_store.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "usb/usb.h"
#include "users/users.h"

#define PAD_X 20
#define TILE_GAP 8
#define PIN_MAX 8
#define NAME_MAX 8
#define FRAME_PAD_R 10
#define FRAME_PAD_B 10

#define COL_PANEL_EDGE gfx_rgb(70, 82, 102)
#define COL_PANEL_BG_TOP gfx_rgb(248, 250, 253)
#define COL_PANEL_BG_BOTTOM gfx_rgb(233, 238, 246)
#define COL_PANEL_RULE gfx_rgb(201, 211, 224)
#define COL_TEXT gfx_rgb(33, 44, 60)
#define COL_TEXT_DIM gfx_rgb(98, 112, 133)
#define COL_TEXT_HINT gfx_rgb(118, 134, 156)
#define COL_FIELD_BG gfx_rgb(255, 255, 255)
#define COL_FIELD_EDGE gfx_rgb(140, 155, 177)
#define COL_ERROR gfx_rgb(180, 52, 72)

typedef struct {
    uint32_t bg_top;
    uint32_t bg_bottom;
    uint32_t band_top;
    uint32_t band_bottom;
    uint32_t floor_top;
    uint32_t floor_bottom;
    uint32_t accent_top;
    uint32_t accent_bottom;
    uint32_t accent_text;
    uint32_t accent_subtext;
    uint32_t row_bg;
    uint32_t row_edge;
    uint32_t row_sel_top;
    uint32_t row_sel_bottom;
    uint32_t row_sel_edge;
    uint32_t row_sel_text;
    uint32_t row_sel_subtext;
    uint32_t focus_edge;
} auth_palette_t;

typedef struct {
    int selected;
    char pin[PIN_MAX + 1];
    int pin_len;
    char error[64];
    int has_error;
} login_state_t;

typedef struct {
    char name[NAME_MAX + 1];
    int  name_len;
    char msg[64];
    int  has_error;
} setup_state_t;

typedef struct {
    gui_rect_t brand_rect;
    gui_rect_t panel_rect;
    gui_rect_t frame_rect;
    int header_h;
    int inner_x;
    int inner_w;
    int tile_h;
    int tiles_y;
    int pin_label_y;
    int pin_field_y;
    int error_y;
    int hint_y;
} login_layout_t;

typedef struct {
    gui_rect_t brand_rect;
    gui_rect_t panel_rect;
    gui_rect_t frame_rect;
    int header_h;
    int inner_x;
    int inner_w;
    int body_y;
    int field_y;
    int button_y;
    int msg_y;
    int hint_y;
} setup_layout_t;

static void auth_log(const char *msg) {
    serial_write("[auth] ");
    serial_write(msg);
    serial_write("\n");
}

static void auth_log_key(const char *screen, char key) {
    char buf[48];

    str_copy(buf, screen, sizeof(buf));
    str_cat(buf, " key ", sizeof(buf));
    if (key == KEY_UP) {
        str_cat(buf, "up", sizeof(buf));
    } else if (key == KEY_DOWN) {
        str_cat(buf, "down", sizeof(buf));
    } else if (key == '\r' || key == '\n') {
        str_cat(buf, "enter", sizeof(buf));
    } else if (key == '\b' || key == (char)0x7F) {
        str_cat(buf, "backspace", sizeof(buf));
    } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
        str_cat(buf, "char", sizeof(buf));
    } else {
        str_cat(buf, "other", sizeof(buf));
    }
    auth_log(buf);
}

static int rect_right(gui_rect_t r) { return r.x + r.w; }
static int rect_bottom(gui_rect_t r) { return r.y + r.h; }

static gui_rect_t rect_union(gui_rect_t a, gui_rect_t b) {
    gui_rect_t out;
    int right;
    int bottom;

    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;

    out.x = a.x < b.x ? a.x : b.x;
    out.y = a.y < b.y ? a.y : b.y;
    right = rect_right(a) > rect_right(b) ? rect_right(a) : rect_right(b);
    bottom = rect_bottom(a) > rect_bottom(b) ? rect_bottom(a) : rect_bottom(b);
    out.w = right - out.x;
    out.h = bottom - out.y;
    return out;
}

static int intersect_rect(gui_rect_t a, gui_rect_t b, gui_rect_t *out) {
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = rect_right(a) < rect_right(b) ? rect_right(a) : rect_right(b);
    int y1 = rect_bottom(a) < rect_bottom(b) ? rect_bottom(a) : rect_bottom(b);

    if (x1 <= x0 || y1 <= y0) {
        if (out) *out = (gui_rect_t){0, 0, 0, 0};
        return 0;
    }
    if (out) {
        out->x = x0;
        out->y = y0;
        out->w = x1 - x0;
        out->h = y1 - y0;
    }
    return 1;
}

static uint32_t lerp_color(uint32_t from, uint32_t to, int num, int den) {
    uint32_t fr = (from >> 16) & 0xFFu;
    uint32_t fg = (from >> 8) & 0xFFu;
    uint32_t fb = from & 0xFFu;
    uint32_t tr = (to >> 16) & 0xFFu;
    uint32_t tg = (to >> 8) & 0xFFu;
    uint32_t tb = to & 0xFFu;
    uint32_t rr;
    uint32_t rg;
    uint32_t rb;

    if (den <= 0) return from;
    if (num < 0) num = 0;
    if (num > den) num = den;

    rr = (fr * (uint32_t)(den - num) + tr * (uint32_t)num) / (uint32_t)den;
    rg = (fg * (uint32_t)(den - num) + tg * (uint32_t)num) / (uint32_t)den;
    rb = (fb * (uint32_t)(den - num) + tb * (uint32_t)num) / (uint32_t)den;
    return (rr << 16) | (rg << 8) | rb;
}

static void fill_rect_clipped(int x, int y, int w, int h, uint32_t color, const gui_rect_t *clip) {
    gui_rect_t shape = { x, y, w, h };
    gui_rect_t hit;

    if (!clip) {
        gfx_fill_rect(x, y, w, h, color);
        return;
    }
    if (!intersect_rect(shape, *clip, &hit)) return;
    gfx_fill_rect(hit.x, hit.y, hit.w, hit.h, color);
}

static void fill_gradient_v_clipped(int x, int y, int w, int h,
                                    uint32_t top, uint32_t bottom,
                                    const gui_rect_t *clip) {
    gui_rect_t shape = { x, y, w, h };
    gui_rect_t hit;
    uint32_t clip_top;
    uint32_t clip_bottom;

    if (h <= 0 || w <= 0) return;
    if (!clip) {
        gfx_fill_rect_gradient_v(x, y, w, h, top, bottom);
        return;
    }
    if (!intersect_rect(shape, *clip, &hit)) return;
    if (h == 1) {
        gfx_fill_rect(hit.x, hit.y, hit.w, hit.h, top);
        return;
    }

    clip_top = lerp_color(top, bottom, hit.y - y, h - 1);
    clip_bottom = lerp_color(top, bottom, rect_bottom(hit) - y - 1, h - 1);
    gfx_fill_rect_gradient_v(hit.x, hit.y, hit.w, hit.h, clip_top, clip_bottom);
}

static void auth_palette_for_theme(auth_palette_t *out) {
    gui_background_theme_t theme = gui_get_background_theme();

    out->row_bg = gfx_rgb(247, 249, 252);
    out->row_edge = gfx_rgb(211, 219, 231);
    out->focus_edge = gfx_rgb(54, 122, 214);

    switch (theme) {
        case GUI_BG_THEME_GLASS:
            out->bg_top = gfx_rgb(24, 72, 118);
            out->bg_bottom = gfx_rgb(10, 19, 31);
            out->band_top = gfx_rgb(44, 110, 190);
            out->band_bottom = gfx_rgb(20, 48, 82);
            out->floor_top = gfx_rgb(27, 40, 58);
            out->floor_bottom = gfx_rgb(13, 20, 28);
            out->accent_top = gfx_rgb(81, 161, 245);
            out->accent_bottom = gfx_rgb(33, 112, 194);
            break;
        case GUI_BG_THEME_STUDIO:
            out->bg_top = gfx_rgb(44, 47, 78);
            out->bg_bottom = gfx_rgb(15, 15, 24);
            out->band_top = gfx_rgb(104, 75, 164);
            out->band_bottom = gfx_rgb(48, 37, 81);
            out->floor_top = gfx_rgb(33, 28, 42);
            out->floor_bottom = gfx_rgb(16, 13, 20);
            out->accent_top = gfx_rgb(144, 102, 228);
            out->accent_bottom = gfx_rgb(86, 62, 156);
            break;
        case GUI_BG_THEME_SUNSET:
            out->bg_top = gfx_rgb(120, 63, 50);
            out->bg_bottom = gfx_rgb(28, 14, 24);
            out->band_top = gfx_rgb(189, 105, 66);
            out->band_bottom = gfx_rgb(86, 39, 44);
            out->floor_top = gfx_rgb(46, 28, 31);
            out->floor_bottom = gfx_rgb(21, 12, 17);
            out->accent_top = gfx_rgb(223, 128, 92);
            out->accent_bottom = gfx_rgb(172, 73, 78);
            break;
        case GUI_BG_THEME_OCEAN:
            out->bg_top = gfx_rgb(16, 82, 94);
            out->bg_bottom = gfx_rgb(7, 18, 27);
            out->band_top = gfx_rgb(27, 138, 149);
            out->band_bottom = gfx_rgb(12, 58, 66);
            out->floor_top = gfx_rgb(23, 37, 43);
            out->floor_bottom = gfx_rgb(10, 18, 21);
            out->accent_top = gfx_rgb(43, 176, 188);
            out->accent_bottom = gfx_rgb(16, 124, 136);
            break;
        case GUI_BG_THEME_NEUTRAL:
            out->bg_top = gfx_rgb(74, 82, 95);
            out->bg_bottom = gfx_rgb(14, 18, 24);
            out->band_top = gfx_rgb(129, 143, 164);
            out->band_bottom = gfx_rgb(55, 65, 78);
            out->floor_top = gfx_rgb(33, 38, 46);
            out->floor_bottom = gfx_rgb(17, 20, 26);
            out->accent_top = gfx_rgb(102, 153, 214);
            out->accent_bottom = gfx_rgb(58, 108, 171);
            break;
        case GUI_BG_THEME_MINT:
        default:
            out->bg_top = gfx_rgb(28, 78, 70);
            out->bg_bottom = gfx_rgb(9, 18, 24);
            out->band_top = gfx_rgb(48, 124, 102);
            out->band_bottom = gfx_rgb(20, 50, 48);
            out->floor_top = gfx_rgb(24, 41, 46);
            out->floor_bottom = gfx_rgb(11, 18, 22);
            out->accent_top = gfx_rgb(86, 176, 142);
            out->accent_bottom = gfx_rgb(35, 115, 86);
            break;
    }

    out->accent_text = gfx_rgb(255, 255, 255);
    out->accent_subtext = gfx_rgb(223, 233, 246);
    out->row_sel_top = out->accent_top;
    out->row_sel_bottom = out->accent_bottom;
    out->row_sel_edge = out->accent_bottom;
    out->row_sel_text = gfx_rgb(255, 255, 255);
    out->row_sel_subtext = gfx_rgb(227, 236, 246);
}

static void draw_auth_backdrop(const gui_rect_t *clip) {
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    auth_palette_t palette;

    auth_palette_for_theme(&palette);
    fill_gradient_v_clipped(0, 0, sw, sh, palette.bg_top, palette.bg_bottom, clip);
    fill_gradient_v_clipped(0, 0, sw, sh / 3, palette.band_top, palette.band_bottom, clip);
    fill_gradient_v_clipped(0, sh - sh / 4, sw, sh / 4,
                            palette.floor_top, palette.floor_bottom, clip);
    fill_rect_clipped(0, sh / 2, sw, 1, gfx_rgb(36, 48, 66), clip);
}

static void draw_brand_box(const gui_rect_t *rect,
                           const char *title,
                           const char *subtitle,
                           const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();

    gfx_fill_rect(rect->x, rect->y, rect->w, rect->h, palette->accent_bottom);
    gfx_fill_rect_gradient_v(rect->x + 1, rect->y + 1, rect->w - 2, rect->h - 2,
                             palette->accent_top, palette->accent_bottom);
    gfx_fill_rect(rect->x + 1, rect->y + rect->h - 2, rect->w - 2, 1, gfx_rgb(12, 25, 42));

    gfx_draw_string_role_transparent(rect->x + 14, rect->y + 8,
                                     "AswdOS", FONT_ROLE_UI, tm->font_hero,
                                     palette->accent_text);
    gfx_draw_string_role_transparent(rect->x + 14,
                                     rect->y + 10 + tm->font_hero,
                                     title,
                                     FONT_ROLE_UI,
                                     tm->font_small,
                                     palette->accent_subtext);
    if (subtitle && subtitle[0]) {
        gfx_draw_string_role_transparent(rect->x + 14,
                                         rect->y + 12 + tm->font_hero + tm->font_small,
                                         subtitle,
                                         FONT_ROLE_UI,
                                         tm->font_small,
                                         palette->accent_subtext);
    }
}

static void draw_panel_shell(const gui_rect_t *rect,
                             int header_h,
                             const char *title,
                             const char *subtitle,
                             const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();

    gfx_fill_rect(rect->x, rect->y, rect->w, rect->h, COL_PANEL_EDGE);
    gfx_fill_rect_gradient_v(rect->x + 1, rect->y + 1, rect->w - 2, rect->h - 2,
                             COL_PANEL_BG_TOP, COL_PANEL_BG_BOTTOM);
    gfx_fill_rect_gradient_v(rect->x + 1, rect->y + 1, rect->w - 2, header_h,
                             palette->accent_top, palette->accent_bottom);
    gfx_fill_rect(rect->x + 1, rect->y + header_h, rect->w - 2, 1, COL_PANEL_RULE);

    gfx_draw_string_role_transparent(rect->x + PAD_X, rect->y + 10,
                                     title, FONT_ROLE_UI, tm->font_title,
                                     palette->accent_text);
    if (subtitle && subtitle[0]) {
        gfx_draw_string_role_transparent(rect->x + PAD_X,
                                         rect->y + 12 + tm->font_title,
                                         subtitle,
                                         FONT_ROLE_UI,
                                         tm->font_small,
                                         palette->accent_subtext);
    }
}

static void draw_row_box(int x, int y, int w, int h,
                         const char *title,
                         const char *subtitle,
                         int selected,
                         const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();
    uint32_t edge = selected ? palette->row_sel_edge : palette->row_edge;
    uint32_t text = selected ? palette->row_sel_text : COL_TEXT;
    uint32_t dim = selected ? palette->row_sel_subtext : COL_TEXT_DIM;
    uint32_t bg = selected ? palette->row_sel_bottom : palette->row_bg;

    gfx_fill_rect(x, y, w, h, edge);
    if (selected) {
        gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2,
                                 palette->row_sel_top, palette->row_sel_bottom);
        gfx_fill_rect(x + 1, y + 1, 5, h - 2, gfx_rgb(255, 255, 255));
    } else {
        gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, palette->row_bg);
        gfx_fill_rect(x + 1, y + 1, 5, h - 2, gfx_rgb(214, 222, 233));
    }

    gfx_draw_string_role_transparent(x + 14, y + 8, title, FONT_ROLE_UI, tm->font_body, text);
    gfx_draw_string_role_transparent(x + 14, y + 10 + tm->font_body,
                                     subtitle, FONT_ROLE_UI, tm->font_small, dim);
    (void)bg;
}

static void draw_field_box(int x, int y, int w, const char *text,
                           int masked, int focused, uint32_t focus_edge) {
    const th_metrics_t *tm = th_metrics();
    int fh = tm->field_h;
    int cursor_x = x + 10;
    int baseline_y = y + (fh - tm->font_body) / 2;

    gfx_fill_rect(x, y, w, fh, focused ? focus_edge : COL_FIELD_EDGE);
    gfx_fill_rect(x + 1, y + 1, w - 2, fh - 2, COL_FIELD_BG);

    for (int i = 0; text && text[i]; i++) {
        char c = masked ? '*' : text[i];
        char buf[2];
        int advance;

        buf[0] = c;
        buf[1] = '\0';
        gfx_draw_string_role_transparent(cursor_x, baseline_y,
                                         buf, FONT_ROLE_UI, tm->font_body, COL_TEXT);
        advance = gfx_measure_text(FONT_ROLE_UI, tm->font_body, buf);
        if (advance < 1) advance = tm->font_body / 2;
        cursor_x += advance;
        if (cursor_x >= x + w - 12) break;
    }

    if (focused) {
        gfx_fill_rect(cursor_x, y + 6, 2, fh - 12, focus_edge);
    }
}

static void draw_button_box(int x, int y, int w, int h,
                            const char *label,
                            const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();
    int tx = x + (w - gfx_measure_text(FONT_ROLE_UI, tm->font_body, label)) / 2;
    int ty = y + (h - tm->font_body) / 2;

    gfx_fill_rect(x, y, w, h, palette->accent_bottom);
    gfx_fill_rect_gradient_v(x + 1, y + 1, w - 2, h - 2,
                             palette->accent_top, palette->accent_bottom);
    gfx_draw_string_role_transparent(tx, ty, label, FONT_ROLE_UI, tm->font_body,
                                     palette->accent_text);
}

static int tile_count(void) { return 2 + users_count(); }

static const char *tile_label(int idx) {
    if (idx == 0) return "Guest";
    if (idx == 1) return "devacc";
    return users_name_at(idx - 2);
}

static const char *tile_subtitle(int idx) {
    if (idx == 0) return "Quick guest access";
    if (idx == 1) return "Development account";
    return "Local user account";
}

static int auth_panel_w(int sw) {
    int pw = sw / 3 + 24;
    if (pw < 380) pw = 380;
    if (pw > 500) pw = 500;
    return pw;
}

static login_layout_t login_layout_for_state(const login_state_t *s) {
    const th_metrics_t *tm = th_metrics();
    login_layout_t layout;
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int count = tile_count();
    int panel_w = auth_panel_w(sw);
    int rows_h;
    int pin_h = 0;
    int brand_h;
    int brand_w;
    int frame_right;
    int frame_bottom;
    int panel_y;

    layout.tile_h = tm->list_row_h + tm->gap_sm + 8;
    rows_h = count * layout.tile_h + (count - 1) * TILE_GAP;
    layout.header_h = tm->header_h + tm->gap_sm;
    if (s->selected == 1) {
        pin_h = tm->gap_md + tm->font_body + 6 + tm->field_h;
    }

    layout.panel_rect.w = panel_w;
    layout.panel_rect.h = layout.header_h + tm->gap_lg + rows_h + pin_h + 46;
    layout.panel_rect.x = (sw - layout.panel_rect.w) / 2;
    panel_y = (sh - layout.panel_rect.h) / 2;
    if (panel_y < 88) panel_y = 88;
    layout.panel_rect.y = panel_y;

    brand_h = tm->font_hero + tm->font_small * 2 + 24;
    brand_w = panel_w - 28;
    if (brand_w < 300) brand_w = panel_w;
    layout.brand_rect.w = brand_w;
    layout.brand_rect.h = brand_h;
    layout.brand_rect.x = layout.panel_rect.x + (panel_w - brand_w) / 2;
    layout.brand_rect.y = layout.panel_rect.y - brand_h - tm->gap_sm;
    if (layout.brand_rect.y < 18) {
        layout.brand_rect.y = 18;
    }

    layout.inner_x = layout.panel_rect.x + PAD_X;
    layout.inner_w = layout.panel_rect.w - PAD_X * 2;
    layout.tiles_y = layout.panel_rect.y + layout.header_h + tm->gap_md;
    layout.pin_label_y = layout.tiles_y + rows_h + tm->gap_md;
    layout.pin_field_y = layout.pin_label_y + tm->font_body + 6;
    layout.error_y = layout.panel_rect.y + layout.panel_rect.h - 38;
    layout.hint_y = layout.panel_rect.y + layout.panel_rect.h - 18;

    layout.frame_rect.x = layout.brand_rect.x < layout.panel_rect.x
        ? layout.brand_rect.x : layout.panel_rect.x;
    layout.frame_rect.y = layout.brand_rect.y;
    frame_right = rect_right(layout.brand_rect) > rect_right(layout.panel_rect)
        ? rect_right(layout.brand_rect) : rect_right(layout.panel_rect);
    frame_bottom = rect_bottom(layout.panel_rect) + FRAME_PAD_B;
    layout.frame_rect.w = frame_right - layout.frame_rect.x + FRAME_PAD_R;
    layout.frame_rect.h = frame_bottom - layout.frame_rect.y;
    return layout;
}

static setup_layout_t setup_layout_for_state(const setup_state_t *s) {
    const th_metrics_t *tm = th_metrics();
    setup_layout_t layout;
    int sw = (int)gfx_width();
    int sh = (int)gfx_height();
    int panel_w = auth_panel_w(sw);
    int brand_h;
    int brand_w;
    int frame_right;
    int frame_bottom;

    layout.header_h = tm->header_h + tm->gap_sm;
    layout.panel_rect.w = panel_w;
    layout.panel_rect.h = 236;
    layout.panel_rect.x = (sw - panel_w) / 2;
    layout.panel_rect.y = (sh - layout.panel_rect.h) / 2;
    if (layout.panel_rect.y < 92) layout.panel_rect.y = 92;

    brand_h = tm->font_title + tm->font_small * 2 + 24;
    brand_w = panel_w - 28;
    if (brand_w < 300) brand_w = panel_w;
    layout.brand_rect.w = brand_w;
    layout.brand_rect.h = brand_h;
    layout.brand_rect.x = layout.panel_rect.x + (panel_w - brand_w) / 2;
    layout.brand_rect.y = layout.panel_rect.y - brand_h - tm->gap_sm;
    if (layout.brand_rect.y < 18) {
        layout.brand_rect.y = 18;
    }

    layout.inner_x = layout.panel_rect.x + PAD_X;
    layout.inner_w = layout.panel_rect.w - PAD_X * 2;
    layout.body_y = layout.panel_rect.y + layout.header_h + tm->gap_md;
    layout.field_y = layout.body_y + tm->font_body * 2 + 18;
    layout.button_y = layout.field_y + tm->field_h + 16;
    layout.msg_y = layout.panel_rect.y + layout.panel_rect.h - 38;
    layout.hint_y = layout.panel_rect.y + layout.panel_rect.h - 18;

    layout.frame_rect.x = layout.brand_rect.x < layout.panel_rect.x
        ? layout.brand_rect.x : layout.panel_rect.x;
    layout.frame_rect.y = layout.brand_rect.y;
    frame_right = rect_right(layout.brand_rect) > rect_right(layout.panel_rect)
        ? rect_right(layout.brand_rect) : rect_right(layout.panel_rect);
    frame_bottom = rect_bottom(layout.panel_rect) + FRAME_PAD_B;
    layout.frame_rect.w = frame_right - layout.frame_rect.x + FRAME_PAD_R;
    layout.frame_rect.h = frame_bottom - layout.frame_rect.y;
    (void)s;
    return layout;
}

static void draw_login_frame(const login_state_t *s,
                             const login_layout_t *layout,
                             const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();
    int count = tile_count();

    draw_brand_box(&layout->brand_rect,
                   "Sign in to reach the desktop",
                   "Use the keyboard to pick an account and continue.",
                   palette);
    draw_panel_shell(&layout->panel_rect,
                     layout->header_h,
                     "Sign in",
                     "Choose an account and continue into the desktop.",
                     palette);

    for (int i = 0; i < count; i++) {
        int ty = layout->tiles_y + i * (layout->tile_h + TILE_GAP);
        draw_row_box(layout->inner_x, ty, layout->inner_w, layout->tile_h,
                     tile_label(i), tile_subtitle(i), i == s->selected, palette);
    }

    if (s->selected == 1) {
        gfx_draw_string_role_transparent(layout->inner_x, layout->pin_label_y,
                                         "PIN", FONT_ROLE_UI, tm->font_body, COL_TEXT_DIM);
        draw_field_box(layout->inner_x, layout->pin_field_y, layout->inner_w,
                       s->pin, 1, 1, palette->focus_edge);
    }

    if (s->has_error && s->error[0]) {
        gfx_draw_string_role_transparent(layout->inner_x, layout->error_y,
                                         s->error, FONT_ROLE_UI, tm->font_small, COL_ERROR);
    }

    gfx_draw_string_role_transparent(layout->inner_x, layout->hint_y,
                                     "Up/Down: select   Enter: confirm",
                                     FONT_ROLE_UI, tm->font_small, COL_TEXT_HINT);
}

static void draw_setup_frame(const setup_state_t *s,
                             const setup_layout_t *layout,
                             const auth_palette_t *palette) {
    const th_metrics_t *tm = th_metrics();

    draw_brand_box(&layout->brand_rect,
                   "Create the first admin account",
                   "Finish setup, then continue into the desktop.",
                   palette);
    draw_panel_shell(&layout->panel_rect,
                     layout->header_h,
                     "First boot setup",
                     "Create the first admin account before entering the desktop.",
                     palette);

    gfx_draw_string_role_transparent(layout->inner_x, layout->body_y,
                                     "Create an admin account to continue.",
                                     FONT_ROLE_UI, tm->font_body, COL_TEXT_DIM);
    gfx_draw_string_role_transparent(layout->inner_x, layout->body_y + tm->font_body + 10,
                                     "Admin name", FONT_ROLE_UI, tm->font_body, COL_TEXT_DIM);
    draw_field_box(layout->inner_x, layout->field_y, layout->inner_w,
                   s->name, 0, 1, palette->focus_edge);

    if (s->name_len > 0) {
        draw_button_box(layout->inner_x, layout->button_y, 88, tm->button_h,
                        "Create", palette);
    }

    if (s->msg[0]) {
        gfx_draw_string_role_transparent(layout->inner_x, layout->msg_y,
                                         s->msg,
                                         FONT_ROLE_UI,
                                         tm->font_small,
                                         s->has_error ? COL_ERROR : COL_TEXT_HINT);
    }

    gfx_draw_string_role_transparent(layout->inner_x, layout->hint_y,
                                     "A-Z / 0-9, max 8 chars   Enter: confirm",
                                     FONT_ROLE_UI, tm->font_small, COL_TEXT_HINT);
}

static void present_full_screen(const char *screen) {
    auth_log(screen);
    auth_log("swap begin");
    gfx_swap();
    auth_log("swap end");
}

static void present_dirty_rect(const char *screen, const gui_rect_t *rect) {
    (void)screen;
    auth_log("present rect");
    gfx_present_rect(rect->x, rect->y, rect->w, rect->h);
}

static void redraw_login(const login_state_t *s, gui_rect_t *prev_rect, int full_screen) {
    login_layout_t layout = login_layout_for_state(s);
    auth_palette_t palette;
    gui_rect_t dirty = layout.frame_rect;

    auth_palette_for_theme(&palette);
    if (!full_screen) {
        dirty = rect_union(*prev_rect, layout.frame_rect);
    }

    auth_log(full_screen ? "login draw begin" : "login redraw begin");
    draw_auth_backdrop(full_screen ? 0 : &dirty);
    draw_login_frame(s, &layout, &palette);
    auth_log(full_screen ? "login draw end" : "login redraw end");

    if (full_screen) {
        present_full_screen("login");
    } else {
        present_dirty_rect("login", &dirty);
    }

    *prev_rect = layout.frame_rect;
}

static void redraw_setup(const setup_state_t *s, gui_rect_t *prev_rect, int full_screen) {
    setup_layout_t layout = setup_layout_for_state(s);
    auth_palette_t palette;
    gui_rect_t dirty = layout.frame_rect;

    auth_palette_for_theme(&palette);
    if (!full_screen) {
        dirty = rect_union(*prev_rect, layout.frame_rect);
    }

    auth_log(full_screen ? "setup draw begin" : "setup redraw begin");
    draw_auth_backdrop(full_screen ? 0 : &dirty);
    draw_setup_frame(s, &layout, &palette);
    auth_log(full_screen ? "setup draw end" : "setup redraw end");

    if (full_screen) {
        present_full_screen("setup");
    } else {
        present_dirty_rect("setup", &dirty);
    }

    *prev_rect = layout.frame_rect;
}

static void run_login_screen(void) {
    login_state_t s;
    gui_rect_t frame_rect = {0, 0, 0, 0};
    char key;
    int count;

    auth_log("login enter");
    mem_set(&s, 0, sizeof(s));
    s.selected = 0;
    redraw_login(&s, &frame_rect, 1);

    for (;;) {
        usb_poll();
        if (!keyboard_try_getchar(&key)) continue;
        auth_log_key("login", key);
        count = tile_count();

        if (key == KEY_UP && s.selected > 0) {
            s.selected--;
            s.pin[0] = '\0';
            s.pin_len = 0;
            s.has_error = 0;
            redraw_login(&s, &frame_rect, 0);
            continue;
        }
        if (key == KEY_DOWN && s.selected + 1 < count) {
            s.selected++;
            s.pin[0] = '\0';
            s.pin_len = 0;
            s.has_error = 0;
            redraw_login(&s, &frame_rect, 0);
            continue;
        }

        if (key == '\r' || key == '\n') {
            if (s.selected == 0) {
                auth_session_begin("Guest");
                return;
            }
            if (s.selected == 1) {
                if (s.pin_len == 0) {
                    str_copy(s.error, "Enter PIN.", sizeof(s.error));
                    s.has_error = 1;
                    redraw_login(&s, &frame_rect, 0);
                    continue;
                }
                if (auth_verify_devacc(s.pin)) {
                    auth_session_begin(AUTH_DEVACC_NAME);
                    return;
                }
                str_copy(s.error, "Wrong PIN.", sizeof(s.error));
                s.has_error = 1;
                s.pin[0] = '\0';
                s.pin_len = 0;
                redraw_login(&s, &frame_rect, 0);
                continue;
            }

            {
                const char *name = tile_label(s.selected);
                if (name && users_switch(name)) {
                    auth_session_begin(name);
                    return;
                }
                str_copy(s.error, "Cannot open account.", sizeof(s.error));
                s.has_error = 1;
                redraw_login(&s, &frame_rect, 0);
            }
            continue;
        }

        if (s.selected == 1) {
            if ((key == '\b' || key == (char)0x7F) && s.pin_len > 0) {
                s.pin[--s.pin_len] = '\0';
                s.has_error = 0;
                redraw_login(&s, &frame_rect, 0);
            } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                       s.pin_len < PIN_MAX) {
                s.pin[s.pin_len++] = key;
                s.pin[s.pin_len] = '\0';
                s.has_error = 0;
                redraw_login(&s, &frame_rect, 0);
            }
        }
    }
}

static void run_setup_screen(void) {
    setup_state_t s;
    gui_rect_t frame_rect = {0, 0, 0, 0};
    char key;

    auth_log("setup enter");
    mem_set(&s, 0, sizeof(s));
    redraw_setup(&s, &frame_rect, 1);

    for (;;) {
        usb_poll();
        if (!keyboard_try_getchar(&key)) continue;
        auth_log_key("setup", key);

        if (key == '\b' || key == (char)0x7F) {
            if (s.name_len > 0) {
                s.name[--s.name_len] = '\0';
                s.msg[0] = '\0';
                s.has_error = 0;
                redraw_setup(&s, &frame_rect, 0);
            }
            continue;
        }

        if (key == '\r' || key == '\n') {
            if (s.name_len == 0) {
                str_copy(s.msg, "Enter a name first.", sizeof(s.msg));
                s.has_error = 1;
                redraw_setup(&s, &frame_rect, 0);
                continue;
            }
            if (users_create(s.name, 1) && users_switch(s.name)) {
                return;
            }
            str_copy(s.msg, "Name invalid or already exists.", sizeof(s.msg));
            s.has_error = 1;
            s.name[0] = '\0';
            s.name_len = 0;
            redraw_setup(&s, &frame_rect, 0);
            continue;
        }

        if ((unsigned char)key >= 32 && (unsigned char)key <= 126 && s.name_len < NAME_MAX) {
            char ch = key;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
                s.name[s.name_len++] = ch;
                s.name[s.name_len] = '\0';
                s.msg[0] = '\0';
                s.has_error = 0;
                redraw_setup(&s, &frame_rect, 0);
            }
        }
    }
}

void auth_gui_run(boot_target_t requested_target) {
    auth_log("auth gui run");
    if (gfx_get_mode() != GFX_MODE_GRAPHICS) return;
    if (requested_target != BOOT_TARGET_NORMAL_GUI) return;
    if (users_needs_setup()) run_setup_screen();
    run_login_screen();
}
