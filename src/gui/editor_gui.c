#include "gui/editor_gui.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "gui/axdocs_gui.h"
#include "gui/gui.h"
#include "lang/lang.h"
#include "lib/string.h"

#define EDITOR_MAX_LINES   256
#define EDITOR_LINE_CAP    161
#define EDITOR_TEXT_CAP    (EDITOR_MAX_LINES * EDITOR_LINE_CAP + 1)

#define TOOLBAR_H          34
#define STATUS_H           22
#define PANEL_PAD          10
#define GUTTER_W           44
#define ROW_H              (FONT_HEIGHT + 2)
#define BUTTON_H           22

#define COL_BG             gfx_rgb(242, 246, 252)
#define COL_TOOLBAR        gfx_rgb(34, 52, 84)
#define COL_TOOLBAR_TXT    gfx_rgb(255, 255, 255)
#define COL_BORDER         gfx_rgb(183, 196, 218)
#define COL_BODY           gfx_rgb(255, 255, 255)
#define COL_GUTTER         gfx_rgb(240, 244, 250)
#define COL_TEXT           gfx_rgb(33, 45, 66)
#define COL_TEXT_DIM       gfx_rgb(117, 128, 146)
#define COL_LINE_ACTIVE    gfx_rgb(243, 247, 255)
#define COL_STATUS         gfx_rgb(231, 238, 247)
#define COL_STATUS_TXT     gfx_rgb(58, 74, 101)
#define COL_BUTTON         gfx_rgb(255, 255, 255)
#define COL_BUTTON_TXT     gfx_rgb(35, 47, 67)
#define COL_CURSOR         gfx_rgb(38, 99, 235)

enum {
    EDITOR_BTN_NEW  = 0,
    EDITOR_BTN_OPEN = 1,
    EDITOR_BTN_SAVE = 2,
    EDITOR_BTN_RUN  = 3,
    EDITOR_BTN_DOCS = 4,
    EDITOR_BTN_COUNT = 5,
};

/* Open-file overlay state */
static int  g_open_overlay = 0;
static char g_open_buf[128];
static int  g_open_len = 0;

/* ---- Language mode ---- */
typedef enum { LANG_NONE = 0, LANG_AX } editor_lang_t;
static editor_lang_t g_lang_mode = LANG_NONE;

/* Syntax highlight colors */
#define COL_SYN_KW    gfx_rgb(100, 149, 237)
#define COL_SYN_STR   gfx_rgb(106, 190,  90)
#define COL_SYN_NUM   gfx_rgb( 80, 200, 220)
#define COL_SYN_CMT   gfx_rgb(120, 130, 150)

static int g_win_id = -1;
static char g_file_path[128];
static char g_lines[EDITOR_MAX_LINES][EDITOR_LINE_CAP];
static uint16_t g_lens[EDITOR_MAX_LINES];
static uint8_t g_load_buf[EDITOR_TEXT_CAP];
static char g_save_buf[EDITOR_TEXT_CAP];
static int g_line_count = 1;
static int g_cursor_line = 0;
static int g_cursor_col = 0;
static int g_view_top = 0;
static int g_view_left = 0;
static int g_dirty = 0;
static int g_insert_mode = 1;
static int g_quit_armed = 0;
static char g_message[96];

static const char *editor_default_path(void) {
    return "/ROOT/HELLO.AX";
}

static int editor_path_is_workspace_target(const char *path) {
    if (!path || !path[0]) return 0;
    if (str_eq(path, "/ROOT")) return 1;
    return str_ncmp(path, "/ROOT", 5) == 0 && path[5] == '/';
}

static void editor_set_message(const char *msg) {
    str_copy(g_message, msg ? msg : "", sizeof(g_message));
}

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

static void editor_basename(const char *path, char *out, size_t out_size) {
    int len;
    int start;

    if (!path || !path[0]) {
        str_copy(out, "UNTITLED.TXT", out_size);
        return;
    }

    len = (int)str_len(path);
    start = len;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    str_copy(out, path + start, out_size);
}

static void editor_update_title(void) {
    char base[32];
    char title[48];

    if (g_win_id < 0 || !gui_window_active(g_win_id)) {
        return;
    }

    editor_basename(g_file_path, base, sizeof(base));
    title[0] = '\0';
    str_copy(title, "AX Code - ", sizeof(title));
    str_cat(title, base, sizeof(title));
    gui_window_set_title(g_win_id, title);
}

static void editor_clear_buffer(void) {
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        g_lines[i][0] = '\0';
        g_lens[i] = 0;
    }

    g_line_count = 1;
    g_cursor_line = 0;
    g_cursor_col = 0;
    g_view_top = 0;
    g_view_left = 0;
    g_dirty = 0;
    g_insert_mode = 1;
    g_quit_armed = 0;
    g_message[0] = '\0';
}

static void editor_append_line_raw(const char *src, int len) {
    if (g_line_count >= EDITOR_MAX_LINES) return;
    if (len < 0) len = 0;
    if (len >= EDITOR_LINE_CAP) len = EDITOR_LINE_CAP - 1;

    for (int i = 0; i < len; i++) {
        g_lines[g_line_count][i] = src[i];
    }
    g_lines[g_line_count][len] = '\0';
    g_lens[g_line_count] = (uint16_t)len;
    g_line_count++;
}

static int editor_visible_rows(void) {
    gui_rect_t r = gui_window_content(g_win_id);
    int rows = (r.h - TOOLBAR_H - STATUS_H - PANEL_PAD * 2) / ROW_H;

    if (rows < 1) rows = 1;
    return rows;
}

static int editor_visible_cols(void) {
    gui_rect_t r = gui_window_content(g_win_id);
    int cols = (r.w - PANEL_PAD * 2 - GUTTER_W - 12) / FONT_WIDTH;

    if (cols < 1) cols = 1;
    return cols;
}

static void editor_clamp_cursor(void) {
    if (g_line_count < 1) g_line_count = 1;
    if (g_cursor_line < 0) g_cursor_line = 0;
    if (g_cursor_line >= g_line_count) g_cursor_line = g_line_count - 1;
    if (g_cursor_col < 0) g_cursor_col = 0;
    if (g_cursor_col > (int)g_lens[g_cursor_line]) {
        g_cursor_col = (int)g_lens[g_cursor_line];
    }
}

static void editor_ensure_visible(void) {
    int rows = editor_visible_rows();
    int cols = editor_visible_cols();

    editor_clamp_cursor();
    if (g_cursor_line < g_view_top) g_view_top = g_cursor_line;
    if (g_cursor_line >= g_view_top + rows) g_view_top = g_cursor_line - rows + 1;
    if (g_cursor_col < g_view_left) g_view_left = g_cursor_col;
    if (g_cursor_col >= g_view_left + cols) g_view_left = g_cursor_col - cols + 1;
    if (g_view_top < 0) g_view_top = 0;
    if (g_view_left < 0) g_view_left = 0;
}

static void editor_load_file(const char *path) {
    int read;

    editor_clear_buffer();
    if (!path || !path[0]) path = editor_default_path();
    str_copy(g_file_path, path, sizeof(g_file_path));

    /* AX Code is always Ax mode */
    g_lang_mode = LANG_AX;
    g_line_count = 0;

    if (!vfs_available()) {
        editor_set_message("filesystem unavailable");
        g_line_count = 1;
        return;
    }

    read = vfs_cat(path, g_load_buf, (int)sizeof(g_load_buf) - 1);
    if (read < 0) {
        editor_set_message("New file");
        g_line_count = 1;
        g_lines[0][0] = '\0';
        g_lens[0] = 0;
        editor_update_title();
        return;
    }

    if (read == 0) {
        g_line_count = 1;
        g_lines[0][0] = '\0';
        g_lens[0] = 0;
        editor_set_message("Loaded");
        editor_update_title();
        return;
    }

    g_load_buf[read] = '\0';
    {
        int start = 0;

        for (int i = 0; i <= read; i++) {
            int len;

            if (i < read && g_load_buf[i] != '\n') continue;
            len = i - start;
            while (len > 0 && g_load_buf[start + len - 1] == '\r') len--;
            editor_append_line_raw((const char *)&g_load_buf[start], len);
            start = i + 1;
            if (g_line_count >= EDITOR_MAX_LINES) break;
        }
    }

    if (g_line_count == 0) {
        g_line_count = 1;
        g_lines[0][0] = '\0';
        g_lens[0] = 0;
    }

    editor_set_message("Loaded");
    g_dirty = 0;
    editor_update_title();
}

static int editor_save_file(void) {
    int out = 0;

    for (int i = 0; i < g_line_count; i++) {
        int len = (int)g_lens[i];

        if (out + len >= (int)sizeof(g_save_buf)) {
            editor_set_message("file too large");
            return 0;
        }

        for (int j = 0; j < len; j++) {
            g_save_buf[out++] = g_lines[i][j];
        }

        if (i + 1 < g_line_count) {
            if (out + 1 >= (int)sizeof(g_save_buf)) {
                editor_set_message("file too large");
                return 0;
            }
            g_save_buf[out++] = '\n';
        }
    }

    {
        int written = vfs_write(g_file_path, (const uint8_t *)g_save_buf, (uint32_t)out);
        if (written < 0) {
            if (!editor_path_is_workspace_target(g_file_path)) {
                editor_set_message("protected system location");
            } else {
                editor_set_message("save failed");
            }
            return 0;
        }
    }

    g_dirty = 0;
    g_quit_armed = 0;
    editor_set_message("Saved");
    editor_update_title();
    return 1;
}

static void split_current_line(void) {
    int line;
    int col;
    int len;
    char right[EDITOR_LINE_CAP];
    int right_len;

    if (g_line_count >= EDITOR_MAX_LINES) {
        editor_set_message("line limit reached");
        return;
    }

    line = g_cursor_line;
    col = g_cursor_col;
    len = (int)g_lens[line];
    if (col < 0) col = 0;
    if (col > len) col = len;

    right_len = len - col;
    if (right_len < 0) right_len = 0;
    if (right_len >= EDITOR_LINE_CAP) right_len = EDITOR_LINE_CAP - 1;

    for (int i = 0; i < right_len; i++) {
        right[i] = g_lines[line][col + i];
    }
    right[right_len] = '\0';

    g_lines[line][col] = '\0';
    g_lens[line] = (uint16_t)col;

    for (int i = g_line_count; i > line + 1; i--) {
        str_copy(g_lines[i], g_lines[i - 1], EDITOR_LINE_CAP);
        g_lens[i] = g_lens[i - 1];
    }

    str_copy(g_lines[line + 1], right, EDITOR_LINE_CAP);
    g_lens[line + 1] = (uint16_t)right_len;
    g_line_count++;
    g_cursor_line++;
    g_cursor_col = 0;
    g_dirty = 1;
    g_quit_armed = 0;

    /* Auto-indent for Ax files */
    if (g_lang_mode == LANG_AX) {
        const char *prev = g_lines[line];
        int prev_len = (int)g_lens[line];
        int indent = 0;
        int last;
        int i;
        while (prev[indent] == ' ' || prev[indent] == '\t') indent++;
        last = prev_len - 1;
        while (last >= 0 && (prev[last] == ' ' || prev[last] == '\t')) last--;
        if (last >= 0 && prev[last] == '{') indent += 4;
        if (indent > 0 && right_len + indent < EDITOR_LINE_CAP) {
            for (i = right_len; i >= 0; i--)
                g_lines[line + 1][i + indent] = g_lines[line + 1][i];
            for (i = 0; i < indent; i++) g_lines[line + 1][i] = ' ';
            g_lens[line + 1] = (uint16_t)(right_len + indent);
            g_cursor_col = indent;
        }
    }
}

static void merge_with_previous_line(void) {
    int current;
    int previous;
    int prev_len;
    int cur_len;
    int room;

    if (g_cursor_line <= 0) return;

    current = g_cursor_line;
    previous = current - 1;
    prev_len = (int)g_lens[previous];
    cur_len = (int)g_lens[current];
    room = EDITOR_LINE_CAP - 1 - prev_len;
    if (room < 0) room = 0;
    if (cur_len > room) cur_len = room;

    for (int i = 0; i < cur_len; i++) {
        g_lines[previous][prev_len + i] = g_lines[current][i];
    }
    g_lines[previous][prev_len + cur_len] = '\0';
    g_lens[previous] = (uint16_t)(prev_len + cur_len);

    for (int i = current; i < g_line_count - 1; i++) {
        str_copy(g_lines[i], g_lines[i + 1], EDITOR_LINE_CAP);
        g_lens[i] = g_lens[i + 1];
    }

    g_line_count--;
    if (g_line_count < 1) {
        g_line_count = 1;
        g_lines[0][0] = '\0';
        g_lens[0] = 0;
    }

    g_cursor_line = previous;
    g_cursor_col = prev_len;
    g_dirty = 1;
    g_quit_armed = 0;
}

static void delete_at_cursor(void) {
    int line = g_cursor_line;
    int col = g_cursor_col;
    int len = (int)g_lens[line];

    if (col < len) {
        for (int i = col; i < len; i++) {
            g_lines[line][i] = g_lines[line][i + 1];
        }
        g_lens[line] = (uint16_t)(len - 1);
        g_dirty = 1;
        g_quit_armed = 0;
        return;
    }

    if (line + 1 < g_line_count) {
        int next_len = (int)g_lens[line + 1];
        int room = EDITOR_LINE_CAP - 1 - len;

        if (room < 0) room = 0;
        if (next_len > room) next_len = room;

        for (int i = 0; i < next_len; i++) {
            g_lines[line][len + i] = g_lines[line + 1][i];
        }
        g_lines[line][len + next_len] = '\0';
        g_lens[line] = (uint16_t)(len + next_len);

        for (int i = line + 1; i < g_line_count - 1; i++) {
            str_copy(g_lines[i], g_lines[i + 1], EDITOR_LINE_CAP);
            g_lens[i] = g_lens[i + 1];
        }
        g_line_count--;
        g_dirty = 1;
        g_quit_armed = 0;
    }
}

static void backspace_at_cursor(void) {
    if (g_cursor_col > 0) {
        int line = g_cursor_line;
        int col = g_cursor_col;
        int len = (int)g_lens[line];

        for (int i = col - 1; i < len; i++) {
            g_lines[line][i] = g_lines[line][i + 1];
        }
        g_lens[line] = (uint16_t)(len - 1);
        g_cursor_col--;
        g_dirty = 1;
        g_quit_armed = 0;
        return;
    }

    merge_with_previous_line();
}

static void insert_char(char c) {
    int line = g_cursor_line;
    int col = g_cursor_col;
    int len = (int)g_lens[line];

    if (g_insert_mode) {
        if (len >= EDITOR_LINE_CAP - 1) {
            editor_set_message("line full");
            return;
        }
        for (int i = len; i > col; i--) {
            g_lines[line][i] = g_lines[line][i - 1];
        }
        g_lines[line][col] = c;
        g_lines[line][len + 1] = '\0';
        g_lens[line] = (uint16_t)(len + 1);
    } else {
        if (col < len) {
            g_lines[line][col] = c;
        } else {
            if (len >= EDITOR_LINE_CAP - 1) {
                editor_set_message("line full");
                return;
            }
            g_lines[line][col] = c;
            g_lines[line][col + 1] = '\0';
            g_lens[line] = (uint16_t)(len + 1);
        }
    }

    g_cursor_col++;
    g_dirty = 1;
    g_quit_armed = 0;
}

static void move_left(void) {
    if (g_cursor_col > 0) {
        g_cursor_col--;
        return;
    }
    if (g_cursor_line > 0) {
        g_cursor_line--;
        g_cursor_col = (int)g_lens[g_cursor_line];
    }
}

static void move_right(void) {
    if (g_cursor_col < (int)g_lens[g_cursor_line]) {
        g_cursor_col++;
        return;
    }
    if (g_cursor_line + 1 < g_line_count) {
        g_cursor_line++;
        g_cursor_col = 0;
    }
}

static void move_up(void) {
    if (g_cursor_line > 0) {
        g_cursor_line--;
        if (g_cursor_col > (int)g_lens[g_cursor_line]) {
            g_cursor_col = (int)g_lens[g_cursor_line];
        }
    }
}

static void move_down(void) {
    if (g_cursor_line + 1 < g_line_count) {
        g_cursor_line++;
        if (g_cursor_col > (int)g_lens[g_cursor_line]) {
            g_cursor_col = (int)g_lens[g_cursor_line];
        }
    }
}

static void page_up(void) {
    g_cursor_line -= editor_visible_rows();
    if (g_cursor_line < 0) g_cursor_line = 0;
    if (g_cursor_col > (int)g_lens[g_cursor_line]) {
        g_cursor_col = (int)g_lens[g_cursor_line];
    }
}

static void page_down(void) {
    g_cursor_line += editor_visible_rows();
    if (g_cursor_line >= g_line_count) g_cursor_line = g_line_count - 1;
    if (g_cursor_col > (int)g_lens[g_cursor_line]) {
        g_cursor_col = (int)g_lens[g_cursor_line];
    }
}

static void editor_button_rect(int button, int *x, int *y, int *w, int *h) {
    static const int bx[] = { 8, 58, 108, 166, 224 };
    static const int bw[] = { 46, 46, 54,  54,  50  };
    *y = 6;
    *h = BUTTON_H;
    if (button < 0 || button >= EDITOR_BTN_COUNT) { *x = 0; *w = 0; return; }
    *x = bx[button];
    *w = bw[button];
}

static void editor_draw_button(int px, int py, int w, int h, const char *label) {
    int text_x = px + (w - (int)str_len(label) * FONT_WIDTH) / 2;
    int text_y = py + (h - FONT_HEIGHT) / 2;

    gfx_fill_rect(px, py, w, h, COL_BORDER);
    gfx_fill_rect(px + 1, py + 1, w - 2, h - 2, COL_BUTTON);
    gfx_draw_string(text_x, text_y, label, COL_BUTTON_TXT, COL_BUTTON);
}

/* ---- Syntax highlighting ---- */
static int syn_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int syn_is_ident_char(char c) {
    return syn_is_ident_start(c) || (c >= '0' && c <= '9');
}
static int syn_is_keyword(const char *s, int len) {
    static const char *kws[] = {
        "let","fn","if","else","while","return",
        "print","sys","input","true","false", 0
    };
    int i;
    for (i = 0; kws[i]; i++) {
        int kl = 0;
        while (kws[i][kl]) kl++;
        if (kl == len && str_ncmp(s, kws[i], (size_t)len) == 0) return 1;
    }
    return 0;
}

/* Draw one editor line with Ax syntax highlighting.
 * base_x/y: pixel top-left of the text area for this line.
 * line_idx:  index into g_lines[].
 * bg:        background color.
 * view_left: horizontal scroll offset (chars).
 * visible_len: number of visible characters. */
static void draw_line_highlighted(int base_x, int y, int line_idx,
                                   uint32_t bg, int view_left, int visible_len) {
    const char *line = g_lines[line_idx];
    int len = (int)g_lens[line_idx];
    /* Color codes: 0=text, 1=kw, 2=str, 3=num, 4=cmt */
    uint8_t colors[EDITOR_LINE_CAP];
    int i;

    for (i = 0; i < len && i < EDITOR_LINE_CAP; i++) colors[i] = 0;

    i = 0;
    {
        int in_str = 0, in_cmt = 0;
        while (i < len) {
            if (in_cmt) {
                colors[i++] = 4;
            } else if (in_str) {
                colors[i] = 2;
                if (line[i] == '"') in_str = 0;
                i++;
            } else if (line[i] == '/' && i+1 < len && line[i+1] == '/') {
                in_cmt = 1;
                colors[i++] = 4;
            } else if (line[i] == '"') {
                in_str = 1;
                colors[i++] = 2;
            } else if (line[i] >= '0' && line[i] <= '9') {
                colors[i++] = 3;
            } else if (syn_is_ident_start(line[i])) {
                int start = i;
                while (i < len && syn_is_ident_char(line[i])) i++;
                {
                    uint8_t c = syn_is_keyword(line + start, i - start) ? 1 : 0;
                    int j;
                    for (j = start; j < i; j++) colors[j] = c;
                }
            } else {
                i++;
            }
        }
    }

    /* Draw visible portion */
    {
        int end = view_left + visible_len;
        if (end > len) end = len;
        for (i = view_left; i < end; i++) {
            uint32_t col;
            char ch[2];
            switch (colors[i]) {
                case 1: col = COL_SYN_KW;  break;
                case 2: col = COL_SYN_STR; break;
                case 3: col = COL_SYN_NUM; break;
                case 4: col = COL_SYN_CMT; break;
                default: col = COL_TEXT;   break;
            }
            ch[0] = line[i]; ch[1] = '\0';
            gfx_draw_string(base_x + (i - view_left) * FONT_WIDTH, y, ch, col, bg);
        }
    }
}

static void editor_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int body_y = r.y + TOOLBAR_H;
    int body_h = r.h - TOOLBAR_H - STATUS_H;
    int rows = editor_visible_rows();
    int cols = editor_visible_cols();
    char path_line[64];
    char status[128];
    char line_no[8];

    editor_ensure_visible();

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    gfx_fill_rect(r.x, r.y, r.w, TOOLBAR_H, COL_TOOLBAR);
    gfx_fill_rect(r.x, body_y, r.w, body_h, COL_BODY);
    gfx_fill_rect(r.x, r.y + r.h - STATUS_H, r.w, STATUS_H, COL_STATUS);
    gfx_fill_rect(r.x, r.y + TOOLBAR_H - 1, r.w, 1, COL_BORDER);
    gfx_fill_rect(r.x, r.y + r.h - STATUS_H, r.w, 1, COL_BORDER);

    {
        static const char *btn_labels[] = { "New", "Open", "Save", "Run", "Docs" };
        int bx, by, bw, bh;
        for (int b = 0; b < EDITOR_BTN_COUNT; b++) {
            editor_button_rect(b, &bx, &by, &bw, &bh);
            editor_draw_button(r.x + bx, r.y + by, bw, bh, btn_labels[b]);
        }
    }

    clip_text(path_line, sizeof(path_line), g_file_path, (r.w - 286) / FONT_WIDTH);
    gfx_draw_string(r.x + 280, r.y + 9, path_line, COL_TOOLBAR_TXT, COL_TOOLBAR);

    gfx_fill_rect(r.x + PANEL_PAD, body_y + PANEL_PAD, GUTTER_W,
                  body_h - PANEL_PAD * 2, COL_GUTTER);

    for (int vis = 0; vis < rows; vis++) {
        int line = g_view_top + vis;
        int row_y = body_y + PANEL_PAD + vis * ROW_H;
        uint32_t bg = (line == g_cursor_line) ? COL_LINE_ACTIVE : COL_BODY;

        if (row_y + ROW_H > body_y + body_h - PANEL_PAD) break;

        gfx_fill_rect(r.x + PANEL_PAD + GUTTER_W, row_y,
                      r.w - PANEL_PAD * 2 - GUTTER_W, ROW_H, bg);

        if (line < g_line_count) {
            char visible[EDITOR_LINE_CAP];
            int len = (int)g_lens[line];
            int start = g_view_left;
            int out = 0;

            if (start > len) start = len;
            while (start + out < len && out < cols && out + 1 < (int)sizeof(visible)) {
                visible[out] = g_lines[line][start + out];
                out++;
            }
            visible[out] = '\0';

            u32_to_dec((uint32_t)(line + 1), line_no, sizeof(line_no));
            gfx_draw_string(r.x + PANEL_PAD + 8, row_y + 1, line_no, COL_TEXT_DIM, COL_GUTTER);
            if (g_lang_mode == LANG_AX) {
                draw_line_highlighted(r.x + PANEL_PAD + GUTTER_W + 6, row_y + 1,
                                      line, bg, g_view_left, cols);
            } else {
                gfx_draw_string(r.x + PANEL_PAD + GUTTER_W + 6, row_y + 1, visible, COL_TEXT, bg);
            }
        }
    }

    if (g_cursor_line >= g_view_top && g_cursor_line < g_view_top + rows) {
        int cursor_row_y = body_y + PANEL_PAD + (g_cursor_line - g_view_top) * ROW_H + 1;
        int cursor_x = r.x + PANEL_PAD + GUTTER_W + 6 +
                       (g_cursor_col - g_view_left) * FONT_WIDTH;

        if (cursor_x < r.x + r.w - PANEL_PAD - 2) {
            gfx_fill_rect(cursor_x, cursor_row_y, 2, FONT_HEIGHT, COL_CURSOR);
        }
    }

    {
        char ln_buf[10], col_buf[10];
        u32_to_dec((uint32_t)(g_cursor_line + 1), ln_buf,  sizeof(ln_buf));
        u32_to_dec((uint32_t)(g_cursor_col + 1),  col_buf, sizeof(col_buf));
        status[0] = '\0';
        str_copy(status, "Ln ", sizeof(status));
        str_cat(status, ln_buf, sizeof(status));
        str_cat(status, ", Col ", sizeof(status));
        str_cat(status, col_buf, sizeof(status));
        str_cat(status, " | ", sizeof(status));
    }
    if (g_message[0]) {
        str_cat(status, g_message, sizeof(status));
        str_cat(status, " | ", sizeof(status));
    }
    str_cat(status, g_insert_mode ? "INS" : "OVR", sizeof(status));
    str_cat(status, g_dirty ? " *" : " saved", sizeof(status));
    str_cat(status, " | Ctrl+S | Ctrl+Q", sizeof(status));
    gfx_draw_string(r.x + 10, r.y + r.h - STATUS_H + 4, status, COL_STATUS_TXT, COL_STATUS);

    /* Open file overlay */
    if (g_open_overlay) {
        int ox = r.x + 40, oy = r.y + TOOLBAR_H + 20, ow = r.w - 80, oh = 50;
        gfx_fill_rect(ox - 2, oy - 2, ow + 4, oh + 4, COL_BORDER);
        gfx_fill_rect(ox, oy, ow, oh, gfx_rgb(250, 252, 255));
        gfx_draw_string(ox + 8, oy + 6, "Open file path:", COL_TEXT, gfx_rgb(250,252,255));
        gfx_fill_rect(ox + 8, oy + 22, ow - 16, 20, gfx_rgb(255,255,255));
        gfx_draw_string(ox + 12, oy + 26, g_open_buf, COL_TEXT, gfx_rgb(255,255,255));
        /* cursor in input */
        gfx_fill_rect(ox + 12 + g_open_len * FONT_WIDTH, oy + 26, 2, FONT_HEIGHT, COL_CURSOR);
    }
}

static void editor_run_file(void) {
    if (!editor_save_file()) return;
    lang_run_file(g_file_path);
    editor_set_message("Ran. Output -> Terminal");
}

static void editor_on_key(int win_id, char key) {
    (void)win_id;

    /* Open overlay captures all keys */
    if (g_open_overlay) {
        if (key == 0x1B) {
            g_open_overlay = 0;
        } else if (key == '\r' || key == '\n') {
            if (g_open_len > 0) {
                g_open_overlay = 0;
                editor_load_file(g_open_buf);
                editor_update_title();
            }
        } else if ((key == '\b' || key == (char)0x7F) && g_open_len > 0) {
            g_open_buf[--g_open_len] = '\0';
        } else if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                   g_open_len < (int)(sizeof(g_open_buf) - 1)) {
            g_open_buf[g_open_len++] = key;
            g_open_buf[g_open_len]   = '\0';
        }
        return;
    }

    if (key != 0x11) g_quit_armed = 0;

    switch (key) {
        case KEY_LEFT: move_left(); break;
        case KEY_RIGHT: move_right(); break;
        case KEY_UP: move_up(); break;
        case KEY_DOWN: move_down(); break;
        case KEY_HOME: g_cursor_col = 0; break;
        case KEY_END: g_cursor_col = (int)g_lens[g_cursor_line]; break;
        case KEY_CTRL_HOME:
            g_cursor_line = 0;
            g_cursor_col = 0;
            break;
        case KEY_CTRL_END:
            g_cursor_line = g_line_count - 1;
            g_cursor_col = (int)g_lens[g_cursor_line];
            break;
        case KEY_PAGEUP: page_up(); break;
        case KEY_PAGEDOWN: page_down(); break;
        case KEY_DELETE: delete_at_cursor(); break;
        case KEY_INSERT:
            g_insert_mode = !g_insert_mode;
            editor_set_message(g_insert_mode ? "insert mode" : "overwrite mode");
            break;
        case '\b':
        case (char)0x7F:
            backspace_at_cursor();
            break;
        case '\t':
            if (g_lang_mode == LANG_AX) {
                insert_char(' '); insert_char(' ');
                insert_char(' '); insert_char(' ');
            }
            break;
        case '\r':
        case '\n':
            split_current_line();
            break;
        case 0x13:
            editor_save_file();
            break;
        case 0x11:
            if (g_dirty && !g_quit_armed) {
                g_quit_armed = 1;
                editor_set_message("unsaved changes: press Ctrl+Q again");
                break;
            }
            gui_window_close(g_win_id);
            return;
        default:
            if ((unsigned char)key >= 32 && (unsigned char)key <= 126) {
                insert_char(key);
            }
            break;
    }

    editor_ensure_visible();
}

static void editor_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    gui_rect_t r;
    int bx;
    int by;
    int bw;
    int bh;
    int row;
    int col;

    (void)win_id;
    if (!(buttons & 1)) return;

    editor_button_rect(EDITOR_BTN_NEW, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        editor_clear_buffer();
        str_copy(g_file_path, editor_default_path(), sizeof(g_file_path));
        g_lang_mode = LANG_AX;
        editor_update_title();
        return;
    }

    editor_button_rect(EDITOR_BTN_OPEN, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        g_open_buf[0] = '\0';
        g_open_len    = 0;
        g_open_overlay = 1;
        return;
    }

    editor_button_rect(EDITOR_BTN_SAVE, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        editor_save_file();
        return;
    }

    editor_button_rect(EDITOR_BTN_RUN, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        editor_run_file();
        return;
    }

    editor_button_rect(EDITOR_BTN_DOCS, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        axdocs_gui_launch();
        return;
    }

    r = gui_window_content(g_win_id);
    if (x < PANEL_PAD + GUTTER_W + 2 || y < TOOLBAR_H + PANEL_PAD ||
        y >= r.h - STATUS_H - PANEL_PAD) {
        return;
    }

    row = (y - (TOOLBAR_H + PANEL_PAD)) / ROW_H;
    col = (x - (PANEL_PAD + GUTTER_W + 6)) / FONT_WIDTH;
    if (row < 0) row = 0;
    if (col < 0) col = 0;

    g_cursor_line = g_view_top + row;
    if (g_cursor_line >= g_line_count) g_cursor_line = g_line_count - 1;
    g_cursor_col = g_view_left + col;
    if (g_cursor_col > (int)g_lens[g_cursor_line]) {
        g_cursor_col = (int)g_lens[g_cursor_line];
    }
    editor_ensure_visible();
}

static void editor_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void editor_gui_open(const char *path) {
    gui_rect_t rect;
    gui_window_t *w;
    const char *target = (path && path[0]) ? path : editor_default_path();

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        if (!str_eq(g_file_path, target)) {
            editor_load_file(target);
        }
        editor_update_title();
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(780, 500, &rect);
    g_win_id = gui_window_create("AX Code", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;

    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_EDITOR;
    w->on_paint = editor_on_paint;
    w->on_key = editor_on_key;
    w->on_mouse = editor_on_mouse;
    w->on_close = editor_on_close;

    editor_load_file(target);
    editor_update_title();
}

void editor_gui_launch(void) {
    editor_gui_open(editor_default_path());
}
