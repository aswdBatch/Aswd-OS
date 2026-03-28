#include "gui/axdocs_gui.h"

#include <stdint.h>
#include <stddef.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "lib/string.h"

#define DOCS_WIN_W  620
#define DOCS_WIN_H  460
#define DOCS_PAD     10
#define DOCS_ROW_H  (FONT_HEIGHT + 3)

#define COL_DOCS_BG     gfx_rgb(242, 246, 252)
#define COL_DOCS_HDR    gfx_rgb(27, 104, 188)
#define COL_DOCS_HDR_TXT gfx_rgb(255, 255, 255)
#define COL_DOCS_TXT    gfx_rgb(33, 45, 66)
#define COL_DOCS_DIM    gfx_rgb(100, 115, 140)
#define COL_DOCS_RULE   gfx_rgb(200, 210, 228)
#define COL_SCROLL_BG   gfx_rgb(220, 228, 240)
#define COL_SCROLL_FG   gfx_rgb(100, 130, 180)

typedef enum { DOC_HDR = 0, DOC_TXT, DOC_BLANK } doc_line_type_t;

typedef struct {
    doc_line_type_t type;
    const char *text;
} doc_line_t;

static const doc_line_t k_docs[] = {
    { DOC_HDR,   "Quick Start" },
    { DOC_TXT,   "Save a file with .ax extension (e.g. /ROOT/HELLO.AX)" },
    { DOC_TXT,   "Run it from the terminal with: ax /ROOT/HELLO.AX" },
    { DOC_TXT,   "AX Studio stays separate for visual app building." },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Variables & Types" },
    { DOC_TXT,   "let x = 42          // integer" },
    { DOC_TXT,   "let s = \"hello\"    // string" },
    { DOC_TXT,   "let b = true        // boolean: true / false" },
    { DOC_TXT,   "let n = nil         // nil (no value)" },
    { DOC_TXT,   "Variables are dynamically typed." },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Operators" },
    { DOC_TXT,   "Arithmetic: + - * /  (integer)" },
    { DOC_TXT,   "Compare:    == != < > <= >=" },
    { DOC_TXT,   "Logic:      && ||  (short-circuit)" },
    { DOC_TXT,   "Unary:      -  !   (negate, not)" },
    { DOC_TXT,   "String concat: \"hi\" + \" world\"" },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Control Flow" },
    { DOC_TXT,   "if cond { ... }" },
    { DOC_TXT,   "if cond { ... } else { ... }" },
    { DOC_TXT,   "while cond { ... }    // max 1,000,000 iterations" },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Functions" },
    { DOC_TXT,   "fn greet(name) {" },
    { DOC_TXT,   "    print \"Hello \" + name" },
    { DOC_TXT,   "    return 0" },
    { DOC_TXT,   "}" },
    { DOC_TXT,   "greet(\"world\")" },
    { DOC_TXT,   "Functions are declared with fn, called by name." },
    { DOC_TXT,   "Max 32 functions. Max call depth: 32." },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Built-ins" },
    { DOC_TXT,   "print expr         -- print value followed by newline" },
    { DOC_TXT,   "let s = input()    -- read one line from keyboard" },
    { DOC_TXT,   "sys \"cmd arg\"     -- run shell command, e.g. sys \"ls\"" },
    { DOC_BLANK, "" },
    { DOC_HDR,   "Limits" },
    { DOC_TXT,   "Max lines per file: 256.  Max line length: 160 chars." },
    { DOC_TXT,   "Max 128 variables per scope.  Max 4096 tokens." },
    { DOC_TXT,   "String pool: 16 KB.  AST nodes: 2048." },
};

#define DOCS_LINE_COUNT ((int)(sizeof(k_docs) / sizeof(k_docs[0])))

static int g_win_id = -1;
static int g_scroll  = 0;

static int docs_visible_rows(void) {
    gui_rect_t r = gui_window_content(g_win_id);
    int rows = (r.h - DOCS_PAD * 2) / DOCS_ROW_H;
    if (rows < 1) rows = 1;
    return rows;
}

static void docs_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int rows    = docs_visible_rows();
    int max_scroll = DOCS_LINE_COUNT - rows;
    int bar_h, bar_y;

    if (max_scroll < 0) max_scroll = 0;
    if (g_scroll > max_scroll) g_scroll = max_scroll;
    if (g_scroll < 0) g_scroll = 0;

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_DOCS_BG);

    /* scrollbar */
    {
        int sx = r.x + r.w - 8;
        gfx_fill_rect(sx, r.y, 8, r.h, COL_SCROLL_BG);
        bar_h = (DOCS_LINE_COUNT > 0) ? (r.h * rows / DOCS_LINE_COUNT) : r.h;
        if (bar_h < 12) bar_h = 12;
        if (bar_h > r.h) bar_h = r.h;
        bar_y = (DOCS_LINE_COUNT > 0) ? (r.h * g_scroll / DOCS_LINE_COUNT) : 0;
        gfx_fill_rect(sx + 1, r.y + bar_y, 6, bar_h, COL_SCROLL_FG);
    }

    for (int i = 0; i < rows; i++) {
        int li  = g_scroll + i;
        int row_y = r.y + DOCS_PAD + i * DOCS_ROW_H;
        if (li >= DOCS_LINE_COUNT) break;

        if (k_docs[li].type == DOC_HDR) {
            gfx_fill_rect(r.x, row_y - 2, r.w - 10, DOCS_ROW_H + 2, COL_DOCS_HDR);
            gfx_draw_string(r.x + DOCS_PAD, row_y,
                            k_docs[li].text, COL_DOCS_HDR_TXT, COL_DOCS_HDR);
        } else if (k_docs[li].type == DOC_TXT) {
            gfx_draw_string(r.x + DOCS_PAD + 8, row_y,
                            k_docs[li].text, COL_DOCS_TXT, COL_DOCS_BG);
        }
        /* DOC_BLANK: just a blank line, nothing to draw */

        if (k_docs[li].type != DOC_HDR && li + 1 < DOCS_LINE_COUNT &&
            k_docs[li + 1].type == DOC_HDR) {
            gfx_fill_rect(r.x, row_y + DOCS_ROW_H - 1, r.w - 10, 1, COL_DOCS_RULE);
        }
    }
}

static void docs_on_key(int win_id, char key) {
    int rows = docs_visible_rows();
    (void)win_id;
    if (key == KEY_UP)       g_scroll--;
    else if (key == KEY_DOWN)    g_scroll++;
    else if (key == KEY_PAGEUP)  g_scroll -= rows;
    else if (key == KEY_PAGEDOWN)g_scroll += rows;
    else if (key == KEY_HOME)    g_scroll = 0;
    else if (key == KEY_END)     g_scroll = DOCS_LINE_COUNT;
    else if (key == 0x11 || key == 0x1B) { gui_window_close(g_win_id); return; }
    if (g_scroll < 0) g_scroll = 0;
}

static void docs_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    (void)win_id; (void)x; (void)y;
    /* scroll wheel: buttons bit2=up, bit3=down */
    if (buttons & 0x04u) g_scroll -= 3;
    if (buttons & 0x08u) g_scroll += 3;
    if (g_scroll < 0) g_scroll = 0;
}

static void docs_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void axdocs_gui_launch(void) {
    gui_rect_t rect;
    gui_window_t *w;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    g_scroll = 0;
    gui_window_suggest_rect(DOCS_WIN_W, DOCS_WIN_H, &rect);
    g_win_id = gui_window_create("Ax Language Reference", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;

    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_AXDOCS;
    w->on_paint  = docs_on_paint;
    w->on_key    = docs_on_key;
    w->on_mouse  = docs_on_mouse;
    w->on_close  = docs_on_close;
}
