#include "gui/notes_gui.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "lib/string.h"

/* ---- Layout ---- */
#define TOOLBAR_H 34
#define STATUS_H  20
#define PAD        8
#define ROW_H     (FONT_HEIGHT + 3)
#define BTN_W     68
#define BTN_H     22

/* ---- Capacity ---- */
#define NOTES_MAX_LINES  128
#define NOTES_LINE_CAP    80
#define NOTES_TEXT_CAP   (NOTES_MAX_LINES * NOTES_LINE_CAP + 1)

/* ---- Colours ---- */
#define COL_TOOLBAR   gfx_rgb(34,  52,  84)
#define COL_TB_TXT    gfx_rgb(255, 255, 255)
#define COL_BG        gfx_rgb(255, 255, 255)
#define COL_BODY_BG   gfx_rgb(250, 252, 255)
#define COL_TXT       gfx_rgb(30,  40,  60)
#define COL_CURSOR    gfx_rgb(38,  99, 235)
#define COL_STATUS    gfx_rgb(235, 240, 248)
#define COL_ST_TXT    gfx_rgb(70,  90, 120)
#define COL_BTN       gfx_rgb(255, 255, 255)
#define COL_BTN_TXT   gfx_rgb(35,  47,  67)
#define COL_BTN_SAVE  gfx_rgb(38,  99, 235)
#define COL_BTN_STXT  gfx_rgb(255, 255, 255)
#define COL_DIALOG_BG gfx_rgb(28,  34,  50)
#define COL_DIALOG_TXT gfx_rgb(220,228,245)
#define COL_INPUT_BG  gfx_rgb(255, 255, 255)
#define COL_INPUT_TXT gfx_rgb(20,  30,  50)
#define COL_BORDER    gfx_rgb(190, 204, 224)

/* ---- State ---- */
static int  g_win_id     = -1;
static char g_path[128];
static char g_lines[NOTES_MAX_LINES][NOTES_LINE_CAP];
static int  g_line_count;
static int  g_cur_line, g_cur_col;
static int  g_view_top;
static int  g_dirty_flag;
static char g_msg[80];

/* Save-As dialog state */
static int  g_saveas_open;
static char g_saveas_buf[128];
static int  g_saveas_len;

/* Load buffer (static so it's not on the stack) */
static uint8_t g_load_buf[NOTES_TEXT_CAP];
static char    g_save_buf[NOTES_TEXT_CAP];

/* ---- Helpers ---- */
static void set_msg(const char *m) { str_copy(g_msg, m ? m : "", sizeof(g_msg)); }

static void reset_state(void) {
    int i;
    for (i = 0; i < NOTES_MAX_LINES; i++) g_lines[i][0] = '\0';
    g_line_count = 1;
    g_cur_line   = 0;
    g_cur_col    = 0;
    g_view_top   = 0;
    g_dirty_flag = 0;
    g_msg[0]     = '\0';
}

static void clamp_view(int content_rows) {
    if (g_cur_line < g_view_top) g_view_top = g_cur_line;
    if (g_cur_line >= g_view_top + content_rows)
        g_view_top = g_cur_line - content_rows + 1;
    if (g_view_top < 0) g_view_top = 0;
}

/* ---- File I/O ---- */
static void do_load(void) {
    int rc = vfs_cat(g_path, g_load_buf, sizeof(g_load_buf) - 1);
    if (rc <= 0) { set_msg("(new file)"); return; }
    g_load_buf[rc] = '\0';

    int line = 0, col = 0;
    for (int i = 0; i <= rc && line < NOTES_MAX_LINES; i++) {
        char c = (char)g_load_buf[i];
        if (c == '\n' || c == '\0') {
            g_lines[line][col] = '\0';
            line++;
            col = 0;
        } else if (col < NOTES_LINE_CAP - 1) {
            g_lines[line][col++] = c;
        }
    }
    g_line_count = (line > 0) ? line : 1;
    g_dirty_flag = 0;
    set_msg("Loaded.");
}

static void do_save(const char *path) {
    int pos = 0;
    for (int i = 0; i < g_line_count && pos < NOTES_TEXT_CAP - 2; i++) {
        int len = (int)str_len(g_lines[i]);
        if (pos + len + 1 >= NOTES_TEXT_CAP) break;
        str_copy(g_save_buf + pos, g_lines[i], (size_t)(NOTES_TEXT_CAP - pos));
        pos += len;
        g_save_buf[pos++] = '\n';
    }
    g_save_buf[pos] = '\0';
    int rc = vfs_write(path, (uint8_t *)g_save_buf, (uint32_t)pos);
    if (rc == 0) {
        str_copy(g_path, path, sizeof(g_path));
        g_dirty_flag = 0;
        set_msg("Saved.");
    } else {
        set_msg("Save failed. Path must be under /ROOT/");
    }
}

/* ---- Rendering ---- */
static void draw_btn(gui_rect_t cr, int bx, int label_short,
                     const char *label, uint32_t bg, uint32_t fg) {
    (void)label_short;
    int x = cr.x + bx;
    int y = cr.y + (TOOLBAR_H - BTN_H) / 2;
    gfx_fill_rect(x, y, BTN_W, BTN_H, bg);
    gfx_fill_rect(x, y, BTN_W, 1, COL_BORDER);
    gfx_fill_rect(x, y + BTN_H - 1, BTN_W, 1, COL_BORDER);
    gfx_fill_rect(x, y, 1, BTN_H, COL_BORDER);
    gfx_fill_rect(x + BTN_W - 1, y, 1, BTN_H, COL_BORDER);
    int tx = x + (BTN_W - (int)str_len(label) * FONT_WIDTH) / 2;
    gfx_draw_string(tx, y + (BTN_H - FONT_HEIGHT) / 2, label, fg, bg);
}

static void on_paint(int win_id) {
    gui_rect_t cr = gui_window_content(win_id);
    int cw = cr.w, ch = cr.h;

    /* Toolbar */
    gfx_fill_rect(cr.x, cr.y, cw, TOOLBAR_H, COL_TOOLBAR);
    int bx = PAD;
    draw_btn(cr, bx, 0, "New",   COL_BTN,      COL_BTN_TXT);  bx += BTN_W + 6;
    draw_btn(cr, bx, 0, "Open",  COL_BTN,      COL_BTN_TXT);  bx += BTN_W + 6;
    draw_btn(cr, bx, 0, "Save",  COL_BTN,      COL_BTN_TXT);  bx += BTN_W + 6;
    draw_btn(cr, bx, 0, "SaveAs",COL_BTN_SAVE, COL_BTN_STXT);

    /* Status bar */
    int sy = cr.y + ch - STATUS_H;
    gfx_fill_rect(cr.x, sy, cw, STATUS_H, COL_STATUS);
    char statbuf[96];
    const char *base = g_path[0] ? g_path : "(unsaved)";
    if (g_dirty_flag)
        str_copy(statbuf, "* ", sizeof(statbuf));
    else
        statbuf[0] = '\0';
    str_cat(statbuf, base, sizeof(statbuf));
    if (g_msg[0]) { str_cat(statbuf, "  ", sizeof(statbuf)); str_cat(statbuf, g_msg, sizeof(statbuf)); }
    gfx_draw_string(cr.x + PAD, sy + (STATUS_H - FONT_HEIGHT) / 2, statbuf, COL_ST_TXT, COL_STATUS);

    /* Text area */
    int ty = cr.y + TOOLBAR_H;
    int th = ch - TOOLBAR_H - STATUS_H;
    gfx_fill_rect(cr.x, ty, cw, th, COL_BODY_BG);
    int content_rows = th / ROW_H;
    clamp_view(content_rows);

    for (int i = 0; i < content_rows; i++) {
        int ln = g_view_top + i;
        if (ln >= g_line_count) break;
        int ry = ty + i * ROW_H;
        gfx_draw_string(cr.x + PAD, ry, g_lines[ln], COL_TXT, COL_BODY_BG);
        if (ln == g_cur_line) {
            /* cursor */
            int cx = cr.x + PAD + g_cur_col * FONT_WIDTH;
            gfx_fill_rect(cx, ry, 2, FONT_HEIGHT, COL_CURSOR);
        }
    }

    /* Save-As dialog overlay */
    if (g_saveas_open) {
        int dw = cw - 60, dh = 100;
        int dx = cr.x + 30, dy = cr.y + (ch - dh) / 2;
        gfx_fill_rect(dx, dy, dw, dh, COL_DIALOG_BG);
        gfx_fill_rect(dx, dy, dw, 1, COL_BORDER);
        gfx_fill_rect(dx, dy + dh - 1, dw, 1, COL_BORDER);
        gfx_fill_rect(dx, dy, 1, dh, COL_BORDER);
        gfx_fill_rect(dx + dw - 1, dy, 1, dh, COL_BORDER);
        gfx_draw_string(dx + 10, dy + 10, "Save As - enter path:", COL_DIALOG_TXT, COL_DIALOG_BG);
        /* Input field */
        int ix = dx + 10, iy = dy + 36, iw = dw - 20, ih = 24;
        gfx_fill_rect(ix, iy, iw, ih, COL_INPUT_BG);
        gfx_fill_rect(ix, iy, iw, 1, COL_BORDER);
        gfx_fill_rect(ix, iy + ih - 1, iw, 1, COL_BORDER);
        gfx_fill_rect(ix, iy, 1, ih, COL_BORDER);
        gfx_fill_rect(ix + iw - 1, iy, 1, ih, COL_BORDER);
        gfx_draw_string(ix + 4, iy + (ih - FONT_HEIGHT) / 2,
                        g_saveas_buf, COL_INPUT_TXT, COL_INPUT_BG);
        /* Cursor in input */
        int curs_x = ix + 4 + g_saveas_len * FONT_WIDTH;
        gfx_fill_rect(curs_x, iy + 3, 2, ih - 6, COL_CURSOR);
        gfx_draw_string(dx + 10, dy + 68,
                        "Enter to confirm  |  Esc to cancel",
                        COL_DIALOG_TXT, COL_DIALOG_BG);
    }
}

static void on_key(int win_id, char key) {
    (void)win_id;

    /* Save-As dialog captures all keys */
    if (g_saveas_open) {
        if (key == '\r' || key == '\n') {
            if (g_saveas_len > 0) do_save(g_saveas_buf);
            g_saveas_open = 0;
        } else if (key == 0x1B) {   /* Escape */
            g_saveas_open = 0;
        } else if (key == '\b') {
            if (g_saveas_len > 0) g_saveas_buf[--g_saveas_len] = '\0';
        } else if (key >= 0x20 && key < 0x7F &&
                   g_saveas_len < (int)sizeof(g_saveas_buf) - 1) {
            g_saveas_buf[g_saveas_len++] = key;
            g_saveas_buf[g_saveas_len]   = '\0';
        }
        gui_repaint();
        return;
    }

    /* Arrow keys (special codes from keyboard driver) */
    if (key == (char)0x80) {   /* KEY_UP */
        if (g_cur_line > 0) { g_cur_line--; int ll = (int)str_len(g_lines[g_cur_line]); if (g_cur_col > ll) g_cur_col = ll; }
    } else if (key == (char)0x81) {   /* KEY_DOWN */
        if (g_cur_line < g_line_count - 1) { g_cur_line++; int ll = (int)str_len(g_lines[g_cur_line]); if (g_cur_col > ll) g_cur_col = ll; }
    } else if (key == (char)0x82) {   /* KEY_LEFT */
        if (g_cur_col > 0) g_cur_col--;
        else if (g_cur_line > 0) { g_cur_line--; g_cur_col = (int)str_len(g_lines[g_cur_line]); }
    } else if (key == (char)0x83) {   /* KEY_RIGHT */
        int ll = (int)str_len(g_lines[g_cur_line]);
        if (g_cur_col < ll) g_cur_col++;
        else if (g_cur_line < g_line_count - 1) { g_cur_line++; g_cur_col = 0; }
    } else if (key == '\r' || key == '\n') {
        /* Split line at cursor */
        if (g_line_count < NOTES_MAX_LINES) {
            int i;
            for (i = g_line_count; i > g_cur_line + 1; i--)
                str_copy(g_lines[i], g_lines[i-1], NOTES_LINE_CAP);
            char *cur = g_lines[g_cur_line];
            char *nxt = g_lines[g_cur_line + 1];
            str_copy(nxt, cur + g_cur_col, NOTES_LINE_CAP);
            cur[g_cur_col] = '\0';
            g_line_count++;
            g_cur_line++;
            g_cur_col = 0;
        }
        g_dirty_flag = 1;
    } else if (key == '\b') {
        /* Backspace */
        if (g_cur_col > 0) {
            char *cur = g_lines[g_cur_line];
            int ll = (int)str_len(cur);
            int i;
            for (i = g_cur_col - 1; i < ll; i++) cur[i] = cur[i+1];
            g_cur_col--;
            g_dirty_flag = 1;
        } else if (g_cur_line > 0) {
            /* Merge with previous line */
            char *prev = g_lines[g_cur_line - 1];
            char *cur  = g_lines[g_cur_line];
            int plen   = (int)str_len(prev);
            int clen   = (int)str_len(cur);
            if (plen + clen < NOTES_LINE_CAP - 1) {
                str_copy(prev + plen, cur, NOTES_LINE_CAP - plen);
                int i;
                for (i = g_cur_line; i < g_line_count - 1; i++)
                    str_copy(g_lines[i], g_lines[i+1], NOTES_LINE_CAP);
                g_lines[g_line_count - 1][0] = '\0';
                g_line_count--;
                g_cur_line--;
                g_cur_col = plen;
            }
            g_dirty_flag = 1;
        }
    } else if (key >= 0x20 && key < 0x7F) {
        /* Insert character */
        char *cur = g_lines[g_cur_line];
        int ll = (int)str_len(cur);
        if (ll < NOTES_LINE_CAP - 1) {
            int i;
            for (i = ll; i >= g_cur_col; i--) cur[i+1] = cur[i];
            cur[g_cur_col++] = key;
            g_dirty_flag = 1;
        }
    }

    gui_repaint();
}

static void on_close(int win_id) {
    (void)win_id;
    g_win_id     = -1;
    g_saveas_open = 0;
}

static void on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    if (!(buttons & 0x01u)) return;

    gui_rect_t cr = gui_window_content(win_id);

    /* Toolbar buttons (mx, my are content-relative) */
    if (my < TOOLBAR_H) {
        int bx = PAD;
        /* New */
        if (mx >= bx && mx < bx + BTN_W) {
            reset_state();
            g_path[0] = '\0';
            gui_repaint();
            return;
        }
        bx += BTN_W + 6;
        /* Open — just reload current path */
        if (mx >= bx && mx < bx + BTN_W) {
            if (g_path[0]) do_load();
            gui_repaint();
            return;
        }
        bx += BTN_W + 6;
        /* Save */
        if (mx >= bx && mx < bx + BTN_W) {
            if (g_path[0]) do_save(g_path);
            else { g_saveas_open = 1; str_copy(g_saveas_buf, "/ROOT/", sizeof(g_saveas_buf)); g_saveas_len = 6; }
            gui_repaint();
            return;
        }
        bx += BTN_W + 6;
        /* Save As */
        if (mx >= bx && mx < bx + BTN_W) {
            g_saveas_open = 1;
            str_copy(g_saveas_buf, g_path[0] ? g_path : "/ROOT/note.txt", sizeof(g_saveas_buf));
            g_saveas_len = (int)str_len(g_saveas_buf);
            gui_repaint();
            return;
        }
    }

    /* Click in text area → move cursor */
    int th_top = TOOLBAR_H;
    int th_bot = cr.h - STATUS_H;
    if (my >= th_top && my < th_bot) {
        int row = g_view_top + (my - th_top) / ROW_H;
        if (row >= 0 && row < g_line_count) {
            int col = (mx - PAD) / FONT_WIDTH;
            if (col < 0) col = 0;
            int ll = (int)str_len(g_lines[row]);
            if (col > ll) col = ll;
            g_cur_line = row;
            g_cur_col  = col;
            gui_repaint();
        }
    }
}

/* ---- Public ---- */
void notes_gui_launch(void) {
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_rect_t r;
    gui_window_suggest_rect(560, 440, &r);
    g_win_id = gui_window_create("Notes", r.x, r.y, r.w, r.h);
    if (g_win_id < 0) return;

    gui_window_t *w = gui_get_window(g_win_id);
    w->on_paint = on_paint;
    w->on_key   = on_key;
    w->on_close = on_close;
    w->on_mouse = on_mouse;

    reset_state();
    str_copy(g_path, "/ROOT/note.txt", sizeof(g_path));
    g_saveas_open = 0;

    gui_repaint();
}
