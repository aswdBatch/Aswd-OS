#include "gui/calc_gui.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "gui/gui.h"
#include "lib/string.h"

/* ---- Layout constants ---- */
#define CALC_W       260
#define CALC_H       320

#define DISP_H       56
#define DISP_PAD     10

#define BTN_COLS     4
#define BTN_ROWS     5
#define BTN_W        54
#define BTN_H        40
#define BTN_GAP      4
#define BTN_MARGIN   12

/* ---- Colors ---- */
#define COL_BG          gfx_rgb(240, 243, 249)
#define COL_DISP_BG     gfx_rgb(22,  34,  60)
#define COL_DISP_TXT    gfx_rgb(255, 255, 255)
#define COL_DISP_DIM    gfx_rgb(140, 160, 200)
#define COL_BTN_NUM     gfx_rgb(255, 255, 255)
#define COL_BTN_OP      gfx_rgb(37,  99, 235)
#define COL_BTN_OP_TXT  gfx_rgb(255, 255, 255)
#define COL_BTN_CLR     gfx_rgb(220,  50,  50)
#define COL_BTN_CLR_TXT gfx_rgb(255, 255, 255)
#define COL_BTN_EQL     gfx_rgb(22, 163,  74)
#define COL_BTN_EQL_TXT gfx_rgb(255, 255, 255)
#define COL_BTN_TXT     gfx_rgb(22,  34,  60)
#define COL_BTN_BORDER  gfx_rgb(180, 195, 220)

/* ---- Button kinds ---- */
#define BK_NUM  0
#define BK_OP   1
#define BK_CLR  2
#define BK_EQL  3

typedef struct {
    const char *label;
    int kind;
    char ch;   /* character sent to logic */
} calc_btn_t;

static const calc_btn_t g_btns[BTN_ROWS][BTN_COLS] = {
    { {"C",   BK_CLR, 'C'}, {"(",  BK_OP,  '('}, {")",  BK_OP,  ')'}, {"/",  BK_OP,  '/'} },
    { {"7",   BK_NUM, '7'}, {"8",  BK_NUM, '8'}, {"9",  BK_NUM, '9'}, {"*",  BK_OP,  '*'} },
    { {"4",   BK_NUM, '4'}, {"5",  BK_NUM, '5'}, {"6",  BK_NUM, '6'}, {"-",  BK_OP,  '-'} },
    { {"1",   BK_NUM, '1'}, {"2",  BK_NUM, '2'}, {"3",  BK_NUM, '3'}, {"+",  BK_OP,  '+'} },
    { {"0",   BK_NUM, '0'}, {".",  BK_NUM, '.'}, {"\b", BK_CLR, '\b'}, {"=", BK_EQL, '='} },
};

/* ---- Calculator state ---- */
#define EXPR_MAX 48

static int    g_win_id = -1;
static char   g_expr[EXPR_MAX];   /* expression string (what user typed) */
static int    g_expr_len = 0;
static char   g_result[EXPR_MAX]; /* last computed result or error message */
static int    g_has_result = 0;   /* 1 = show result, 0 = no result yet */
static int    g_err = 0;          /* 1 = error state */

/* ---- Integer-only expression evaluator ---- *
 * Supports +, -, *, / and parentheses. No floats. */

static const char *g_eval_pos;

static void   eval_skip_ws(void) { while (*g_eval_pos == ' ') g_eval_pos++; }
static int    eval_expr(int32_t *out);
static int    eval_term(int32_t *out);
static int    eval_factor(int32_t *out);

static int eval_factor(int32_t *out) {
    eval_skip_ws();
    if (*g_eval_pos == '(') {
        g_eval_pos++;
        if (!eval_expr(out)) return 0;
        eval_skip_ws();
        if (*g_eval_pos != ')') return 0;
        g_eval_pos++;
        return 1;
    }
    int neg = 0;
    if (*g_eval_pos == '-') { neg = 1; g_eval_pos++; }
    else if (*g_eval_pos == '+') { g_eval_pos++; }
    if (*g_eval_pos < '0' || *g_eval_pos > '9') return 0;
    int32_t v = 0;
    while (*g_eval_pos >= '0' && *g_eval_pos <= '9') {
        v = v * 10 + (*g_eval_pos - '0');
        g_eval_pos++;
    }
    /* fractional part (truncated) */
    if (*g_eval_pos == '.') {
        g_eval_pos++;
        while (*g_eval_pos >= '0' && *g_eval_pos <= '9') g_eval_pos++;
    }
    *out = neg ? -v : v;
    return 1;
}

static int eval_term(int32_t *out) {
    int32_t a;
    if (!eval_factor(&a)) return 0;
    for (;;) {
        eval_skip_ws();
        char op = *g_eval_pos;
        if (op != '*' && op != '/') break;
        g_eval_pos++;
        int32_t b;
        if (!eval_factor(&b)) return 0;
        if (op == '*') a = a * b;
        else           { if (b == 0) return 0; a = a / b; }
    }
    *out = a;
    return 1;
}

static int eval_expr(int32_t *out) {
    int32_t a;
    if (!eval_term(&a)) return 0;
    for (;;) {
        eval_skip_ws();
        char op = *g_eval_pos;
        if (op != '+' && op != '-') break;
        g_eval_pos++;
        int32_t b;
        if (!eval_term(&b)) return 0;
        if (op == '+') a = a + b;
        else           a = a - b;
    }
    *out = a;
    return 1;
}

static void calc_evaluate(void) {
    int32_t result;
    g_eval_pos = g_expr;
    if (eval_expr(&result) && *g_eval_pos == '\0') {
        /* convert to string */
        char tmp[16];
        int neg = (result < 0);
        uint32_t uval = neg ? (uint32_t)(-result) : (uint32_t)result;
        u32_to_dec(uval, tmp, sizeof(tmp));
        g_result[0] = '\0';
        if (neg) str_cat(g_result, "-", sizeof(g_result));
        str_cat(g_result, tmp, sizeof(g_result));
        g_err = 0;
    } else {
        str_copy(g_result, "Error", sizeof(g_result));
        g_err = 1;
    }
    g_has_result = 1;
}

static void calc_press(char ch) {
    if (ch == 'C') {
        g_expr[0] = '\0';
        g_expr_len = 0;
        g_result[0] = '\0';
        g_has_result = 0;
        g_err = 0;
        return;
    }
    if (ch == '=') {
        calc_evaluate();
        /* If result not error, replace expr with result for chaining */
        if (!g_err) {
            str_copy(g_expr, g_result, sizeof(g_expr));
            g_expr_len = (int)str_len(g_expr);
        }
        return;
    }
    if (ch == '\b') {
        if (g_expr_len > 0) {
            g_expr[--g_expr_len] = '\0';
        }
        g_has_result = 0;
        return;
    }
    if (g_expr_len + 1 >= EXPR_MAX) return;
    g_expr[g_expr_len++] = ch;
    g_expr[g_expr_len]   = '\0';
    g_has_result = 0;
}

/* ---- Drawing helpers ---- */

static void btn_rect(int row, int col, int *bx, int *by) {
    *bx = BTN_MARGIN + col * (BTN_W + BTN_GAP);
    *by = DISP_H + BTN_MARGIN + row * (BTN_H + BTN_GAP);
}

static void draw_button(int cx, int row, int col, int ry) {
    int bx, by;
    const calc_btn_t *b = &g_btns[row][col];
    uint32_t bg, fg;
    btn_rect(row, col, &bx, &by);
    bx += cx; by += ry;

    if (b->kind == BK_OP)       { bg = COL_BTN_OP;  fg = COL_BTN_OP_TXT;  }
    else if (b->kind == BK_CLR) { bg = COL_BTN_CLR; fg = COL_BTN_CLR_TXT; }
    else if (b->kind == BK_EQL) { bg = COL_BTN_EQL; fg = COL_BTN_EQL_TXT; }
    else                        { bg = COL_BTN_NUM;  fg = COL_BTN_TXT;     }

    /* Border */
    gfx_fill_rect(bx - 1, by - 1, BTN_W + 2, BTN_H + 2, COL_BTN_BORDER);
    gfx_fill_rect(bx, by, BTN_W, BTN_H, bg);

    /* Label */
    const char *lbl = b->label;
    if (b->ch == '\b') lbl = "BS";
    int lw = (int)str_len(lbl) * FONT_WIDTH;
    gfx_draw_string(bx + (BTN_W - lw) / 2,
                    by + (BTN_H - FONT_HEIGHT) / 2,
                    lbl, fg, bg);
}

static void calc_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    int cx = r.x, ry = r.y;
    int row, col;

    /* Background */
    gfx_fill_rect(cx, ry, r.w, r.h, COL_BG);

    /* Display area */
    gfx_fill_rect(cx, ry, r.w, DISP_H, COL_DISP_BG);

    /* Expression line (small, top) */
    if (g_expr_len > 0) {
        gfx_draw_string(cx + DISP_PAD, ry + 8, g_expr, COL_DISP_DIM, COL_DISP_BG);
    }

    /* Result line (large, bottom of display) */
    if (g_has_result) {
        int rw = (int)str_len(g_result) * FONT_WIDTH;
        gfx_draw_string(cx + r.w - rw - DISP_PAD,
                        ry + DISP_H - FONT_HEIGHT - 10,
                        g_result,
                        g_err ? gfx_rgb(255, 100, 100) : COL_DISP_TXT,
                        COL_DISP_BG);
    } else if (g_expr_len == 0) {
        gfx_draw_string(cx + r.w - FONT_WIDTH - DISP_PAD,
                        ry + DISP_H - FONT_HEIGHT - 10,
                        "0", COL_DISP_DIM, COL_DISP_BG);
    }

    /* Buttons */
    for (row = 0; row < BTN_ROWS; row++) {
        for (col = 0; col < BTN_COLS; col++) {
            draw_button(cx, row, col, ry);
        }
    }
}

static void calc_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }
    if (key == '\r' || key == '\n') { calc_press('='); return; }
    if (key == 0x1B) { calc_press('C'); return; }   /* Escape = clear */
    if (key == '\b') { calc_press('\b'); return; }
    /* Accept digits, operators, parens, dot */
    if ((key >= '0' && key <= '9') ||
        key == '+' || key == '-' || key == '*' || key == '/' ||
        key == '(' || key == ')' || key == '.') {
        calc_press(key);
    }
}

static void calc_on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    (void)win_id;
    if (!(buttons & 1)) return;

    /* Check which button was clicked */
    int row, col;
    for (row = 0; row < BTN_ROWS; row++) {
        for (col = 0; col < BTN_COLS; col++) {
            int bx, by;
            btn_rect(row, col, &bx, &by);
            if (mx >= bx && mx < bx + BTN_W &&
                my >= by && my < by + BTN_H) {
                calc_press(g_btns[row][col].ch);
                return;
            }
        }
    }
}

static void calc_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void calc_gui_launch(void) {
    gui_rect_t r;
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_window_suggest_rect(CALC_W, CALC_H, &r);
    g_win_id = gui_window_create("Calculator", r.x, r.y, r.w, r.h);
    if (g_win_id < 0) return;

    g_expr[0] = '\0'; g_expr_len = 0;
    g_result[0] = '\0'; g_has_result = 0; g_err = 0;

    gui_window_t *w = gui_get_window(g_win_id);
    w->on_paint = calc_on_paint;
    w->on_key   = calc_on_key;
    w->on_mouse = calc_on_mouse;
    w->on_close = calc_on_close;
}
