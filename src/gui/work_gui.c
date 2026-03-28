#include "gui/work_gui.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "tests/test_runner.h"

#define WORK_HDR_H            (th_metrics()->toolbar_h + 4)
#define WORK_SUBBAR_H         (th_metrics()->toolbar_h - 8)
#define WORK_STATUS_H         (th_metrics()->status_h)
#define WORK_PAD              (th_metrics()->card_pad)
#define WORK_BTN_H            (th_metrics()->button_h)
#define WORK_CARD_W           182
#define WORK_CARD_H           138
#define WORK_DOC_MAX_BLOCKS   96
#define WORK_DOC_TEXT_MAX     192
#define WORK_SHEET_ROWS       50
#define WORK_SHEET_COLS       26
#define WORK_SHEET_TEXT_MAX   48
#define WORK_SLIDE_MAX        12
#define WORK_SLIDE_ELEM_MAX   20
#define WORK_SLIDE_TEXT_MAX   64
#define SCALE_FP_ONE          256

#define COL_WORK_BG           gfx_rgb(244, 242, 236)
#define COL_WORK_HDR          gfx_rgb(44, 64, 97)
#define COL_WORK_HDR_TXT      gfx_rgb(248, 250, 252)
#define COL_WORK_SUBBAR       gfx_rgb(230, 226, 217)
#define COL_WORK_STATUS       gfx_rgb(228, 223, 212)
#define COL_WORK_STATUS_TXT   gfx_rgb(70, 61, 48)
#define COL_WORK_TEXT         gfx_rgb(42, 45, 53)
#define COL_WORK_TEXT_DIM     gfx_rgb(116, 111, 102)
#define COL_WORK_BORDER       gfx_rgb(176, 169, 153)
#define COL_WORK_PANEL        gfx_rgb(255, 252, 246)
#define COL_WORK_FIELD        gfx_rgb(255, 255, 255)
#define COL_WORK_FIELD_FOCUS  gfx_rgb(255, 247, 222)
#define COL_WORK_BTN          gfx_rgb(252, 249, 241)
#define COL_WORK_BTN_TXT      gfx_rgb(56, 54, 49)
#define COL_WORK_ACCENT       gfx_rgb(228, 118, 46)
#define COL_WORK_CARD_DOCS    gfx_rgb(228, 118, 46)
#define COL_WORK_CARD_SHEET   gfx_rgb(41, 156, 93)
#define COL_WORK_CARD_SLIDE   gfx_rgb(37, 99, 198)
#define COL_WORK_PAGE         gfx_rgb(255, 255, 252)
#define COL_WORK_PAGE_SHADOW  gfx_rgb(206, 197, 182)
#define COL_WORK_CURSOR       gfx_rgb(28, 97, 207)
#define COL_WORK_SHEET_HEAD   gfx_rgb(235, 238, 242)
#define COL_WORK_SHEET_SEL    gfx_rgb(209, 227, 255)
#define COL_WORK_SLIDE_AREA   gfx_rgb(228, 233, 242)
#define COL_WORK_SLIDE_FRAME  gfx_rgb(110, 127, 154)
#define COL_WORK_THUMB_SEL    gfx_rgb(255, 233, 209)

typedef enum {
    DOC_BLOCK_TITLE = 0,
    DOC_BLOCK_HEADING,
    DOC_BLOCK_BODY,
    DOC_BLOCK_BULLET,
} work_doc_block_type_t;

typedef enum {
    DOC_ALIGN_LEFT = 0,
    DOC_ALIGN_CENTER,
    DOC_ALIGN_RIGHT,
} work_doc_align_t;

typedef struct {
    char text[WORK_DOC_TEXT_MAX];
    uint16_t len;
    uint8_t type;
    uint8_t align;
    uint8_t size_px;
} work_doc_block_t;

typedef struct {
    char raw[WORK_SHEET_TEXT_MAX];
    char display[WORK_SHEET_TEXT_MAX];
    int value;
    uint8_t is_number;
    uint8_t eval_state;
    uint8_t error_code;
} work_sheet_cell_t;

typedef enum {
    SLIDE_ELEM_TEXT = 0,
    SLIDE_ELEM_RECT,
} work_slide_elem_type_t;

typedef struct {
    uint8_t type;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
    char text[WORK_SLIDE_TEXT_MAX];
    uint16_t len;
} work_slide_elem_t;

typedef struct {
    uint32_t bg;
    work_slide_elem_t elems[WORK_SLIDE_ELEM_MAX];
    int elem_count;
} work_slide_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int scale_fp;
} work_slide_view_t;

static int g_win_id = -1;
static work_mode_t g_mode = WORK_MODE_HOME;
static char g_message[128];
static int g_open_overlay = 0;
static char g_open_buf[128];
static int g_open_len = 0;

static work_doc_block_t g_doc_blocks[WORK_DOC_MAX_BLOCKS];
static int g_doc_block_count = 0;
static int g_doc_block_sel = 0;
static int g_doc_cursor = 0;
static int g_doc_scroll = 0;
static int g_doc_dirty = 0;
static int g_doc_close_armed = 0;
static char g_doc_path[128];
static int g_doc_layout_top[WORK_DOC_MAX_BLOCKS];
static int g_doc_layout_h[WORK_DOC_MAX_BLOCKS];
static int g_doc_layout_total = 0;

static work_sheet_cell_t g_sheet_cells[WORK_SHEET_ROWS][WORK_SHEET_COLS];
static int g_sheet_row = 0;
static int g_sheet_col = 0;
static int g_sheet_scroll_row = 0;
static int g_sheet_scroll_col = 0;
static int g_sheet_sel_r0 = 0;
static int g_sheet_sel_c0 = 0;
static int g_sheet_sel_r1 = 0;
static int g_sheet_sel_c1 = 0;
static int g_sheet_dragging = 0;
static int g_sheet_editing = 0;
static char g_sheet_edit_buf[WORK_SHEET_TEXT_MAX];
static int g_sheet_edit_len = 0;
static int g_sheet_edit_cursor = 0;

static work_slide_t g_slides[WORK_SLIDE_MAX];
static int g_slide_count = 0;
static int g_slide_index = 0;
static int g_slide_sel = -1;
static int g_slide_drag_sel = -1;
static int g_slide_drag_ox = 0;
static int g_slide_drag_oy = 0;
static int g_slide_present = 0;
static int g_slide_text_edit = 0;
static char g_slide_text_buf[WORK_SLIDE_TEXT_MAX];
static int g_slide_text_len = 0;
static int g_slide_text_cursor = 0;

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static void clip_text(char *out, size_t out_size, const char *text, int max_chars) {
    int i = 0;
    if (out_size == 0) return;
    if (max_chars < 0) max_chars = 0;
    while (text && text[i] && i < max_chars && i + 1 < (int)out_size) {
        out[i] = text[i];
        i++;
    }
    out[i] = '\0';
}

static void i32_to_dec(int value, char *out, size_t out_size) {
    char tmp[16];
    size_t p = 0;
    size_t o = 0;
    uint32_t mag;
    int neg = 0;

    if (out_size == 0) return;
    if (value < 0) {
        neg = 1;
        mag = (uint32_t)(-value);
    } else {
        mag = (uint32_t)value;
    }
    if (mag == 0) {
        out[0] = '0';
        if (out_size > 1) out[1] = '\0';
        return;
    }
    while (mag > 0 && p < sizeof(tmp)) {
        tmp[p++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    }
    if (neg && o + 1 < out_size) out[o++] = '-';
    while (p > 0 && o + 1 < out_size) out[o++] = tmp[--p];
    out[o] = '\0';
}

static int path_is_workspace_target(const char *path) {
    if (!path || !path[0]) return 0;
    if (str_eq(path, "/ROOT")) return 1;
    return str_ncmp(path, "/ROOT", 5) == 0 && path[5] == '/';
}

static void basename_from_path(const char *path, char *out, size_t out_size) {
    int len;
    int start;

    if (!path || !path[0]) {
        str_copy(out, "180 Work", out_size);
        return;
    }
    len = (int)str_len(path);
    start = len;
    while (start > 0 && path[start - 1] != '/') start--;
    str_copy(out, path + start, out_size);
}

static void work_set_message(const char *msg) {
    str_copy(g_message, msg ? msg : "", sizeof(g_message));
}

static const char *mode_label(work_mode_t mode) {
    if (mode == WORK_MODE_DOCS) return "Docs";
    if (mode == WORK_MODE_SHEETS) return "Sheets";
    if (mode == WORK_MODE_SLIDES) return "Slides";
    return "Home";
}

static void work_update_title(void) {
    char title[GUI_TITLE_MAX];
    char name[32];

    if (g_win_id < 0 || !gui_window_active(g_win_id)) return;
    if (g_mode == WORK_MODE_DOCS && g_doc_path[0]) {
        basename_from_path(g_doc_path, name, sizeof(name));
        str_copy(title, "180 Work - ", sizeof(title));
        str_cat(title, name, sizeof(title));
    } else {
        str_copy(title, "180 Work - ", sizeof(title));
        str_cat(title, mode_label(g_mode), sizeof(title));
    }
    gui_window_set_title(g_win_id, title);
}

static void draw_button(int x, int y, int w, int h, const char *label, int active) {
    uint32_t bg = active ? COL_WORK_ACCENT : COL_WORK_BTN;
    uint32_t fg = active ? gfx_rgb(255, 255, 255) : COL_WORK_BTN_TXT;
    int tx = x + (w - (int)str_len(label) * FONT_WIDTH) / 2;
    int ty = y + (h - FONT_HEIGHT) / 2;
    gfx_fill_rect(x, y, w, h, COL_WORK_BORDER);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2, bg);
    gfx_draw_string(tx, ty, label, fg, bg);
}

static void draw_field(int x, int y, int w, int h, const char *text, int focused) {
    char clip[96];
    int max_chars = (w - 8) / FONT_WIDTH;
    clip_text(clip, sizeof(clip), text, max_chars);
    gfx_fill_rect(x, y, w, h, COL_WORK_BORDER);
    gfx_fill_rect(x + 1, y + 1, w - 2, h - 2,
                  focused ? COL_WORK_FIELD_FOCUS : COL_WORK_FIELD);
    gfx_draw_string(x + 4, y + (h - FONT_HEIGHT) / 2, clip,
                    COL_WORK_TEXT, focused ? COL_WORK_FIELD_FOCUS : COL_WORK_FIELD);
}

static void linebuf_load(char *dst, int dst_cap, int *len, int *cursor, const char *src) {
    str_copy(dst, src ? src : "", (size_t)dst_cap);
    *len = (int)str_len(dst);
    *cursor = *len;
}

static void linebuf_insert(char *dst, int dst_cap, int *len, int *cursor, char c) {
    if (*len >= dst_cap - 1) return;
    if (*cursor < 0) *cursor = 0;
    if (*cursor > *len) *cursor = *len;
    for (int i = *len; i >= *cursor; i--) dst[i + 1] = dst[i];
    dst[*cursor] = c;
    (*len)++;
    (*cursor)++;
}

static void linebuf_backspace(char *dst, int *len, int *cursor) {
    if (*cursor <= 0 || *len <= 0) return;
    for (int i = *cursor - 1; i < *len; i++) dst[i] = dst[i + 1];
    (*cursor)--;
    (*len)--;
}

static void linebuf_delete(char *dst, int *len, int *cursor) {
    if (*cursor < 0 || *cursor >= *len) return;
    for (int i = *cursor; i < *len; i++) dst[i] = dst[i + 1];
    (*len)--;
}

static int scaled_char_w(int font_px) {
    int w = (font_px * FONT_WIDTH) / FONT_HEIGHT;
    if (w < 4) w = 4;
    return w;
}

static void draw_char_scaled(int x, int y, char c, int font_px, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph;
    int char_w = scaled_char_w(font_px);
    unsigned char uc = (unsigned char)c;

    if (uc >= 128) uc = '?';
    glyph = &g_font_8x16[uc * FONT_HEIGHT];
    for (int dy = 0; dy < font_px; dy++) {
        int sy = (dy * FONT_HEIGHT) / font_px;
        uint8_t bits = glyph[sy];
        for (int dx = 0; dx < char_w; dx++) {
            int sx = (dx * FONT_WIDTH) / char_w;
            uint32_t col = (bits & (uint8_t)(0x80u >> sx)) ? fg : bg;
            gfx_put_pixel(x + dx, y + dy, col);
        }
    }
}

static void draw_text_scaled(int x, int y, const char *text, int font_px, uint32_t fg, uint32_t bg) {
    int cw = scaled_char_w(font_px);
    for (int i = 0; text && text[i]; i++) {
        draw_char_scaled(x + i * cw, y, text[i], font_px, fg, bg);
    }
}

static int scale_pos(int value, int scale_fp) {
    return (value * scale_fp) / SCALE_FP_ONE;
}

static int scale_dim(int value, int scale_fp) {
    int scaled = scale_pos(value, scale_fp);
    if (scaled < 1) scaled = 1;
    return scaled;
}

static int unscale_pos(int value, int scale_fp) {
    if (scale_fp <= 0) return value;
    return (value * SCALE_FP_ONE) / scale_fp;
}

static void work_mode_rect(int idx, int *x, int *y, int *w, int *h) {
    static const char *labels[] = { "Home", "Docs", "Sheets", "Slides" };
    int off = 112;
    for (int i = 0; i < idx; i++) off += (int)str_len(labels[i]) * FONT_WIDTH + 22;
    *x = off;
    *y = 10;
    *w = (int)str_len(labels[idx]) * FONT_WIDTH + 14;
    *h = WORK_BTN_H;
}

static void work_docs_button_rect(int idx, int *x, int *y, int *w, int *h) {
    static const int bx[] = { 12, 72, 132, 202, 272, 348 };
    static const int bw[] = { 52, 52, 52, 60, 68, 52 };
    *x = bx[idx];
    *y = WORK_HDR_H + 4;
    *w = bw[idx];
    *h = WORK_BTN_H;
}

static int doc_default_size(int type) {
    if (type == DOC_BLOCK_TITLE) return 32;
    if (type == DOC_BLOCK_HEADING) return 24;
    return 16;
}

static const char *doc_type_name(int type) {
    if (type == DOC_BLOCK_TITLE) return "Title";
    if (type == DOC_BLOCK_HEADING) return "Heading";
    if (type == DOC_BLOCK_BULLET) return "Bullet";
    return "Body";
}

static const char *doc_align_name(int align) {
    if (align == DOC_ALIGN_CENTER) return "Center";
    if (align == DOC_ALIGN_RIGHT) return "Right";
    return "Left";
}

static void doc_set_block_defaults(work_doc_block_t *b, int type) {
    if (!b) return;
    b->text[0] = '\0';
    b->len = 0;
    b->type = (uint8_t)type;
    b->align = DOC_ALIGN_LEFT;
    b->size_px = (uint8_t)doc_default_size(type);
}

static void docs_mark_dirty(void) {
    g_doc_dirty = 1;
    g_doc_close_armed = 0;
}

static void docs_reset_new(void) {
    mem_set(g_doc_blocks, 0, sizeof(g_doc_blocks));
    g_doc_block_count = 2;
    doc_set_block_defaults(&g_doc_blocks[0], DOC_BLOCK_TITLE);
    doc_set_block_defaults(&g_doc_blocks[1], DOC_BLOCK_BODY);
    g_doc_block_sel = 0;
    g_doc_cursor = 0;
    g_doc_scroll = 0;
    g_doc_dirty = 0;
    g_doc_close_armed = 0;
    str_copy(g_doc_path, "/ROOT/UNTITLED.TXT", sizeof(g_doc_path));
    work_set_message("New document");
}

static void sheets_reset(void) {
    mem_set(g_sheet_cells, 0, sizeof(g_sheet_cells));
    g_sheet_row = 0;
    g_sheet_col = 0;
    g_sheet_scroll_row = 0;
    g_sheet_scroll_col = 0;
    g_sheet_sel_r0 = g_sheet_sel_r1 = 0;
    g_sheet_sel_c0 = g_sheet_sel_c1 = 0;
    g_sheet_dragging = 0;
    g_sheet_editing = 0;
    g_sheet_edit_buf[0] = '\0';
    g_sheet_edit_len = 0;
    g_sheet_edit_cursor = 0;
}

static uint32_t slide_bg_for_index(int idx) {
    switch (idx % 5) {
        case 0: return gfx_rgb(255, 251, 244);
        case 1: return gfx_rgb(245, 251, 255);
        case 2: return gfx_rgb(246, 255, 249);
        case 3: return gfx_rgb(255, 245, 247);
        default: return gfx_rgb(251, 248, 255);
    }
}

static void slides_reset(void) {
    mem_set(g_slides, 0, sizeof(g_slides));
    g_slide_count = 1;
    g_slide_index = 0;
    g_slide_sel = -1;
    g_slide_drag_sel = -1;
    g_slide_present = 0;
    g_slide_text_edit = 0;
    g_slide_text_buf[0] = '\0';
    g_slide_text_len = 0;
    g_slide_text_cursor = 0;
    g_slides[0].bg = slide_bg_for_index(0);
}

static void work_reset_all(void) {
    docs_reset_new();
    sheets_reset();
    slides_reset();
    g_mode = WORK_MODE_HOME;
    g_open_overlay = 0;
    g_open_buf[0] = '\0';
    g_open_len = 0;
}

static int docs_parse_meta(work_doc_block_t *b, const char *line) {
    char type[16];
    char align[16];
    char size[16];
    char text[WORK_DOC_TEXT_MAX];
    int p = 5;
    int field = 0;
    char *outs[4];
    int caps[4];
    int lens[4] = { 0, 0, 0, 0 };

    outs[0] = type;  caps[0] = (int)sizeof(type);
    outs[1] = align; caps[1] = (int)sizeof(align);
    outs[2] = size;  caps[2] = (int)sizeof(size);
    outs[3] = text;  caps[3] = (int)sizeof(text);
    if (str_ncmp(line, "@180|", 5) != 0) return 0;
    while (line[p] && field < 4) {
        if (line[p] == '|') {
            if (lens[field] < caps[field]) outs[field][lens[field]] = '\0';
            field++;
            p++;
            continue;
        }
        if (lens[field] + 1 < caps[field]) outs[field][lens[field]++] = line[p];
        p++;
    }
    if (field < 3) return 0;
    outs[3][lens[3]] = '\0';
    if (ascii_upper(type[0]) == 'T') b->type = DOC_BLOCK_TITLE;
    else if (ascii_upper(type[0]) == 'H') b->type = DOC_BLOCK_HEADING;
    else if (ascii_upper(type[0]) == 'B' || ascii_upper(type[0]) == 'U') b->type = DOC_BLOCK_BULLET;
    else b->type = DOC_BLOCK_BODY;
    if (ascii_upper(align[0]) == 'C') b->align = DOC_ALIGN_CENTER;
    else if (ascii_upper(align[0]) == 'R') b->align = DOC_ALIGN_RIGHT;
    else b->align = DOC_ALIGN_LEFT;
    b->size_px = (uint8_t)clampi(size[0] ? (size[0] - '0') * 10 + (size[1] ? size[1] - '0' : 0) : 16, 12, 32);
    str_copy(b->text, text, sizeof(b->text));
    b->len = (uint16_t)str_len(b->text);
    return 1;
}

static void docs_load_file(const char *path) {
    static uint8_t load_buf[WORK_DOC_MAX_BLOCKS * WORK_DOC_TEXT_MAX];
    int read;
    int start;

    docs_reset_new();
    if (!path || !path[0]) return;
    str_copy(g_doc_path, path, sizeof(g_doc_path));
    if (!vfs_available()) {
        work_set_message("filesystem unavailable");
        return;
    }
    read = vfs_cat(path, load_buf, (int)sizeof(load_buf) - 1);
    if (read < 0) {
        work_set_message("New document");
        g_doc_dirty = 0;
        return;
    }
    load_buf[read] = '\0';
    g_doc_block_count = 0;
    start = 0;
    for (int i = 0; i <= read; i++) {
        int len;
        work_doc_block_t *b;
        char line[WORK_DOC_TEXT_MAX];

        if (i < read && load_buf[i] != '\n') continue;
        if (g_doc_block_count >= WORK_DOC_MAX_BLOCKS) break;
        len = i - start;
        while (len > 0 && load_buf[start + len - 1] == '\r') len--;
        if (len >= WORK_DOC_TEXT_MAX) len = WORK_DOC_TEXT_MAX - 1;
        for (int j = 0; j < len; j++) line[j] = (char)load_buf[start + j];
        line[len] = '\0';
        b = &g_doc_blocks[g_doc_block_count++];
        doc_set_block_defaults(b, DOC_BLOCK_BODY);
        if (!docs_parse_meta(b, line)) {
            if (str_ncmp(line, "## ", 3) == 0) {
                b->type = DOC_BLOCK_HEADING;
                b->size_px = 24;
                str_copy(b->text, line + 3, sizeof(b->text));
            } else if (str_ncmp(line, "# ", 2) == 0) {
                b->type = DOC_BLOCK_TITLE;
                b->size_px = 32;
                str_copy(b->text, line + 2, sizeof(b->text));
            } else if (str_ncmp(line, "- ", 2) == 0) {
                b->type = DOC_BLOCK_BULLET;
                b->size_px = 16;
                str_copy(b->text, line + 2, sizeof(b->text));
            } else {
                str_copy(b->text, line, sizeof(b->text));
            }
            b->len = (uint16_t)str_len(b->text);
        }
        start = i + 1;
    }
    if (g_doc_block_count < 1) {
        g_doc_block_count = 1;
        doc_set_block_defaults(&g_doc_blocks[0], DOC_BLOCK_BODY);
    }
    g_doc_block_sel = 0;
    g_doc_cursor = 0;
    g_doc_scroll = 0;
    g_doc_dirty = 0;
    g_doc_close_armed = 0;
    work_set_message("Loaded");
}

static void docs_serialize_block(const work_doc_block_t *b, char *out, size_t out_size) {
    int def_size = doc_default_size(b->type);
    int custom = b->align != DOC_ALIGN_LEFT || b->size_px != def_size;
    out[0] = '\0';
    if (custom) {
        char size_buf[8];
        str_copy(out, "@180|", out_size);
        str_cat(out, doc_type_name(b->type), out_size);
        str_cat(out, "|", out_size);
        str_cat(out, doc_align_name(b->align), out_size);
        str_cat(out, "|", out_size);
        i32_to_dec((int)b->size_px, size_buf, sizeof(size_buf));
        str_cat(out, size_buf, out_size);
        str_cat(out, "|", out_size);
        str_cat(out, b->text, out_size);
        return;
    }
    if (b->type == DOC_BLOCK_TITLE) {
        str_copy(out, "# ", out_size);
        str_cat(out, b->text, out_size);
    } else if (b->type == DOC_BLOCK_HEADING) {
        str_copy(out, "## ", out_size);
        str_cat(out, b->text, out_size);
    } else if (b->type == DOC_BLOCK_BULLET) {
        str_copy(out, "- ", out_size);
        str_cat(out, b->text, out_size);
    } else {
        str_copy(out, b->text, out_size);
    }
}

static int docs_save_file(void) {
    static char save_buf[WORK_DOC_MAX_BLOCKS * (WORK_DOC_TEXT_MAX + 24)];
    int out = 0;

    if (!g_doc_path[0]) str_copy(g_doc_path, "/ROOT/UNTITLED.TXT", sizeof(g_doc_path));
    for (int i = 0; i < g_doc_block_count; i++) {
        char line[WORK_DOC_TEXT_MAX + 32];
        int len;
        docs_serialize_block(&g_doc_blocks[i], line, sizeof(line));
        len = (int)str_len(line);
        if (out + len + 2 >= (int)sizeof(save_buf)) {
            work_set_message("document too large");
            return 0;
        }
        for (int j = 0; j < len; j++) save_buf[out++] = line[j];
        if (i + 1 < g_doc_block_count) save_buf[out++] = '\n';
    }
    if (!path_is_workspace_target(g_doc_path)) {
        work_set_message("protected system location");
        return 0;
    }
    if (vfs_write(g_doc_path, (const uint8_t *)save_buf, (uint32_t)out) < 0) {
        work_set_message("save failed");
        return 0;
    }
    g_doc_dirty = 0;
    g_doc_close_armed = 0;
    work_set_message("Saved");
    return 1;
}

static int doc_block_gap(const work_doc_block_t *b) {
    if (b->type == DOC_BLOCK_TITLE) return 14;
    if (b->type == DOC_BLOCK_HEADING) return 10;
    return 6;
}

static int doc_block_prefix_chars(const work_doc_block_t *b) {
    return b->type == DOC_BLOCK_BULLET ? 2 : 0;
}

static int docs_layout(gui_rect_t r, int *page_x, int *page_y, int *page_w, int *page_h) {
    int body_y = r.y + WORK_HDR_H + WORK_SUBBAR_H;
    int body_h = r.h - WORK_HDR_H - WORK_SUBBAR_H - WORK_STATUS_H;
    int w = r.w - 120;
    if (w > 600) w = 600;
    if (w < 220) w = r.w - 24;
    *page_x = r.x + (r.w - w) / 2;
    *page_y = body_y + 16;
    *page_w = w;
    *page_h = body_h - 28;
    if (*page_h < 80) *page_h = 80;

    g_doc_layout_total = 0;
    for (int i = 0; i < g_doc_block_count; i++) {
        work_doc_block_t *b = &g_doc_blocks[i];
        int font_px = clampi((int)b->size_px, 12, 32);
        int char_w = scaled_char_w(font_px);
        int cols = (*page_w - 48) / char_w - doc_block_prefix_chars(b);
        int lines;
        if (cols < 1) cols = 1;
        lines = ((int)b->len + cols - 1) / cols;
        if (lines < 1) lines = 1;
        g_doc_layout_top[i] = g_doc_layout_total;
        g_doc_layout_h[i] = lines * (font_px + 2) + doc_block_gap(b);
        g_doc_layout_total += g_doc_layout_h[i];
    }
    if (g_doc_layout_total < *page_h - 32) g_doc_layout_total = *page_h - 32;
    if (g_doc_scroll > g_doc_layout_total - (*page_h - 32)) {
        g_doc_scroll = g_doc_layout_total - (*page_h - 32);
    }
    if (g_doc_scroll < 0) g_doc_scroll = 0;
    return body_y;
}

static void docs_ensure_visible(gui_rect_t r) {
    int page_x;
    int page_y;
    int page_w;
    int page_h;
    int top;
    int bottom;

    (void)docs_layout(r, &page_x, &page_y, &page_w, &page_h);
    if (g_doc_block_sel < 0) g_doc_block_sel = 0;
    if (g_doc_block_sel >= g_doc_block_count) g_doc_block_sel = g_doc_block_count - 1;
    top = g_doc_layout_top[g_doc_block_sel];
    bottom = top + g_doc_layout_h[g_doc_block_sel];
    if (top < g_doc_scroll) g_doc_scroll = top;
    if (bottom > g_doc_scroll + page_h - 32) g_doc_scroll = bottom - (page_h - 32);
    if (g_doc_scroll < 0) g_doc_scroll = 0;
}

static void docs_insert_block(int idx, int type) {
    if (g_doc_block_count >= WORK_DOC_MAX_BLOCKS) return;
    if (idx < 0) idx = 0;
    if (idx > g_doc_block_count) idx = g_doc_block_count;
    for (int i = g_doc_block_count; i > idx; i--) g_doc_blocks[i] = g_doc_blocks[i - 1];
    doc_set_block_defaults(&g_doc_blocks[idx], type);
    g_doc_block_count++;
}

static void docs_delete_block(int idx) {
    if (g_doc_block_count <= 1 || idx < 0 || idx >= g_doc_block_count) return;
    for (int i = idx; i + 1 < g_doc_block_count; i++) g_doc_blocks[i] = g_doc_blocks[i + 1];
    g_doc_block_count--;
    if (g_doc_block_sel >= g_doc_block_count) g_doc_block_sel = g_doc_block_count - 1;
}

static void docs_split_block(void) {
    work_doc_block_t *cur;
    work_doc_block_t *nxt;
    int right_len;
    int at_end;

    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    if (g_doc_block_count >= WORK_DOC_MAX_BLOCKS) {
        work_set_message("block limit reached");
        return;
    }
    cur = &g_doc_blocks[g_doc_block_sel];
    if (g_doc_cursor < 0) g_doc_cursor = 0;
    if (g_doc_cursor > (int)cur->len) g_doc_cursor = (int)cur->len;
    at_end = g_doc_cursor == (int)cur->len;
    docs_insert_block(g_doc_block_sel + 1, cur->type);
    nxt = &g_doc_blocks[g_doc_block_sel + 1];
    *nxt = *cur;
    right_len = (int)cur->len - g_doc_cursor;
    if (right_len < 0) right_len = 0;
    for (int i = 0; i < right_len; i++) nxt->text[i] = cur->text[g_doc_cursor + i];
    nxt->text[right_len] = '\0';
    nxt->len = (uint16_t)right_len;
    cur->text[g_doc_cursor] = '\0';
    cur->len = (uint16_t)g_doc_cursor;
    if (at_end && (cur->type == DOC_BLOCK_TITLE || cur->type == DOC_BLOCK_HEADING)) {
        nxt->type = DOC_BLOCK_BODY;
        nxt->size_px = 16;
    }
    g_doc_block_sel++;
    g_doc_cursor = 0;
    docs_mark_dirty();
}

static void docs_merge_previous(void) {
    work_doc_block_t *cur;
    work_doc_block_t *prev;
    int room;

    if (g_doc_block_sel <= 0 || g_doc_block_sel >= g_doc_block_count) return;
    cur = &g_doc_blocks[g_doc_block_sel];
    prev = &g_doc_blocks[g_doc_block_sel - 1];
    room = WORK_DOC_TEXT_MAX - 1 - (int)prev->len;
    if ((int)cur->len > room) {
        work_set_message("block full");
        return;
    }
    for (int i = 0; i < (int)cur->len; i++) prev->text[prev->len + i] = cur->text[i];
    prev->len = (uint16_t)(prev->len + cur->len);
    prev->text[prev->len] = '\0';
    g_doc_cursor = (int)(prev->len - cur->len);
    docs_delete_block(g_doc_block_sel);
    g_doc_block_sel--;
    docs_mark_dirty();
}

static void docs_insert_char(char c) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    if ((int)b->len >= WORK_DOC_TEXT_MAX - 1) {
        work_set_message("block full");
        return;
    }
    if (g_doc_cursor < 0) g_doc_cursor = 0;
    if (g_doc_cursor > (int)b->len) g_doc_cursor = (int)b->len;
    for (int i = (int)b->len; i >= g_doc_cursor; i--) b->text[i + 1] = b->text[i];
    b->text[g_doc_cursor] = c;
    b->len++;
    g_doc_cursor++;
    docs_mark_dirty();
}

static void docs_backspace(void) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    if (g_doc_cursor > 0) {
        for (int i = g_doc_cursor - 1; i < (int)b->len; i++) b->text[i] = b->text[i + 1];
        b->len--;
        g_doc_cursor--;
        docs_mark_dirty();
        return;
    }
    docs_merge_previous();
}

static void docs_delete(void) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    if (g_doc_cursor < (int)b->len) {
        for (int i = g_doc_cursor; i < (int)b->len; i++) b->text[i] = b->text[i + 1];
        b->len--;
        docs_mark_dirty();
        return;
    }
    if (g_doc_block_sel + 1 < g_doc_block_count) {
        g_doc_block_sel++;
        docs_merge_previous();
    }
}

static void docs_move_left(void) {
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    if (g_doc_cursor > 0) g_doc_cursor--;
    else if (g_doc_block_sel > 0) {
        g_doc_block_sel--;
        g_doc_cursor = (int)g_doc_blocks[g_doc_block_sel].len;
    }
}

static void docs_move_right(void) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    if (g_doc_cursor < (int)b->len) g_doc_cursor++;
    else if (g_doc_block_sel + 1 < g_doc_block_count) {
        g_doc_block_sel++;
        g_doc_cursor = 0;
    }
}

static void docs_move_vertical(int dir) {
    int next = g_doc_block_sel + dir;
    if (next < 0 || next >= g_doc_block_count) return;
    g_doc_block_sel = next;
    if (g_doc_cursor > (int)g_doc_blocks[g_doc_block_sel].len) {
        g_doc_cursor = (int)g_doc_blocks[g_doc_block_sel].len;
    }
}

static void docs_cycle_type(void) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    b->type = (uint8_t)((b->type + 1) % 4);
    docs_mark_dirty();
}

static void docs_cycle_align(void) {
    work_doc_block_t *b;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    b->align = (uint8_t)((b->align + 1) % 3);
    docs_mark_dirty();
}

static void docs_cycle_size(void) {
    static const int sizes[] = { 12, 16, 24, 32 };
    work_doc_block_t *b;
    int idx = 0;
    if (g_doc_block_sel < 0 || g_doc_block_sel >= g_doc_block_count) return;
    b = &g_doc_blocks[g_doc_block_sel];
    for (int i = 0; i < 4; i++) {
        if ((int)b->size_px == sizes[i]) {
            idx = i;
            break;
        }
    }
    b->size_px = (uint8_t)sizes[(idx + 1) % 4];
    docs_mark_dirty();
}

static void docs_draw(gui_rect_t r) {
    int page_x;
    int page_y;
    int page_w;
    int page_h;
    char info[64];
    char path_buf[48];

    docs_ensure_visible(r);
    (void)docs_layout(r, &page_x, &page_y, &page_w, &page_h);

    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, r.h - WORK_HDR_H, COL_WORK_BG);
    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, WORK_SUBBAR_H, COL_WORK_SUBBAR);

    {
        static const char *labels[] = { "New", "Open", "Save", "Type", "Align", "Size" };
        int bx, by, bw, bh;
        for (int i = 0; i < 6; i++) {
            work_docs_button_rect(i, &bx, &by, &bw, &bh);
            draw_button(r.x + bx, r.y + by, bw, bh, labels[i], 0);
        }
    }

    gfx_fill_rect(page_x - 6, page_y - 6, page_w + 12, page_h + 12, COL_WORK_PAGE_SHADOW);
    gfx_fill_rect(page_x, page_y, page_w, page_h, COL_WORK_PAGE);

    clip_text(path_buf, sizeof(path_buf), g_doc_path, 30);
    gfx_draw_string(r.x + r.w - 18 - (int)str_len(path_buf) * FONT_WIDTH, r.y + WORK_HDR_H + 7,
                    path_buf, COL_WORK_TEXT_DIM, COL_WORK_SUBBAR);

    if (g_doc_block_sel >= 0 && g_doc_block_sel < g_doc_block_count) {
        work_doc_block_t *sel = &g_doc_blocks[g_doc_block_sel];
        char size_buf[8];
        info[0] = '\0';
        str_copy(info, doc_type_name(sel->type), sizeof(info));
        str_cat(info, " | ", sizeof(info));
        str_cat(info, doc_align_name(sel->align), sizeof(info));
        str_cat(info, " | ", sizeof(info));
        i32_to_dec((int)sel->size_px, size_buf, sizeof(size_buf));
        str_cat(info, size_buf, sizeof(info));
        gfx_draw_string(r.x + 420, r.y + WORK_HDR_H + 7, info, COL_WORK_TEXT_DIM, COL_WORK_SUBBAR);
    }

    for (int i = 0; i < g_doc_block_count; i++) {
        work_doc_block_t *b = &g_doc_blocks[i];
        int top = page_y + 18 + g_doc_layout_top[i] - g_doc_scroll;
        int font_px = clampi((int)b->size_px, 12, 32);
        int line_h = font_px + 2;
        int char_w = scaled_char_w(font_px);
        int prefix_chars = doc_block_prefix_chars(b);
        int cols = (page_w - 48) / char_w - prefix_chars;
        int lines;
        uint32_t fg = COL_WORK_TEXT;
        uint32_t bg = COL_WORK_PAGE;

        if (cols < 1) cols = 1;
        lines = ((int)b->len + cols - 1) / cols;
        if (lines < 1) lines = 1;
        if (top + g_doc_layout_h[i] < page_y || top > page_y + page_h) continue;
        if (i == g_doc_block_sel) {
            gfx_fill_rect(page_x + 12, top - 2, page_w - 24, g_doc_layout_h[i] - 2, gfx_rgb(255, 249, 232));
        }

        for (int line = 0; line < lines; line++) {
            int off = line * cols;
            int remain = (int)b->len - off;
            int chunk = remain > cols ? cols : remain;
            int chunk_chars = prefix_chars + chunk;
            int line_px = chunk_chars * char_w;
            int tx = page_x + 24;
            int ty = top + line * line_h;
            char chunk_buf[WORK_DOC_TEXT_MAX];
            int text_x;

            if (chunk < 0) chunk = 0;
            for (int j = 0; j < chunk; j++) chunk_buf[j] = b->text[off + j];
            chunk_buf[chunk] = '\0';

            if (b->align == DOC_ALIGN_CENTER) tx = page_x + (page_w - line_px) / 2;
            else if (b->align == DOC_ALIGN_RIGHT) tx = page_x + page_w - 24 - line_px;

            text_x = tx;
            if (b->type == DOC_BLOCK_BULLET) {
                draw_text_scaled(tx, ty, "- ", font_px, fg, bg);
                text_x += prefix_chars * char_w;
            }
            draw_text_scaled(text_x, ty, chunk_buf, font_px, fg, bg);

            if (i == g_doc_block_sel) {
                int cur = clampi(g_doc_cursor, 0, (int)b->len);
                int cur_line = cur / cols;
                int cur_col = cur % cols;
                if (cur_line == line) {
                    int cx = tx + (prefix_chars + cur_col) * char_w;
                    gfx_fill_rect(cx, ty, 2, font_px, COL_WORK_CURSOR);
                }
            }
        }
    }
}

static int sheet_rects(gui_rect_t r, int *form_y, int *grid_x, int *grid_y, int *grid_w, int *grid_h) {
    *form_y = r.y + WORK_HDR_H + 4;
    *grid_x = r.x + 16;
    *grid_y = r.y + WORK_HDR_H + WORK_SUBBAR_H + 10;
    *grid_w = r.w - 32;
    *grid_h = r.h - WORK_HDR_H - WORK_SUBBAR_H - WORK_STATUS_H - 20;
    return 1;
}

static int sheet_col_label(int col, char *out, size_t out_size) {
    if (col < 0 || col >= WORK_SHEET_COLS) {
        str_copy(out, "?", out_size);
        return 0;
    }
    out[0] = (char)('A' + col);
    out[1] = '\0';
    return 1;
}

typedef struct {
    const char *s;
    int ok;
} sheet_parse_t;

static void sheet_skip_ws(sheet_parse_t *p) {
    while (*p->s == ' ' || *p->s == '\t') p->s++;
}

static int sheet_parse_int(const char *s, int *out) {
    int neg = 0;
    int v = 0;
    int used = 0;
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (s[i] == '-') {
        neg = 1;
        i++;
    }
    if (s[i] < '0' || s[i] > '9') return 0;
    while (s[i] >= '0' && s[i] <= '9') {
        used = 1;
        v = v * 10 + (s[i] - '0');
        i++;
    }
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (s[i] != '\0') return 0;
    *out = neg ? -v : v;
    return used;
}

static void sheet_cell_ref_name(int row, int col, char *out, size_t out_size) {
    char num[8];
    out[0] = (char)('A' + col);
    out[1] = '\0';
    i32_to_dec(row + 1, num, sizeof(num));
    str_cat(out, num, out_size);
}

static int sheet_eval_cell(int row, int col, int *out_value, int *out_err);
static int sheet_parse_expr(sheet_parse_t *p, int *out);

static int sheet_parse_identifier(sheet_parse_t *p, char *out, size_t out_size) {
    int i = 0;
    sheet_skip_ws(p);
    if (!(ascii_upper(*p->s) >= 'A' && ascii_upper(*p->s) <= 'Z')) return 0;
    while ((ascii_upper(*p->s) >= 'A' && ascii_upper(*p->s) <= 'Z') &&
           i + 1 < (int)out_size) {
        out[i++] = ascii_upper(*p->s);
        p->s++;
    }
    out[i] = '\0';
    return i > 0;
}

static int sheet_parse_cellref(sheet_parse_t *p, int *out_row, int *out_col) {
    const char *s0 = p->s;
    int row = 0;
    int have_digit = 0;
    sheet_skip_ws(p);
    if (!(ascii_upper(*p->s) >= 'A' && ascii_upper(*p->s) <= 'Z')) return 0;
    *out_col = ascii_upper(*p->s) - 'A';
    p->s++;
    while (*p->s >= '0' && *p->s <= '9') {
        row = row * 10 + (*p->s - '0');
        p->s++;
        have_digit = 1;
    }
    if (!have_digit || *out_col < 0 || *out_col >= WORK_SHEET_COLS ||
        row < 1 || row > WORK_SHEET_ROWS) {
        p->s = s0;
        return 0;
    }
    *out_row = row - 1;
    return 1;
}

static int sheet_parse_factor(sheet_parse_t *p, int *out) {
    int row;
    int col;
    int err = 0;
    int val = 0;
    char name[8];

    sheet_skip_ws(p);
    if (*p->s == '(') {
        p->s++;
        if (!sheet_parse_expr(p, out)) return 0;
        sheet_skip_ws(p);
        if (*p->s == ')') p->s++;
        else p->ok = 0;
        return p->ok;
    }
    if (*p->s == '-') {
        p->s++;
        if (!sheet_parse_factor(p, out)) return 0;
        *out = -*out;
        return 1;
    }
    if (sheet_parse_cellref(p, &row, &col)) {
        if (!sheet_eval_cell(row, col, &val, &err)) {
            p->ok = 0;
            return 0;
        }
        *out = val;
        return 1;
    }
    if (sheet_parse_identifier(p, name, sizeof(name))) {
        int args[8];
        int argc = 0;
        int cond_l = 0;
        int cond_r = 0;
        char op = 0;

        sheet_skip_ws(p);
        if (*p->s != '(') {
            p->ok = 0;
            return 0;
        }
        p->s++;
        if (str_eq(name, "IF")) {
            if (!sheet_parse_expr(p, &cond_l)) return 0;
            sheet_skip_ws(p);
            if (*p->s == '=' || *p->s == '<' || *p->s == '>') {
                op = *p->s++;
                if (!sheet_parse_expr(p, &cond_r)) return 0;
            }
            sheet_skip_ws(p);
            if (*p->s != ',') {
                p->ok = 0;
                return 0;
            }
            p->s++;
            if (!sheet_parse_expr(p, &args[0])) return 0;
            sheet_skip_ws(p);
            if (*p->s != ',') {
                p->ok = 0;
                return 0;
            }
            p->s++;
            if (!sheet_parse_expr(p, &args[1])) return 0;
            sheet_skip_ws(p);
            if (*p->s == ')') p->s++;
            else p->ok = 0;
            if (!p->ok) return 0;
            if (op == '=') *out = (cond_l == cond_r) ? args[0] : args[1];
            else if (op == '<') *out = (cond_l < cond_r) ? args[0] : args[1];
            else if (op == '>') *out = (cond_l > cond_r) ? args[0] : args[1];
            else *out = cond_l ? args[0] : args[1];
            return 1;
        }

        while (*p->s && *p->s != ')' && argc < 8) {
            if (!sheet_parse_expr(p, &args[argc++])) return 0;
            sheet_skip_ws(p);
            if (*p->s == ',') p->s++;
            else break;
        }
        sheet_skip_ws(p);
        if (*p->s == ')') p->s++;
        else p->ok = 0;
        if (!p->ok || argc < 1) return 0;

        if (str_eq(name, "SUM")) {
            int sum = 0;
            for (int i = 0; i < argc; i++) sum += args[i];
            *out = sum;
            return 1;
        }
        if (str_eq(name, "AVG")) {
            int sum = 0;
            for (int i = 0; i < argc; i++) sum += args[i];
            *out = sum / argc;
            return 1;
        }
        if (str_eq(name, "MIN")) {
            int minv = args[0];
            for (int i = 1; i < argc; i++) if (args[i] < minv) minv = args[i];
            *out = minv;
            return 1;
        }
        if (str_eq(name, "MAX")) {
            int maxv = args[0];
            for (int i = 1; i < argc; i++) if (args[i] > maxv) maxv = args[i];
            *out = maxv;
            return 1;
        }
        p->ok = 0;
        return 0;
    }
    {
        int neg = 0;
        int value = 0;
        int used = 0;
        sheet_skip_ws(p);
        if (*p->s == '-') {
            neg = 1;
            p->s++;
        }
        while (*p->s >= '0' && *p->s <= '9') {
            value = value * 10 + (*p->s - '0');
            p->s++;
            used = 1;
        }
        if (!used) {
            p->ok = 0;
            return 0;
        }
        *out = neg ? -value : value;
        return 1;
    }
}

static int sheet_parse_term(sheet_parse_t *p, int *out) {
    int rhs;
    if (!sheet_parse_factor(p, out)) return 0;
    for (;;) {
        char op;
        sheet_skip_ws(p);
        op = *p->s;
        if (op != '*' && op != '/') break;
        p->s++;
        if (!sheet_parse_factor(p, &rhs)) return 0;
        if (op == '*') *out *= rhs;
        else {
            if (rhs == 0) {
                p->ok = 0;
                return 0;
            }
            *out /= rhs;
        }
    }
    return 1;
}

static int sheet_parse_expr(sheet_parse_t *p, int *out) {
    int rhs;
    if (!sheet_parse_term(p, out)) return 0;
    for (;;) {
        char op;
        sheet_skip_ws(p);
        op = *p->s;
        if (op != '+' && op != '-') break;
        p->s++;
        if (!sheet_parse_term(p, &rhs)) return 0;
        if (op == '+') *out += rhs;
        else *out -= rhs;
    }
    return 1;
}

static int sheet_eval_cell(int row, int col, int *out_value, int *out_err) {
    work_sheet_cell_t *cell;
    int parsed = 0;

    if (row < 0 || row >= WORK_SHEET_ROWS || col < 0 || col >= WORK_SHEET_COLS) {
        if (out_err) *out_err = 1;
        return 0;
    }
    cell = &g_sheet_cells[row][col];
    if (cell->eval_state == 2) {
        if (out_value) *out_value = cell->value;
        if (out_err) *out_err = cell->error_code;
        return cell->error_code == 0;
    }
    if (cell->eval_state == 1) {
        cell->error_code = 2;
        str_copy(cell->display, "#CYCLE", sizeof(cell->display));
        if (out_err) *out_err = 2;
        return 0;
    }
    cell->eval_state = 1;
    cell->error_code = 0;
    cell->value = 0;
    cell->is_number = 0;
    if (!cell->raw[0]) {
        cell->display[0] = '\0';
        cell->eval_state = 2;
        if (out_value) *out_value = 0;
        if (out_err) *out_err = 0;
        return 1;
    }
    if (cell->raw[0] == '=') {
        sheet_parse_t p;
        int value = 0;
        p.s = cell->raw + 1;
        p.ok = 1;
        if (!sheet_parse_expr(&p, &value)) p.ok = 0;
        sheet_skip_ws(&p);
        if (*p.s != '\0') p.ok = 0;
        if (!p.ok) {
            cell->error_code = 1;
            str_copy(cell->display, "#ERR", sizeof(cell->display));
            cell->eval_state = 2;
            if (out_err) *out_err = 1;
            return 0;
        }
        cell->value = value;
        cell->is_number = 1;
        i32_to_dec(value, cell->display, sizeof(cell->display));
        cell->eval_state = 2;
        if (out_value) *out_value = value;
        if (out_err) *out_err = 0;
        return 1;
    }
    if (sheet_parse_int(cell->raw, &parsed)) {
        cell->value = parsed;
        cell->is_number = 1;
    }
    str_copy(cell->display, cell->raw, sizeof(cell->display));
    cell->eval_state = 2;
    if (out_value) *out_value = cell->value;
    if (out_err) *out_err = 0;
    return 1;
}

static void sheet_recalc_all(void) {
    for (int r = 0; r < WORK_SHEET_ROWS; r++) {
        for (int c = 0; c < WORK_SHEET_COLS; c++) {
            g_sheet_cells[r][c].eval_state = 0;
            g_sheet_cells[r][c].error_code = 0;
            g_sheet_cells[r][c].value = 0;
            g_sheet_cells[r][c].is_number = 0;
            g_sheet_cells[r][c].display[0] = '\0';
        }
    }
    for (int r = 0; r < WORK_SHEET_ROWS; r++) {
        for (int c = 0; c < WORK_SHEET_COLS; c++) {
            int value;
            int err;
            (void)sheet_eval_cell(r, c, &value, &err);
        }
    }
}

static void sheet_set_active(int row, int col, int keep_anchor) {
    g_sheet_row = clampi(row, 0, WORK_SHEET_ROWS - 1);
    g_sheet_col = clampi(col, 0, WORK_SHEET_COLS - 1);
    if (!keep_anchor) {
        g_sheet_sel_r0 = g_sheet_sel_r1 = g_sheet_row;
        g_sheet_sel_c0 = g_sheet_sel_c1 = g_sheet_col;
    } else {
        g_sheet_sel_r1 = g_sheet_row;
        g_sheet_sel_c1 = g_sheet_col;
    }
    if (g_sheet_row < g_sheet_scroll_row) g_sheet_scroll_row = g_sheet_row;
    if (g_sheet_col < g_sheet_scroll_col) g_sheet_scroll_col = g_sheet_col;
}

static void sheet_begin_edit(int replace, char first_char) {
    work_sheet_cell_t *cell = &g_sheet_cells[g_sheet_row][g_sheet_col];
    g_sheet_editing = 1;
    if (replace) {
        g_sheet_edit_buf[0] = '\0';
        g_sheet_edit_len = 0;
        g_sheet_edit_cursor = 0;
        if (first_char >= 32 && first_char <= 126) {
            linebuf_insert(g_sheet_edit_buf, (int)sizeof(g_sheet_edit_buf),
                           &g_sheet_edit_len, &g_sheet_edit_cursor, first_char);
        }
    } else {
        linebuf_load(g_sheet_edit_buf, (int)sizeof(g_sheet_edit_buf),
                     &g_sheet_edit_len, &g_sheet_edit_cursor, cell->raw);
    }
}

static void sheet_commit_edit(void) {
    work_sheet_cell_t *cell = &g_sheet_cells[g_sheet_row][g_sheet_col];
    str_copy(cell->raw, g_sheet_edit_buf, sizeof(cell->raw));
    g_sheet_editing = 0;
    sheet_recalc_all();
    work_set_message("Sheet recalculated");
}

static void sheet_draw(gui_rect_t r) {
    int form_y;
    int grid_x;
    int grid_y;
    int grid_w;
    int grid_h;
    int row_h = 24;
    int col_w = 82;
    int head_w = 40;
    int vis_cols;
    int vis_rows;
    char cell_name[8];

    sheet_rects(r, &form_y, &grid_x, &grid_y, &grid_w, &grid_h);
    vis_cols = (grid_w - head_w) / col_w;
    vis_rows = (grid_h - row_h) / row_h;
    if (vis_cols < 1) vis_cols = 1;
    if (vis_rows < 1) vis_rows = 1;
    if (g_sheet_row >= g_sheet_scroll_row + vis_rows) g_sheet_scroll_row = g_sheet_row - vis_rows + 1;
    if (g_sheet_col >= g_sheet_scroll_col + vis_cols) g_sheet_scroll_col = g_sheet_col - vis_cols + 1;
    if (g_sheet_scroll_row < 0) g_sheet_scroll_row = 0;
    if (g_sheet_scroll_col < 0) g_sheet_scroll_col = 0;

    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, r.h - WORK_HDR_H, COL_WORK_BG);
    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, WORK_SUBBAR_H, COL_WORK_SUBBAR);
    gfx_draw_string(r.x + 14, r.y + WORK_HDR_H + 7, "Formula", COL_WORK_TEXT_DIM, COL_WORK_SUBBAR);
    sheet_cell_ref_name(g_sheet_row, g_sheet_col, cell_name, sizeof(cell_name));
    draw_field(r.x + 78, r.y + WORK_HDR_H + 4, 44, WORK_BTN_H, cell_name, 0);
    draw_field(r.x + 128, r.y + WORK_HDR_H + 4, r.w - 260, WORK_BTN_H,
               g_sheet_editing ? g_sheet_edit_buf : g_sheet_cells[g_sheet_row][g_sheet_col].raw,
               g_sheet_editing);
    draw_button(r.x + r.w - 120, r.y + WORK_HDR_H + 4, 52, WORK_BTN_H, "Clear", 0);
    draw_button(r.x + r.w - 62, r.y + WORK_HDR_H + 4, 50, WORK_BTN_H, "Calc", 0);

    gfx_fill_rect(grid_x, grid_y, grid_w, grid_h, COL_WORK_PANEL);
    gfx_fill_rect(grid_x, grid_y, grid_w, row_h, COL_WORK_SHEET_HEAD);
    gfx_fill_rect(grid_x, grid_y, head_w, grid_h, COL_WORK_SHEET_HEAD);

    for (int c = 0; c < vis_cols && g_sheet_scroll_col + c < WORK_SHEET_COLS; c++) {
        char lbl[4];
        int cx = grid_x + head_w + c * col_w;
        sheet_col_label(g_sheet_scroll_col + c, lbl, sizeof(lbl));
        gfx_fill_rect(cx, grid_y, col_w, row_h, COL_WORK_SHEET_HEAD);
        gfx_fill_rect(cx, grid_y, 1, grid_h, COL_WORK_BORDER);
        gfx_draw_string(cx + (col_w - (int)str_len(lbl) * FONT_WIDTH) / 2,
                        grid_y + 4, lbl, COL_WORK_TEXT, COL_WORK_SHEET_HEAD);
    }
    for (int vr = 0; vr < vis_rows && g_sheet_scroll_row + vr < WORK_SHEET_ROWS; vr++) {
        int row = g_sheet_scroll_row + vr;
        int y = grid_y + row_h + vr * row_h;
        char num[8];
        i32_to_dec(row + 1, num, sizeof(num));
        gfx_fill_rect(grid_x, y, head_w, row_h, COL_WORK_SHEET_HEAD);
        gfx_fill_rect(grid_x, y, grid_w, 1, COL_WORK_BORDER);
        gfx_draw_string(grid_x + head_w - 8 - (int)str_len(num) * FONT_WIDTH,
                        y + 4, num, COL_WORK_TEXT_DIM, COL_WORK_SHEET_HEAD);
        for (int vc = 0; vc < vis_cols && g_sheet_scroll_col + vc < WORK_SHEET_COLS; vc++) {
            int col = g_sheet_scroll_col + vc;
            int x = grid_x + head_w + vc * col_w;
            int sel_r0 = g_sheet_sel_r0 < g_sheet_sel_r1 ? g_sheet_sel_r0 : g_sheet_sel_r1;
            int sel_r1 = g_sheet_sel_r0 > g_sheet_sel_r1 ? g_sheet_sel_r0 : g_sheet_sel_r1;
            int sel_c0 = g_sheet_sel_c0 < g_sheet_sel_c1 ? g_sheet_sel_c0 : g_sheet_sel_c1;
            int sel_c1 = g_sheet_sel_c0 > g_sheet_sel_c1 ? g_sheet_sel_c0 : g_sheet_sel_c1;
            int in_sel = row >= sel_r0 && row <= sel_r1 && col >= sel_c0 && col <= sel_c1;
            uint32_t bg = in_sel ? COL_WORK_SHEET_SEL : COL_WORK_FIELD;
            char clip[WORK_SHEET_TEXT_MAX];
            gfx_fill_rect(x, y, col_w, row_h, bg);
            gfx_fill_rect(x, y, 1, row_h, COL_WORK_BORDER);
            clip_text(clip, sizeof(clip), g_sheet_cells[row][col].display, (col_w - 8) / FONT_WIDTH);
            gfx_draw_string(x + 4, y + 4, clip, COL_WORK_TEXT, bg);
            if (row == g_sheet_row && col == g_sheet_col) {
                gfx_fill_rect(x, y, col_w, 2, COL_WORK_ACCENT);
                gfx_fill_rect(x, y + row_h - 2, col_w, 2, COL_WORK_ACCENT);
                gfx_fill_rect(x, y, 2, row_h, COL_WORK_ACCENT);
                gfx_fill_rect(x + col_w - 2, y, 2, row_h, COL_WORK_ACCENT);
            }
        }
    }

    if (g_sheet_editing) {
        int field_x = r.x + 132;
        int cursor_x = field_x + 4 + g_sheet_edit_cursor * FONT_WIDTH;
        gfx_fill_rect(cursor_x, r.y + WORK_HDR_H + 7, 2, FONT_HEIGHT, COL_WORK_CURSOR);
    }
}

static int slides_view(gui_rect_t r, int present, work_slide_view_t *view,
                       int *thumb_x, int *thumb_y, int *thumb_w, int *thumb_h) {
    int body_y = r.y + WORK_HDR_H + WORK_SUBBAR_H;
    int body_h = r.h - WORK_HDR_H - WORK_SUBBAR_H - WORK_STATUS_H;
    int side_w = present ? 0 : 112;
    int avail_x = r.x + WORK_PAD + side_w;
    int avail_y = body_y + th_metrics()->gap_md;
    int avail_w = r.w - side_w - WORK_PAD * 2 - 10;
    int avail_h = body_h - (present ? WORK_PAD * 2 : 72);

    if (avail_w < 1) avail_w = 1;
    if (avail_h < 1) avail_h = 1;

    th_fit_aspect_rect(avail_x, avail_y, avail_w, avail_h,
                       480, 270, 0,
                       &view->x, &view->y, &view->w, &view->h);
    view->scale_fp = (view->w * SCALE_FP_ONE) / 480;
    if (view->scale_fp > SCALE_FP_ONE) view->scale_fp = SCALE_FP_ONE;
    if (view->scale_fp < 96) view->scale_fp = 96;

    if (thumb_x) {
        *thumb_x = r.x + WORK_PAD;
        *thumb_y = body_y + th_metrics()->gap_md;
        *thumb_w = side_w - 10;
        *thumb_h = body_h - 24;
    }
    return body_y;
}

static int slide_hit_element(const work_slide_view_t *view, int lx, int ly) {
    int sx = unscale_pos(lx - view->x, view->scale_fp);
    int sy = unscale_pos(ly - view->y, view->scale_fp);
    work_slide_t *sl = &g_slides[g_slide_index];
    for (int i = sl->elem_count - 1; i >= 0; i--) {
        work_slide_elem_t *e = &sl->elems[i];
        if (sx >= e->x && sx < e->x + e->w && sy >= e->y && sy < e->y + e->h) return i;
    }
    return -1;
}

static void slide_sync_text_editor(void) {
    work_slide_t *sl;
    if (g_slide_index < 0 || g_slide_index >= g_slide_count) return;
    sl = &g_slides[g_slide_index];
    if (g_slide_sel < 0 || g_slide_sel >= sl->elem_count) {
        g_slide_text_edit = 0;
        return;
    }
    if (sl->elems[g_slide_sel].type != SLIDE_ELEM_TEXT) {
        g_slide_text_edit = 0;
        return;
    }
    linebuf_load(g_slide_text_buf, (int)sizeof(g_slide_text_buf),
                 &g_slide_text_len, &g_slide_text_cursor, sl->elems[g_slide_sel].text);
}

static void slide_add_element(int type) {
    work_slide_t *sl = &g_slides[g_slide_index];
    work_slide_elem_t *e;
    if (sl->elem_count >= WORK_SLIDE_ELEM_MAX) {
        work_set_message("element limit reached");
        return;
    }
    e = &sl->elems[sl->elem_count++];
    mem_set(e, 0, sizeof(*e));
    e->type = (uint8_t)type;
    e->x = 60 + sl->elem_count * 8;
    e->y = 50 + sl->elem_count * 8;
    e->w = (type == SLIDE_ELEM_TEXT) ? 140 : 120;
    e->h = (type == SLIDE_ELEM_TEXT) ? 44 : 72;
    e->color = (type == SLIDE_ELEM_TEXT) ? gfx_rgb(34, 41, 52) : gfx_rgb(80, 122, 214);
    if (type == SLIDE_ELEM_TEXT) {
        str_copy(e->text, "Text box", sizeof(e->text));
        e->len = (uint16_t)str_len(e->text);
    }
    g_slide_sel = sl->elem_count - 1;
    slide_sync_text_editor();
}

static void slide_duplicate_current(void) {
    if (g_slide_count >= WORK_SLIDE_MAX) {
        work_set_message("slide limit reached");
        return;
    }
    for (int i = g_slide_count; i > g_slide_index + 1; i--) g_slides[i] = g_slides[i - 1];
    g_slides[g_slide_index + 1] = g_slides[g_slide_index];
    g_slide_index++;
    g_slide_count++;
    g_slide_sel = -1;
    slide_sync_text_editor();
}

static void slide_delete_current(void) {
    if (g_slide_count <= 1) {
        work_set_message("need at least one slide");
        return;
    }
    for (int i = g_slide_index; i + 1 < g_slide_count; i++) g_slides[i] = g_slides[i + 1];
    g_slide_count--;
    if (g_slide_index >= g_slide_count) g_slide_index = g_slide_count - 1;
    g_slide_sel = -1;
    slide_sync_text_editor();
}

static void slide_move_index(int dir) {
    int other = g_slide_index + dir;
    work_slide_t tmp;
    if (other < 0 || other >= g_slide_count) return;
    tmp = g_slides[g_slide_index];
    g_slides[g_slide_index] = g_slides[other];
    g_slides[other] = tmp;
    g_slide_index = other;
}

static void slides_draw_one(const work_slide_view_t *view, work_slide_t *sl, int selected) {
    gfx_fill_rect(view->x - 2, view->y - 2, view->w + 4, view->h + 4,
                  selected ? COL_WORK_ACCENT : COL_WORK_SLIDE_FRAME);
    gfx_fill_rect(view->x, view->y, view->w, view->h, sl->bg);
    for (int i = 0; i < sl->elem_count; i++) {
        work_slide_elem_t *e = &sl->elems[i];
        int x = view->x + scale_pos(e->x, view->scale_fp);
        int y = view->y + scale_pos(e->y, view->scale_fp);
        int w = scale_dim(e->w, view->scale_fp);
        int h = scale_dim(e->h, view->scale_fp);
        if (e->type == SLIDE_ELEM_RECT) {
            gfx_fill_rect(x, y, w, h, e->color);
        } else {
            char clip[WORK_SLIDE_TEXT_MAX];
            int max_chars = (w - 8) / FONT_WIDTH;
            if (max_chars < 1) max_chars = 1;
            clip_text(clip, sizeof(clip), e->text, max_chars);
            gfx_fill_rect(x, y, w, h, gfx_rgb(255, 255, 255));
            gfx_fill_rect(x, y, w, 1, e->color);
            gfx_fill_rect(x, y + h - 1, w, 1, e->color);
            gfx_fill_rect(x, y, 1, h, e->color);
            gfx_fill_rect(x + w - 1, y, 1, h, e->color);
            gfx_draw_string(x + 4, y + (h - FONT_HEIGHT) / 2, clip, e->color, gfx_rgb(255, 255, 255));
        }
        if (selected && i == g_slide_sel) {
            gfx_fill_rect(x - 2, y - 2, w + 4, 2, COL_WORK_ACCENT);
            gfx_fill_rect(x - 2, y + h, w + 4, 2, COL_WORK_ACCENT);
            gfx_fill_rect(x - 2, y - 2, 2, h + 4, COL_WORK_ACCENT);
            gfx_fill_rect(x + w, y - 2, 2, h + 4, COL_WORK_ACCENT);
        }
    }
}

static void slides_draw(gui_rect_t r) {
    int thumb_x;
    int thumb_y;
    int thumb_w;
    int thumb_h;
    work_slide_view_t view;

    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, r.h - WORK_HDR_H, COL_WORK_BG);
    if (g_slide_present) {
        gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, r.h - WORK_HDR_H, gfx_rgb(22, 26, 34));
        slides_view(r, 1, &view, 0, 0, 0, 0);
        slides_draw_one(&view, &g_slides[g_slide_index], 0);
        gfx_draw_string(r.x + 16, r.y + r.h - WORK_STATUS_H - 18,
                        "Slides: Left/Right move, Esc exits presentation",
                        gfx_rgb(236, 241, 247), gfx_rgb(22, 26, 34));
        return;
    }

    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, WORK_SUBBAR_H, COL_WORK_SUBBAR);
    draw_button(r.x + 12,  r.y + WORK_HDR_H + 4, 68, WORK_BTN_H, "Add Text", 0);
    draw_button(r.x + 86,  r.y + WORK_HDR_H + 4, 68, WORK_BTN_H, "Add Rect", 0);
    draw_button(r.x + 160, r.y + WORK_HDR_H + 4, 46, WORK_BTN_H, "Dup", 0);
    draw_button(r.x + 212, r.y + WORK_HDR_H + 4, 46, WORK_BTN_H, "Del", 0);
    draw_button(r.x + 264, r.y + WORK_HDR_H + 4, 34, WORK_BTN_H, "Up", 0);
    draw_button(r.x + 304, r.y + WORK_HDR_H + 4, 48, WORK_BTN_H, "Down", 0);
    draw_button(r.x + 358, r.y + WORK_HDR_H + 4, 40, WORK_BTN_H, "BG", 0);
    draw_button(r.x + r.w - 82, r.y + WORK_HDR_H + 4, 70, WORK_BTN_H, "Present", 0);

    slides_view(r, 0, &view, &thumb_x, &thumb_y, &thumb_w, &thumb_h);
    gfx_fill_rect(thumb_x, thumb_y, thumb_w, thumb_h, COL_WORK_PANEL);
    gfx_draw_string(thumb_x + 10, thumb_y + 8, "Slides", COL_WORK_TEXT_DIM, COL_WORK_PANEL);
    for (int i = 0; i < g_slide_count; i++) {
        int ty = thumb_y + 30 + i * 62;
        int sel = i == g_slide_index;
        work_slide_view_t thumb_view;
        thumb_view.scale_fp = 96;
        thumb_view.w = scale_dim(480, thumb_view.scale_fp);
        thumb_view.h = scale_dim(270, thumb_view.scale_fp);
        thumb_view.x = thumb_x + 10;
        thumb_view.y = ty;
        gfx_fill_rect(thumb_x + 6, ty - 4, thumb_w - 12, 56, sel ? COL_WORK_THUMB_SEL : COL_WORK_PANEL);
        slides_draw_one(&thumb_view, &g_slides[i], 0);
    }

    gfx_fill_rect(view.x - 18, view.y - 18, view.w + 36, view.h + 36, COL_WORK_SLIDE_AREA);
    slides_draw_one(&view, &g_slides[g_slide_index], 1);

    if (g_slide_sel >= 0 && g_slide_sel < g_slides[g_slide_index].elem_count &&
        g_slides[g_slide_index].elems[g_slide_sel].type == SLIDE_ELEM_TEXT) {
        gfx_draw_string(view.x, view.y + view.h + 24, "Text", COL_WORK_TEXT_DIM, COL_WORK_BG);
        draw_field(view.x + 38, view.y + view.h + 20, view.w - 38, WORK_BTN_H,
                   g_slide_text_edit ? g_slide_text_buf : g_slides[g_slide_index].elems[g_slide_sel].text,
                   g_slide_text_edit);
        if (g_slide_text_edit) {
            int cx = view.x + 42 + g_slide_text_cursor * FONT_WIDTH;
            gfx_fill_rect(cx, view.y + view.h + 24, 2, FONT_HEIGHT, COL_WORK_CURSOR);
        }
    } else {
        gfx_draw_string(view.x, view.y + view.h + 24,
                        "Session only. Select a text box to edit its label.",
                        COL_WORK_TEXT_DIM, COL_WORK_BG);
    }
}

static void work_draw_home(gui_rect_t r) {
    int start_x = r.x + (r.w - (WORK_CARD_W * 3 + 24)) / 2;
    int y = r.y + WORK_HDR_H + 46;
    static const char *titles[] = { "Docs", "Sheets", "Slides" };
    static const char *desc[] = {
        "Styled pages and text file save.",
        "Formula grid with live recalc.",
        "Deck canvas with presentation mode.",
    };
    gfx_fill_rect(r.x, r.y + WORK_HDR_H, r.w, r.h - WORK_HDR_H, COL_WORK_BG);
    gfx_draw_string(r.x + 24, r.y + WORK_HDR_H + 18,
                    "180 Work brings Docs, Sheets, and Slides into one desktop app.",
                    COL_WORK_TEXT, COL_WORK_BG);
    for (int i = 0; i < 3; i++) {
        int x = start_x + i * (WORK_CARD_W + 12);
        uint32_t color = (i == 0) ? COL_WORK_CARD_DOCS :
                         (i == 1) ? COL_WORK_CARD_SHEET : COL_WORK_CARD_SLIDE;
        gfx_fill_rect(x, y, WORK_CARD_W, WORK_CARD_H, COL_WORK_PANEL);
        gfx_fill_rect(x, y, WORK_CARD_W, 36, color);
        gfx_draw_string(x + 14, y + 10, titles[i], gfx_rgb(255, 255, 255), color);
        gfx_draw_string(x + 14, y + 54, desc[i], COL_WORK_TEXT_DIM, COL_WORK_PANEL);
        draw_button(x + 14, y + WORK_CARD_H - 34, 82, WORK_BTN_H, "Open", 0);
    }
}

static void work_draw_header(gui_rect_t r) {
    static const char *labels[] = { "Home", "Docs", "Sheets", "Slides" };
    gfx_fill_rect(r.x, r.y, r.w, WORK_HDR_H, COL_WORK_HDR);
    gfx_draw_string(r.x + 14, r.y + 12, "180 Work", COL_WORK_HDR_TXT, COL_WORK_HDR);
    for (int i = 0; i < 4; i++) {
        int bx, by, bw, bh;
        work_mode_rect(i, &bx, &by, &bw, &bh);
        draw_button(r.x + bx, r.y + by, bw, bh, labels[i], g_mode == (work_mode_t)i);
    }
    gfx_draw_string(r.x + r.w - 170, r.y + 12,
                    g_mode == WORK_MODE_DOCS ? "File-backed Docs" :
                    (g_mode == WORK_MODE_HOME ? "Replace AX Code" : "Session-first tools"),
                    gfx_rgb(216, 226, 239), COL_WORK_HDR);
}

static void work_draw_status(gui_rect_t r) {
    char line[160];
    gfx_fill_rect(r.x, r.y + r.h - WORK_STATUS_H, r.w, WORK_STATUS_H, COL_WORK_STATUS);
    line[0] = '\0';
    str_copy(line, mode_label(g_mode), sizeof(line));
    str_cat(line, " | ", sizeof(line));
    if (g_mode == WORK_MODE_DOCS) {
        str_cat(line, g_doc_dirty ? "unsaved changes" : "saved", sizeof(line));
    } else if (g_mode == WORK_MODE_SHEETS) {
        str_cat(line, "session only", sizeof(line));
    } else if (g_mode == WORK_MODE_SLIDES) {
        str_cat(line, g_slide_present ? "presentation" : "session only", sizeof(line));
    } else {
        str_cat(line, "pick a workspace", sizeof(line));
    }
    if (g_message[0]) {
        str_cat(line, " | ", sizeof(line));
        str_cat(line, g_message, sizeof(line));
    }
    gfx_draw_string(r.x + 10, r.y + r.h - WORK_STATUS_H + 4, line, COL_WORK_STATUS_TXT, COL_WORK_STATUS);
}

static void work_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    work_update_title();
    work_draw_header(r);
    if (g_mode == WORK_MODE_DOCS) docs_draw(r);
    else if (g_mode == WORK_MODE_SHEETS) sheet_draw(r);
    else if (g_mode == WORK_MODE_SLIDES) slides_draw(r);
    else work_draw_home(r);
    work_draw_status(r);
    if (g_open_overlay) {
        int ox = r.x + 80;
        int oy = r.y + 94;
        int ow = r.w - 160;
        int oh = 60;
        gfx_fill_rect(ox - 2, oy - 2, ow + 4, oh + 4, COL_WORK_BORDER);
        gfx_fill_rect(ox, oy, ow, oh, COL_WORK_PANEL);
        gfx_draw_string(ox + 10, oy + 8, "Open docs path:", COL_WORK_TEXT, COL_WORK_PANEL);
        draw_field(ox + 10, oy + 28, ow - 20, WORK_BTN_H, g_open_buf, 1);
        gfx_fill_rect(ox + 14 + g_open_len * FONT_WIDTH, oy + 32, 2, FONT_HEIGHT, COL_WORK_CURSOR);
    }
}

static void work_switch_mode(work_mode_t mode) {
    g_mode = mode;
    g_open_overlay = 0;
    if (g_mode != WORK_MODE_SLIDES) g_slide_present = 0;
    work_update_title();
}

static void work_open_docs_path(const char *path) {
    docs_load_file(path && path[0] ? path : "/ROOT/UNTITLED.TXT");
    g_mode = WORK_MODE_DOCS;
    work_update_title();
}

static void work_global_close(void) {
    if (g_win_id >= 0) gui_window_close(g_win_id);
}

static void work_docs_handle_key(gui_rect_t r, char key) {
    (void)r;
    if (key == 0x13) {
        docs_save_file();
        return;
    }
    if (key == KEY_LEFT) docs_move_left();
    else if (key == KEY_RIGHT) docs_move_right();
    else if (key == KEY_UP) docs_move_vertical(-1);
    else if (key == KEY_DOWN) docs_move_vertical(1);
    else if (key == KEY_PAGEUP) docs_move_vertical(-4);
    else if (key == KEY_PAGEDOWN) docs_move_vertical(4);
    else if (key == KEY_HOME) g_doc_cursor = 0;
    else if (key == KEY_END) g_doc_cursor = (int)g_doc_blocks[g_doc_block_sel].len;
    else if (key == KEY_DELETE) docs_delete();
    else if (key == '\b' || key == (char)0x7F) docs_backspace();
    else if (key == '\r' || key == '\n') docs_split_block();
    else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) docs_insert_char(key);
    docs_ensure_visible(gui_window_content(g_win_id));
}

static void work_sheets_handle_key(char key) {
    if (key == 0x13) {
        work_set_message("Sheets is session only");
        return;
    }
    if (g_sheet_editing) {
        if (key == 0x1B) {
            g_sheet_editing = 0;
            return;
        }
        if (key == '\r' || key == '\n') {
            sheet_commit_edit();
            return;
        }
        if (key == KEY_LEFT && g_sheet_edit_cursor > 0) g_sheet_edit_cursor--;
        else if (key == KEY_RIGHT && g_sheet_edit_cursor < g_sheet_edit_len) g_sheet_edit_cursor++;
        else if (key == KEY_HOME) g_sheet_edit_cursor = 0;
        else if (key == KEY_END) g_sheet_edit_cursor = g_sheet_edit_len;
        else if (key == KEY_DELETE) linebuf_delete(g_sheet_edit_buf, &g_sheet_edit_len, &g_sheet_edit_cursor);
        else if (key == '\b' || key == (char)0x7F) linebuf_backspace(g_sheet_edit_buf, &g_sheet_edit_len, &g_sheet_edit_cursor);
        else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
            linebuf_insert(g_sheet_edit_buf, (int)sizeof(g_sheet_edit_buf),
                           &g_sheet_edit_len, &g_sheet_edit_cursor, key);
        }
        return;
    }
    if (key == KEY_LEFT) sheet_set_active(g_sheet_row, g_sheet_col - 1, 0);
    else if (key == KEY_RIGHT) sheet_set_active(g_sheet_row, g_sheet_col + 1, 0);
    else if (key == KEY_UP) sheet_set_active(g_sheet_row - 1, g_sheet_col, 0);
    else if (key == KEY_DOWN) sheet_set_active(g_sheet_row + 1, g_sheet_col, 0);
    else if (key == KEY_PAGEUP) sheet_set_active(g_sheet_row - 10, g_sheet_col, 0);
    else if (key == KEY_PAGEDOWN) sheet_set_active(g_sheet_row + 10, g_sheet_col, 0);
    else if (key == KEY_HOME) sheet_set_active(0, g_sheet_col, 0);
    else if (key == KEY_END) sheet_set_active(WORK_SHEET_ROWS - 1, g_sheet_col, 0);
    else if (key == '\r' || key == '\n') sheet_begin_edit(0, 0);
    else if (key == KEY_DELETE || key == '\b' || key == (char)0x7F) {
        g_sheet_cells[g_sheet_row][g_sheet_col].raw[0] = '\0';
        sheet_recalc_all();
        work_set_message("Cell cleared");
    } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
        sheet_begin_edit(1, key);
    }
}

static void work_slides_handle_key(char key) {
    work_slide_t *sl = &g_slides[g_slide_index];

    if (g_slide_present) {
        if (key == 0x1B || key == '\r' || key == '\n') {
            g_slide_present = 0;
            return;
        }
        if ((key == KEY_RIGHT || key == KEY_DOWN || key == ' ') && g_slide_index + 1 < g_slide_count) {
            g_slide_index++;
            g_slide_sel = -1;
            return;
        }
        if ((key == KEY_LEFT || key == KEY_UP) && g_slide_index > 0) {
            g_slide_index--;
            g_slide_sel = -1;
            return;
        }
        return;
    }

    if (g_slide_text_edit) {
        if (key == 0x1B) {
            g_slide_text_edit = 0;
            return;
        }
        if (key == '\r' || key == '\n') {
            if (g_slide_sel >= 0 && g_slide_sel < sl->elem_count &&
                sl->elems[g_slide_sel].type == SLIDE_ELEM_TEXT) {
                str_copy(sl->elems[g_slide_sel].text, g_slide_text_buf, sizeof(sl->elems[g_slide_sel].text));
                sl->elems[g_slide_sel].len = (uint16_t)str_len(sl->elems[g_slide_sel].text);
            }
            g_slide_text_edit = 0;
            return;
        }
        if (key == KEY_LEFT && g_slide_text_cursor > 0) g_slide_text_cursor--;
        else if (key == KEY_RIGHT && g_slide_text_cursor < g_slide_text_len) g_slide_text_cursor++;
        else if (key == KEY_HOME) g_slide_text_cursor = 0;
        else if (key == KEY_END) g_slide_text_cursor = g_slide_text_len;
        else if (key == KEY_DELETE) linebuf_delete(g_slide_text_buf, &g_slide_text_len, &g_slide_text_cursor);
        else if (key == '\b' || key == (char)0x7F) linebuf_backspace(g_slide_text_buf, &g_slide_text_len, &g_slide_text_cursor);
        else if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
            linebuf_insert(g_slide_text_buf, (int)sizeof(g_slide_text_buf),
                           &g_slide_text_len, &g_slide_text_cursor, key);
        }
        return;
    }

    if (key == KEY_DELETE && g_slide_sel >= 0 && g_slide_sel < sl->elem_count) {
        for (int i = g_slide_sel; i + 1 < sl->elem_count; i++) sl->elems[i] = sl->elems[i + 1];
        sl->elem_count--;
        if (g_slide_sel >= sl->elem_count) g_slide_sel = sl->elem_count - 1;
        slide_sync_text_editor();
        return;
    }
    if (key == '\r' || key == '\n') {
        g_slide_present = 1;
        return;
    }
    if (key == KEY_LEFT && g_slide_sel >= 0) sl->elems[g_slide_sel].x -= 4;
    else if (key == KEY_RIGHT && g_slide_sel >= 0) sl->elems[g_slide_sel].x += 4;
    else if (key == KEY_UP && g_slide_sel >= 0) sl->elems[g_slide_sel].y -= 4;
    else if (key == KEY_DOWN && g_slide_sel >= 0) sl->elems[g_slide_sel].y += 4;
    else if (key == 't' || key == 'T') slide_add_element(SLIDE_ELEM_TEXT);
    else if (key == 'r' || key == 'R') slide_add_element(SLIDE_ELEM_RECT);
    else if (key == 'p' || key == 'P') g_slide_present = 1;

    if (g_slide_sel >= 0 && g_slide_sel < sl->elem_count) {
        work_slide_elem_t *e = &sl->elems[g_slide_sel];
        e->x = clampi(e->x, 0, 480 - e->w);
        e->y = clampi(e->y, 0, 270 - e->h);
    }
}

static void work_on_key(int win_id, char key) {
    gui_rect_t r = gui_window_content(win_id);
    (void)win_id;

    if (g_open_overlay) {
        if (key == 0x1B) {
            g_open_overlay = 0;
        } else if (key == '\r' || key == '\n') {
            if (g_open_len > 0) {
                g_open_overlay = 0;
                work_open_docs_path(g_open_buf);
            }
        } else if ((key == '\b' || key == (char)0x7F) && g_open_len > 0) {
            g_open_buf[--g_open_len] = '\0';
        } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                   g_open_len + 1 < (int)sizeof(g_open_buf)) {
            g_open_buf[g_open_len++] = key;
            g_open_buf[g_open_len] = '\0';
        }
        return;
    }

    if (key == 0x11) {
        work_global_close();
        return;
    }

    if (g_mode == WORK_MODE_DOCS) work_docs_handle_key(r, key);
    else if (g_mode == WORK_MODE_SHEETS) work_sheets_handle_key(key);
    else if (g_mode == WORK_MODE_SLIDES) work_slides_handle_key(key);
}

static int work_hit_mode_button(gui_rect_t r, int x, int y) {
    (void)r;
    for (int i = 0; i < 4; i++) {
        int bx, by, bw, bh;
        work_mode_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) return i;
    }
    return -1;
}

static int work_hit_home_card(gui_rect_t r, int x, int y) {
    int start_x = (r.w - (WORK_CARD_W * 3 + 24)) / 2;
    int cy = WORK_HDR_H + 46;
    for (int i = 0; i < 3; i++) {
        int cx = start_x + i * (WORK_CARD_W + 12);
        if (x >= cx && x < cx + WORK_CARD_W && y >= cy && y < cy + WORK_CARD_H) return i + 1;
    }
    return -1;
}

static void sheet_mouse(gui_rect_t r, int x, int y) {
    int form_y;
    int grid_x;
    int grid_y;
    int grid_w;
    int grid_h;
    int row_h = 24;
    int col_w = 82;
    int head_w = 40;

    sheet_rects(r, &form_y, &grid_x, &grid_y, &grid_w, &grid_h);
    if (x >= 128 && x < r.w - 132 && y >= WORK_HDR_H + 4 && y < WORK_HDR_H + 4 + WORK_BTN_H) {
        sheet_begin_edit(0, 0);
        return;
    }
    if (x >= r.w - 120 && x < r.w - 68 && y >= WORK_HDR_H + 4 && y < WORK_HDR_H + 4 + WORK_BTN_H) {
        g_sheet_cells[g_sheet_row][g_sheet_col].raw[0] = '\0';
        sheet_recalc_all();
        work_set_message("Cell cleared");
        return;
    }
    if (x >= r.w - 62 && x < r.w - 12 && y >= WORK_HDR_H + 4 && y < WORK_HDR_H + 4 + WORK_BTN_H) {
        sheet_recalc_all();
        work_set_message("Sheet recalculated");
        return;
    }
    if (x < grid_x - r.x + head_w || y < grid_y - r.y + row_h ||
        x >= grid_x - r.x + grid_w || y >= grid_y - r.y + grid_h) {
        return;
    }
    {
        int col = (x - (grid_x - r.x) - head_w) / col_w + g_sheet_scroll_col;
        int row = (y - (grid_y - r.y) - row_h) / row_h + g_sheet_scroll_row;
        if (row >= 0 && row < WORK_SHEET_ROWS && col >= 0 && col < WORK_SHEET_COLS) {
            sheet_set_active(row, col, 0);
            g_sheet_dragging = 1;
        }
    }
}

static void docs_mouse(gui_rect_t r, int x, int y) {
    int bx, by, bw, bh;
    int page_x;
    int page_y;
    int page_w;
    int page_h;

    for (int i = 0; i < 6; i++) {
        work_docs_button_rect(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (i == 0) docs_reset_new();
            else if (i == 1) {
                str_copy(g_open_buf, g_doc_path, sizeof(g_open_buf));
                g_open_len = (int)str_len(g_open_buf);
                g_open_overlay = 1;
            } else if (i == 2) docs_save_file();
            else if (i == 3) docs_cycle_type();
            else if (i == 4) docs_cycle_align();
            else if (i == 5) docs_cycle_size();
            work_update_title();
            return;
        }
    }

    docs_ensure_visible(r);
    (void)docs_layout(r, &page_x, &page_y, &page_w, &page_h);
    if (x < page_x - r.x || x >= page_x - r.x + page_w ||
        y < page_y - r.y || y >= page_y - r.y + page_h) {
        return;
    }
    for (int i = 0; i < g_doc_block_count; i++) {
        work_doc_block_t *b = &g_doc_blocks[i];
        int top = page_y - r.y + 18 + g_doc_layout_top[i] - g_doc_scroll;
        int font_px = clampi((int)b->size_px, 12, 32);
        int char_w = scaled_char_w(font_px);
        int prefix = doc_block_prefix_chars(b);
        int cols = (page_w - 48) / char_w - prefix;
        int line_h = font_px + 2;
        int lines;

        if (cols < 1) cols = 1;
        lines = ((int)b->len + cols - 1) / cols;
        if (lines < 1) lines = 1;
        if (y < top || y >= top + g_doc_layout_h[i]) continue;
        g_doc_block_sel = i;
        {
            int line = clampi((y - top) / line_h, 0, lines - 1);
            int off = line * cols;
            int remain = (int)b->len - off;
            int chunk = remain > cols ? cols : remain;
            int line_px = (prefix + chunk) * char_w;
            int tx = page_x - r.x + 24;
            int relx;
            if (b->align == DOC_ALIGN_CENTER) tx = page_x - r.x + (page_w - line_px) / 2;
            else if (b->align == DOC_ALIGN_RIGHT) tx = page_x - r.x + page_w - 24 - line_px;
            relx = x - tx - prefix * char_w;
            if (relx < 0) relx = 0;
            g_doc_cursor = off + relx / char_w;
            if (g_doc_cursor > (int)b->len) g_doc_cursor = (int)b->len;
        }
        docs_ensure_visible(r);
        return;
    }
}

static void slides_mouse(gui_rect_t r, int x, int y) {
    work_slide_view_t view;
    int thumb_x;
    int thumb_y;
    int thumb_w;
    int thumb_h;

    if (g_slide_present) {
        if (x < r.w / 2) {
            if (g_slide_index > 0) g_slide_index--;
        } else {
            if (g_slide_index + 1 < g_slide_count) g_slide_index++;
        }
        return;
    }

    if (y >= WORK_HDR_H + 4 && y < WORK_HDR_H + 4 + WORK_BTN_H) {
        if (x >= 12 && x < 80) { slide_add_element(SLIDE_ELEM_TEXT); return; }
        if (x >= 86 && x < 154) { slide_add_element(SLIDE_ELEM_RECT); return; }
        if (x >= 160 && x < 206) { slide_duplicate_current(); return; }
        if (x >= 212 && x < 258) { slide_delete_current(); return; }
        if (x >= 264 && x < 298) { slide_move_index(-1); return; }
        if (x >= 304 && x < 352) { slide_move_index(1); return; }
        if (x >= 358 && x < 398) {
            g_slides[g_slide_index].bg = slide_bg_for_index(g_slide_index + g_slides[g_slide_index].elem_count + 1);
            return;
        }
        if (x >= r.w - 82 && x < r.w - 12) { g_slide_present = 1; return; }
    }

    slides_view(r, 0, &view, &thumb_x, &thumb_y, &thumb_w, &thumb_h);
    if (x >= thumb_x - r.x && x < thumb_x - r.x + thumb_w &&
        y >= thumb_y - r.y + 30 && y < thumb_y - r.y + thumb_h) {
        int idx = (y - (thumb_y - r.y) - 30) / 62;
        if (idx >= 0 && idx < g_slide_count) {
            g_slide_index = idx;
            g_slide_sel = -1;
            slide_sync_text_editor();
            return;
        }
    }

    if (g_slide_sel >= 0 && g_slide_sel < g_slides[g_slide_index].elem_count &&
        g_slides[g_slide_index].elems[g_slide_sel].type == SLIDE_ELEM_TEXT) {
        int text_y = view.y - r.y + view.h + 20;
        if (x >= view.x - r.x + 38 && x < view.x - r.x + view.w &&
            y >= text_y && y < text_y + WORK_BTN_H) {
            g_slide_text_edit = 1;
            slide_sync_text_editor();
            return;
        }
    }

    if (x >= view.x - r.x && x < view.x - r.x + view.w &&
        y >= view.y - r.y && y < view.y - r.y + view.h) {
        int hit = slide_hit_element(&view, x, y);
        g_slide_sel = hit;
        if (hit >= 0) {
            work_slide_elem_t *e = &g_slides[g_slide_index].elems[hit];
            g_slide_drag_sel = hit;
            g_slide_drag_ox = unscale_pos(x - (view.x - r.x), view.scale_fp) - e->x;
            g_slide_drag_oy = unscale_pos(y - (view.y - r.y), view.scale_fp) - e->y;
            slide_sync_text_editor();
        } else {
            g_slide_text_edit = 0;
        }
    }
}

static void work_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    gui_rect_t r = gui_window_content(win_id);
    int mode_hit;

    (void)buttons;
    mode_hit = work_hit_mode_button(r, x, y);
    if (mode_hit >= 0) {
        work_switch_mode((work_mode_t)mode_hit);
        return;
    }
    if (g_mode == WORK_MODE_HOME) {
        int card = work_hit_home_card(r, x, y);
        if (card >= 0) work_switch_mode((work_mode_t)card);
        return;
    }
    if (g_mode == WORK_MODE_DOCS) docs_mouse(r, x, y);
    else if (g_mode == WORK_MODE_SHEETS) sheet_mouse(r, x, y);
    else if (g_mode == WORK_MODE_SLIDES) slides_mouse(r, x, y);
}

static int work_on_tick(int win_id, uint32_t now) {
    gui_rect_t r = gui_window_content(win_id);
    int dirty = 0;
    (void)now;

    if (g_mode == WORK_MODE_SHEETS && g_sheet_dragging) {
        if (mouse_buttons() & 1u) {
            int form_y;
            int grid_x;
            int grid_y;
            int grid_w;
            int grid_h;
            int row_h = 24;
            int col_w = 82;
            int head_w = 40;
            int mx = mouse_x() - r.x;
            int my = mouse_y() - r.y;
            sheet_rects(r, &form_y, &grid_x, &grid_y, &grid_w, &grid_h);
            if (mx >= grid_x - r.x + head_w && my >= grid_y - r.y + row_h &&
                mx < grid_x - r.x + grid_w && my < grid_y - r.y + grid_h) {
                int col = (mx - (grid_x - r.x) - head_w) / col_w + g_sheet_scroll_col;
                int row = (my - (grid_y - r.y) - row_h) / row_h + g_sheet_scroll_row;
                row = clampi(row, 0, WORK_SHEET_ROWS - 1);
                col = clampi(col, 0, WORK_SHEET_COLS - 1);
                if (row != g_sheet_sel_r1 || col != g_sheet_sel_c1) {
                    g_sheet_sel_r1 = row;
                    g_sheet_sel_c1 = col;
                    dirty = 1;
                }
            }
        } else {
            g_sheet_dragging = 0;
        }
    }

    if (g_mode == WORK_MODE_SLIDES && g_slide_drag_sel >= 0 && !g_slide_present) {
        if (mouse_buttons() & 1u) {
            work_slide_view_t view;
            slides_view(r, 0, &view, 0, 0, 0, 0);
            {
                int mx = mouse_x() - r.x;
                int my = mouse_y() - r.y;
                int sx = unscale_pos(mx - (view.x - r.x), view.scale_fp) - g_slide_drag_ox;
                int sy = unscale_pos(my - (view.y - r.y), view.scale_fp) - g_slide_drag_oy;
                work_slide_elem_t *e = &g_slides[g_slide_index].elems[g_slide_drag_sel];
                sx = clampi(sx, 0, 480 - e->w);
                sy = clampi(sy, 0, 270 - e->h);
                if (sx != e->x || sy != e->y) {
                    e->x = sx;
                    e->y = sy;
                    dirty = 1;
                }
            }
        } else {
            g_slide_drag_sel = -1;
        }
    }
    return dirty;
}

static void work_on_close(int win_id) {
    gui_window_t *w = gui_get_window(win_id);
    if (g_doc_dirty && !g_doc_close_armed) {
        if (w) w->close_cancelled = 1;
        g_doc_close_armed = 1;
        g_mode = WORK_MODE_DOCS;
        work_set_message("Unsaved Docs changes. Close again to discard.");
        return;
    }
    g_win_id = -1;
}

void work_gui_open(work_mode_t mode, const char *path) {
    gui_rect_t rect;
    gui_window_t *w;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        if (mode == WORK_MODE_DOCS && path && path[0]) {
            work_open_docs_path(path);
        } else {
            work_switch_mode(mode);
        }
        gui_window_focus(g_win_id);
        return;
    }

    work_reset_all();
    if (mode == WORK_MODE_DOCS && path && path[0]) work_open_docs_path(path);
    else g_mode = mode;

    gui_window_suggest_rect(860, 580, &rect);
    g_win_id = gui_window_create("180 Work", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_WORK180;
    w->on_paint = work_on_paint;
    w->on_tick = work_on_tick;
    w->on_key = work_on_key;
    w->on_mouse = work_on_mouse;
    w->on_close = work_on_close;
    work_update_title();
    sheet_recalc_all();
}

void work_gui_launch(void) {
    work_gui_open(WORK_MODE_HOME, 0);
}

/* ---- Unit tests (called by the CI test runner) ---- */

void work_run_tests(void) {
    int val, err;

    /* ---- Docs ---- */
    docs_reset_new();
    /* After reset there should be 2 default blocks (Title + Body) */
    test_assert(g_doc_block_count == 2, "work docs: reset block count");
    test_assert(g_doc_blocks[0].type == DOC_BLOCK_TITLE, "work docs: block[0] is title");
    test_assert(g_doc_blocks[1].type == DOC_BLOCK_BODY,  "work docs: block[1] is body");

    /* Insert an extra block */
    docs_insert_block(2, DOC_BLOCK_BULLET);
    test_assert(g_doc_block_count == 3, "work docs: insert block");
    test_assert(g_doc_blocks[2].type == DOC_BLOCK_BULLET, "work docs: inserted block type");

    /* Write text into block 0 by direct assignment */
    str_copy(g_doc_blocks[0].text, "CI Title", sizeof(g_doc_blocks[0].text));
    g_doc_blocks[0].len = (uint16_t)str_len(g_doc_blocks[0].text);
    test_assert(str_eq(g_doc_blocks[0].text, "CI Title"), "work docs: block text");

    /* Delete the bullet block we inserted */
    docs_delete_block(2);
    test_assert(g_doc_block_count == 2, "work docs: delete block");

    /* ---- Sheets ---- */
    sheets_reset();
    /* Plain integer in a cell */
    str_copy(g_sheet_cells[0][0].raw, "42", sizeof(g_sheet_cells[0][0].raw));
    val = 0; err = 0;
    sheet_eval_cell(0, 0, &val, &err);
    test_assert(err == 0 && val == 42, "work sheet: plain integer cell");

    /* Formula cell */
    str_copy(g_sheet_cells[0][1].raw, "=10+5", sizeof(g_sheet_cells[0][1].raw));
    val = 0; err = 0;
    sheet_eval_cell(0, 1, &val, &err);
    test_assert(err == 0 && val == 15, "work sheet: formula =10+5");

    /* Cell referencing another cell: A2 references A1 which is 42 */
    /* Reset eval state so sheet_eval_cell re-evaluates */
    g_sheet_cells[0][0].eval_state = 0;
    str_copy(g_sheet_cells[1][0].raw, "=A1", sizeof(g_sheet_cells[1][0].raw));
    val = 0; err = 0;
    sheet_eval_cell(1, 0, &val, &err);
    test_assert(err == 0 && val == 42, "work sheet: cell reference =A1");

    /* Empty cell evaluates to 0 without error */
    val = 99; err = 99;
    sheet_eval_cell(5, 5, &val, &err);
    test_assert(err == 0 && val == 0, "work sheet: empty cell = 0");

    /* ---- Slides ---- */
    slides_reset();
    test_assert(g_slide_count == 1, "work slides: reset count");
    test_assert(g_slides[0].elem_count == 0, "work slides: empty slide");

    /* Add a text element directly */
    g_slide_index = 0;
    slide_add_element(SLIDE_ELEM_TEXT);
    test_assert(g_slides[0].elem_count == 1, "work slides: add text element");
    test_assert(g_slides[0].elems[0].type == SLIDE_ELEM_TEXT, "work slides: element type");

    /* Add a rect element */
    slide_add_element(SLIDE_ELEM_RECT);
    test_assert(g_slides[0].elem_count == 2, "work slides: add rect element");
    test_assert(g_slides[0].elems[1].type == SLIDE_ELEM_RECT, "work slides: rect type");
}
