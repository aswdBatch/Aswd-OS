#include "gui/axapp_gui.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "lib/string.h"

/* ---- Layout ---- */
#define FORM_OX   10
#define FORM_OY   10

/* ---- Colors ---- */
#define COL_BG          gfx_rgb(230, 235, 242)
#define COL_FORM_BG     gfx_rgb(245, 248, 252)
#define COL_FORM_BDR    gfx_rgb(80, 100, 132)
#define COL_BTN_BG      gfx_rgb(59, 130, 246)
#define COL_BTN_TXT     gfx_rgb(255, 255, 255)
#define COL_BTN_DIS     gfx_rgb(160, 175, 200)
#define COL_BTN_DOWN    gfx_rgb(30, 90, 200)
#define COL_LBL_TXT     gfx_rgb(20, 30, 50)
#define COL_BOX_BG      gfx_rgb(255, 255, 255)
#define COL_BOX_BDR     gfx_rgb(120, 140, 170)
#define COL_BOX_FBDR    gfx_rgb(50, 110, 220)
#define COL_CHK_BDR     gfx_rgb(80, 100, 132)

/* ---- Instance pool ---- */
#define AXAPP_MAX 4

typedef struct {
    int          active;
    int          win_id;
    ax_project_t proj;
    int          active_scene;
    int          focused_box;
    int          pressed_btn;
    uint8_t      visible[AX_MAX_SCENES][AX_MAX_CTRLS];
    uint8_t      enabled[AX_MAX_SCENES][AX_MAX_CTRLS];
    uint8_t      checked[AX_MAX_CTRLS];
    char         box_vals[AX_MAX_CTRLS][AX_TEXT_LEN];
    int          box_lens[AX_MAX_CTRLS];
} axapp_inst_t;

static axapp_inst_t g_inst[AXAPP_MAX];

/* ---- Expression evaluator (copy from calc_gui pattern) ---- */
static const char *g_ep;

static void   ep_skip(void)   { while (*g_ep == ' ') g_ep++; }
static int    ep_expr(int32_t *out);
static int    ep_term(int32_t *out);
static int    ep_factor(int32_t *out);

static int ep_factor(int32_t *out) {
    ep_skip();
    if (*g_ep == '(') {
        g_ep++;
        if (!ep_expr(out)) return 0;
        ep_skip();
        if (*g_ep != ')') return 0;
        g_ep++;
        return 1;
    }
    int neg = 0;
    if      (*g_ep == '-') { neg = 1; g_ep++; }
    else if (*g_ep == '+') { g_ep++; }
    if (*g_ep < '0' || *g_ep > '9') return 0;
    int32_t v = 0;
    while (*g_ep >= '0' && *g_ep <= '9') { v = v * 10 + (*g_ep - '0'); g_ep++; }
    if (*g_ep == '.') { g_ep++; while (*g_ep >= '0' && *g_ep <= '9') g_ep++; }
    *out = neg ? -v : v;
    return 1;
}

static int ep_term(int32_t *out) {
    int32_t a; if (!ep_factor(&a)) return 0;
    for (;;) {
        ep_skip(); char op = *g_ep;
        if (op != '*' && op != '/') break;
        g_ep++;
        int32_t b; if (!ep_factor(&b)) return 0;
        if (op == '*') a = a * b;
        else { if (b == 0) return 0; a = a / b; }
    }
    *out = a; return 1;
}

static int ep_expr(int32_t *out) {
    int32_t a; if (!ep_term(&a)) return 0;
    for (;;) {
        ep_skip(); char op = *g_ep;
        if (op != '+' && op != '-') break;
        g_ep++;
        int32_t b; if (!ep_term(&b)) return 0;
        a = (op == '+') ? a + b : a - b;
    }
    *out = a; return 1;
}

static void eval_to_str(const char *expr, char *out, int outsz) {
    int32_t result;
    g_ep = expr;
    if (ep_expr(&result) && (*g_ep == '\0' || *g_ep == ' ')) {
        int neg = (result < 0);
        uint32_t uv = neg ? (uint32_t)(-result) : (uint32_t)result;
        char tmp[16];
        u32_to_dec(uv, tmp, sizeof(tmp));
        out[0] = '\0';
        if (neg) str_cat(out, "-", outsz);
        str_cat(out, tmp, outsz);
    } else {
        str_copy(out, "Err", outsz);
    }
}

/* ---- Logic engine ---- */
static void logic_fire(int si, int ttype, int param);
static void logic_exec(int si, int ni, int *budget);
static void follow_wire(int si, int from_node, int from_port, int *budget);

static void set_ctrl_text(axapp_inst_t *s, int ci, const char *val) {
    if (ci < 0 || ci >= AX_MAX_CTRLS) return;
    str_copy(s->box_vals[ci], val, AX_TEXT_LEN);
    s->box_lens[ci] = (int)str_len(s->box_vals[ci]);
}

static const char *resolve_val(axapp_inst_t *s, ax_logic_node_t *n) {
    static char rbuf[AX_TEXT_LEN];
    if (n->param[1] == 0) return n->str;
    if (n->param[1] == 1) {
        int ci = n->param[0];
        if (ci >= 0 && ci < AX_MAX_CTRLS) return s->box_vals[ci];
        return "";
    }
    /* param[1] == 2: evaluate box as expression */
    {
        int ci = n->param[0];
        const char *expr = (ci >= 0 && ci < AX_MAX_CTRLS) ? s->box_vals[ci] : n->str;
        eval_to_str(expr, rbuf, sizeof(rbuf));
        return rbuf;
    }
}

static void follow_wire(int si, int from_node, int from_port, int *budget) {
    if (*budget <= 0) return;
    axapp_inst_t *s = &g_inst[si];
    for (int i = 0; i < s->proj.lwire_count; i++) {
        ax_logic_wire_t *w = &s->proj.lwires[i];
        if (!w->active) continue;
        if (w->from_node == from_node && w->from_port == from_port) {
            logic_exec(si, w->to_node, budget);
        }
    }
}

static void logic_exec(int si, int ni, int *budget) {
    if (*budget <= 0) return;
    (*budget)--;
    axapp_inst_t *s = &g_inst[si];
    if (ni < 0 || ni >= s->proj.lnode_count) return;
    ax_logic_node_t *n = &s->proj.lnodes[ni];
    if (!n->active) return;

    int sc = s->active_scene;

    switch ((ln_type_t)n->type) {
    case LN_ACT_SET_TEXT:
        set_ctrl_text(s, n->param[0], resolve_val(s, n));
        break;
    case LN_ACT_SHOW:
        if (n->param[0] >= 0 && n->param[0] < AX_MAX_CTRLS)
            s->visible[sc][n->param[0]] = 1;
        break;
    case LN_ACT_HIDE:
        if (n->param[0] >= 0 && n->param[0] < AX_MAX_CTRLS)
            s->visible[sc][n->param[0]] = 0;
        break;
    case LN_ACT_SCENE:
        if (n->param[0] >= 0 && n->param[0] < s->proj.scene_count)
            s->active_scene = n->param[0];
        break;
    case LN_ACT_ENABLE:
        if (n->param[0] >= 0 && n->param[0] < AX_MAX_CTRLS)
            s->enabled[sc][n->param[0]] = 1;
        break;
    case LN_ACT_DISABLE:
        if (n->param[0] >= 0 && n->param[0] < AX_MAX_CTRLS)
            s->enabled[sc][n->param[0]] = 0;
        break;
    case LN_COND_IF_TEXT_EQ: {
        int ci = n->param[0];
        const char *bv = (ci >= 0 && ci < AX_MAX_CTRLS) ? s->box_vals[ci] : "";
        int eq = (str_ncmp(bv, n->str, AX_TEXT_LEN) == 0);
        follow_wire(si, ni, eq ? 0 : 1, budget);
        return;
    }
    case LN_COND_IF_CHECKED: {
        int ci = n->param[0];
        int chk = (ci >= 0 && ci < AX_MAX_CTRLS) ? s->checked[ci] : 0;
        follow_wire(si, ni, chk ? 0 : 1, budget);
        return;
    }
    /* Triggers don't get exec'd directly */
    default:
        break;
    }

    follow_wire(si, ni, 0, budget);
}

static void logic_fire(int si, int ttype, int param) {
    axapp_inst_t *s = &g_inst[si];
    int tnode = -1;
    for (int i = 0; i < s->proj.lnode_count; i++) {
        ax_logic_node_t *n = &s->proj.lnodes[i];
        if (!n->active) continue;
        if (n->type == (uint8_t)ttype && n->param[0] == (int16_t)param) {
            tnode = i;
            break;
        }
    }
    if (ttype == LN_TRIG_START) {
        /* LN_TRIG_START has no ctrl param — just find first start node */
        for (int i = 0; i < s->proj.lnode_count; i++) {
            ax_logic_node_t *n = &s->proj.lnodes[i];
            if (!n->active) continue;
            if (n->type == (uint8_t)LN_TRIG_START) { tnode = i; break; }
        }
    }
    if (tnode < 0) return;
    int budget = 64;
    follow_wire(si, tnode, 0, &budget);
}

/* ---- on_paint ---- */
static void axapp_on_paint(int win_id) {
    /* find instance */
    int si = -1;
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (g_inst[i].active && g_inst[i].win_id == win_id) { si = i; break; }
    }
    if (si < 0) return;
    axapp_inst_t *s = &g_inst[si];

    gui_rect_t cr = gui_window_content(win_id);
    int cx = cr.x, cy = cr.y, cw = cr.w, ch = cr.h;

    gfx_fill_rect(cx, cy, cw, ch, COL_BG);

    /* Form background */
    int fw = s->proj.form_w;
    int fh = s->proj.form_h;
    if (fw < 1) fw = 400;
    if (fh < 1) fh = 300;
    int fox = cx + FORM_OX;
    int foy = cy + FORM_OY;
    gfx_fill_rect(fox, foy, fw, fh, COL_FORM_BG);
    /* border */
    gfx_fill_rect(fox, foy, fw, 1, COL_FORM_BDR);
    gfx_fill_rect(fox, foy + fh - 1, fw, 1, COL_FORM_BDR);
    gfx_fill_rect(fox, foy, 1, fh, COL_FORM_BDR);
    gfx_fill_rect(fox + fw - 1, foy, 1, fh, COL_FORM_BDR);

    int sc = s->active_scene;
    if (sc < 0 || sc >= s->proj.scene_count) return;
    ax_scene_t *scene = &s->proj.scenes[sc];

    for (int i = 0; i < scene->ctrl_count; i++) {
        if (!s->visible[sc][i]) continue;
        ax_ctrl_t *c = &scene->ctrls[i];
        int x = fox + c->x;
        int y = foy + c->y;
        int w = c->w;
        int h = c->h;
        int dis = !s->enabled[sc][i];

        switch (c->type) {
        case AX_CTRL_LABEL:
            gfx_draw_string(x, y + (h - FONT_HEIGHT) / 2, c->text, COL_LBL_TXT, COL_FORM_BG);
            break;

        case AX_CTRL_BUTTON: {
            uint32_t bg = dis ? COL_BTN_DIS :
                          (s->pressed_btn == i ? COL_BTN_DOWN : COL_BTN_BG);
            gfx_fill_rect(x, y, w, h, bg);
            /* border */
            gfx_fill_rect(x, y, w, 1, COL_FORM_BDR);
            gfx_fill_rect(x, y + h - 1, w, 1, COL_FORM_BDR);
            gfx_fill_rect(x, y, 1, h, COL_FORM_BDR);
            gfx_fill_rect(x + w - 1, y, 1, h, COL_FORM_BDR);
            /* centered label */
            int tl = (int)str_len(c->text) * FONT_WIDTH;
            int tx = x + (w - tl) / 2;
            int ty = y + (h - FONT_HEIGHT) / 2;
            gfx_draw_string(tx, ty, c->text, COL_BTN_TXT, bg);
            break;
        }

        case AX_CTRL_TEXTBOX: {
            gfx_fill_rect(x, y, w, h, COL_BOX_BG);
            uint32_t bdr = (s->focused_box == i) ? COL_BOX_FBDR : COL_BOX_BDR;
            gfx_fill_rect(x, y, w, 1, bdr);
            gfx_fill_rect(x, y + h - 1, w, 1, bdr);
            gfx_fill_rect(x, y, 1, h, bdr);
            gfx_fill_rect(x + w - 1, y, 1, h, bdr);
            int ty2 = y + (h - FONT_HEIGHT) / 2;
            gfx_draw_string(x + 4, ty2, s->box_vals[i], COL_LBL_TXT, COL_BOX_BG);
            if (s->focused_box == i) {
                int cx2 = x + 4 + s->box_lens[i] * FONT_WIDTH;
                gfx_fill_rect(cx2, ty2, 1, FONT_HEIGHT, COL_LBL_TXT);
            }
            break;
        }

        case AX_CTRL_CHECKBOX: {
            int bsz = 14;
            int bx = x;
            int by = y + (h - bsz) / 2;
            gfx_fill_rect(bx, by, bsz, bsz, COL_BOX_BG);
            gfx_fill_rect(bx, by, bsz, 1, COL_CHK_BDR);
            gfx_fill_rect(bx, by + bsz - 1, bsz, 1, COL_CHK_BDR);
            gfx_fill_rect(bx, by, 1, bsz, COL_CHK_BDR);
            gfx_fill_rect(bx + bsz - 1, by, 1, bsz, COL_CHK_BDR);
            if (s->checked[i]) {
                gfx_draw_string(bx + 2, by + (bsz - FONT_HEIGHT) / 2, "x", COL_LBL_TXT, COL_BOX_BG);
            }
            gfx_draw_string(x + bsz + 4, y + (h - FONT_HEIGHT) / 2, c->text, COL_LBL_TXT, COL_FORM_BG);
            break;
        }
        }
    }
}

/* ---- on_mouse ---- */
static void axapp_on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    int si = -1;
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (g_inst[i].active && g_inst[i].win_id == win_id) { si = i; break; }
    }
    if (si < 0) return;
    axapp_inst_t *s = &g_inst[si];

    if (!(buttons & 1)) {
        s->pressed_btn = -1;
        return;
    }

    gui_rect_t cr = gui_window_content(win_id);
    int fox = cr.x + FORM_OX;
    int foy = cr.y + FORM_OY;

    int sc = s->active_scene;
    if (sc < 0 || sc >= s->proj.scene_count) return;
    ax_scene_t *scene = &s->proj.scenes[sc];

    int hit = 0;
    for (int i = 0; i < scene->ctrl_count; i++) {
        if (!s->visible[sc][i]) continue;
        ax_ctrl_t *c = &scene->ctrls[i];
        int x = fox + c->x;
        int y = foy + c->y;
        if (mx >= x && mx < x + c->w && my >= y && my < y + c->h) {
            hit = 1;
            if (!s->enabled[sc][i]) break;
            switch (c->type) {
            case AX_CTRL_BUTTON:
                if (s->pressed_btn != i) {
                    s->pressed_btn = i;
                    logic_fire(si, LN_TRIG_CLICK, i);
                }
                break;
            case AX_CTRL_TEXTBOX:
                s->focused_box = i;
                break;
            case AX_CTRL_CHECKBOX:
                s->checked[i] ^= 1;
                logic_fire(si, LN_TRIG_TOGGLE, i);
                break;
            default:
                break;
            }
            break;
        }
    }
    if (!hit) {
        s->focused_box = -1;
    }
}

/* ---- on_key ---- */
static void axapp_on_key(int win_id, char key) {
    int si = -1;
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (g_inst[i].active && g_inst[i].win_id == win_id) { si = i; break; }
    }
    if (si < 0) return;
    axapp_inst_t *s = &g_inst[si];

    int fb = s->focused_box;
    if (fb < 0 || fb >= AX_MAX_CTRLS) return;

    if (key == '\r' || key == '\n') {
        logic_fire(si, LN_TRIG_SUBMIT, fb);
        return;
    }
    if (key == '\b') {
        if (s->box_lens[fb] > 0) {
            s->box_vals[fb][--s->box_lens[fb]] = '\0';
        }
        return;
    }
    if (key >= 32 && key < 127) {
        if (s->box_lens[fb] + 1 < AX_TEXT_LEN) {
            s->box_vals[fb][s->box_lens[fb]++] = key;
            s->box_vals[fb][s->box_lens[fb]] = '\0';
        }
        return;
    }
}

/* ---- on_close ---- */
static void axapp_on_close(int win_id) {
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (g_inst[i].active && g_inst[i].win_id == win_id) {
            g_inst[i].active = 0;
            return;
        }
    }
}

/* ---- Internal launch ---- */
static void axapp_launch_slot(int si) {
    axapp_inst_t *s = &g_inst[si];

    /* Init runtime state */
    for (int sc = 0; sc < AX_MAX_SCENES; sc++)
        for (int ci = 0; ci < AX_MAX_CTRLS; ci++) {
            s->visible[sc][ci] = 1;
            s->enabled[sc][ci] = 1;
        }
    for (int ci = 0; ci < AX_MAX_CTRLS; ci++) {
        s->checked[ci] = 0;
        s->box_vals[ci][0] = '\0';
        s->box_lens[ci] = 0;
    }
    s->active_scene = 0;
    s->focused_box  = -1;
    s->pressed_btn  = -1;

    /* Window size = form + padding */
    int fw = s->proj.form_w > 0 ? s->proj.form_w : 400;
    int fh = s->proj.form_h > 0 ? s->proj.form_h : 300;
    int ww = fw + FORM_OX * 2;
    int wh = fh + FORM_OY * 2;

    char title[AX_TITLE_LEN + 4];
    str_copy(title, s->proj.title, sizeof(title));

    int wid = gui_window_create(title, -1, -1, ww, wh);
    if (wid < 0) { s->active = 0; return; }

    s->win_id = wid;
    gui_window_t *w = gui_get_window(wid);
    w->on_paint = axapp_on_paint;
    w->on_mouse = axapp_on_mouse;
    w->on_key   = axapp_on_key;
    w->on_close = axapp_on_close;

    gui_window_focus(wid);

    /* Fire On App Start */
    logic_fire(si, LN_TRIG_START, 0);
}

/* ---- Public API ---- */
void axapp_gui_launch(ax_project_t *p) {
    int si = -1;
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (!g_inst[i].active) { si = i; break; }
    }
    if (si < 0) return; /* no free slot */
    g_inst[si].active = 1;
    /* copy project */
    ax_project_t *dst = &g_inst[si].proj;
    dst->form_w      = p->form_w;
    dst->form_h      = p->form_h;
    dst->scene_count = p->scene_count;
    dst->lnode_count = p->lnode_count;
    dst->lwire_count = p->lwire_count;
    str_copy(dst->title, p->title, AX_TITLE_LEN);
    for (int i = 0; i < AX_MAX_SCENES; i++) dst->scenes[i] = p->scenes[i];
    for (int i = 0; i < LN_MAX; i++)        dst->lnodes[i] = p->lnodes[i];
    for (int i = 0; i < LW_MAX; i++)        dst->lwires[i] = p->lwires[i];

    axapp_launch_slot(si);
}

/* ---- File parser ---- */
#define PARSE_BUF 8192
static char g_pbuf[PARSE_BUF];

static const char *pp_skip(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static const char *pp_int(const char *p, int *out) {
    p = pp_skip(p);
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    int v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    *out = neg ? -v : v;
    return p;
}

static const char *pp_word(const char *p, char *out, int outsz) {
    p = pp_skip(p);
    int i = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < outsz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return p;
}

static const char *pp_quoted(const char *p, char *out, int outsz) {
    p = pp_skip(p);
    if (*p == '"') p++;
    int i = 0;
    while (*p && *p != '"' && *p != '\r' && *p != '\n' && i < outsz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

static int ctrl_type_from_str(const char *s) {
    if (str_eq(s, "button"))   return AX_CTRL_BUTTON;
    if (str_eq(s, "label"))    return AX_CTRL_LABEL;
    if (str_eq(s, "textbox"))  return AX_CTRL_TEXTBOX;
    if (str_eq(s, "checkbox")) return AX_CTRL_CHECKBOX;
    return -1;
}

static int ln_type_from_str(const char *s) {
    if (str_eq(s, "trig_click"))        return LN_TRIG_CLICK;
    if (str_eq(s, "trig_start"))        return LN_TRIG_START;
    if (str_eq(s, "trig_submit"))       return LN_TRIG_SUBMIT;
    if (str_eq(s, "trig_toggle"))       return LN_TRIG_TOGGLE;
    if (str_eq(s, "act_set_text"))      return LN_ACT_SET_TEXT;
    if (str_eq(s, "act_show"))          return LN_ACT_SHOW;
    if (str_eq(s, "act_hide"))          return LN_ACT_HIDE;
    if (str_eq(s, "act_scene"))         return LN_ACT_SCENE;
    if (str_eq(s, "act_enable"))        return LN_ACT_ENABLE;
    if (str_eq(s, "act_disable"))       return LN_ACT_DISABLE;
    if (str_eq(s, "cond_if_text_eq"))   return LN_COND_IF_TEXT_EQ;
    if (str_eq(s, "cond_if_checked"))   return LN_COND_IF_CHECKED;
    return -1;
}

static int axapp_parse(ax_project_t *proj) {
    const char *p = g_pbuf;
    char word[64], word2[64];
    int ival;
    int cur_scene = 0;

    /* zero out */
    str_copy(proj->title, "App", AX_TITLE_LEN);
    proj->form_w = 400; proj->form_h = 300;
    proj->scene_count = 0;
    proj->lnode_count = 0;
    proj->lwire_count = 0;
    for (int i = 0; i < AX_MAX_SCENES; i++) {
        proj->scenes[i].name[0] = '\0';
        proj->scenes[i].ctrl_count = 0;
    }
    for (int i = 0; i < LN_MAX; i++) proj->lnodes[i].active = 0;
    for (int i = 0; i < LW_MAX; i++) proj->lwires[i].active = 0;

    while (*p) {
        /* skip to line start */
        p = pp_skip(p);
        if (*p == '\r' || *p == '\n') { p++; continue; }
        if (*p == '\0') break;

        p = pp_word(p, word, sizeof(word));

        if (str_eq(word, "ax_studio")) {
            /* version line — skip */
            while (*p && *p != '\r' && *p != '\n') p++;
        } else if (str_eq(word, "form")) {
            p = pp_quoted(p, proj->title, AX_TITLE_LEN);
            p = pp_int(p, &ival); proj->form_w = ival;
            p = pp_int(p, &ival); proj->form_h = ival;
        } else if (str_eq(word, "scene_count")) {
            p = pp_int(p, &ival); proj->scene_count = ival;
        } else if (str_eq(word, "scene")) {
            p = pp_int(p, &ival); cur_scene = ival;
            if (cur_scene >= 0 && cur_scene < AX_MAX_SCENES) {
                p = pp_quoted(p, proj->scenes[cur_scene].name, AX_TITLE_LEN);
            }
        } else if (str_eq(word, "ctrl")) {
            int idx;
            p = pp_int(p, &idx);
            p = pp_word(p, word2, sizeof(word2));
            int ctype = ctrl_type_from_str(word2);
            if (ctype >= 0 && cur_scene >= 0 && cur_scene < AX_MAX_SCENES
                && idx >= 0 && idx < AX_MAX_CTRLS) {
                ax_ctrl_t *c = &proj->scenes[cur_scene].ctrls[idx];
                c->type = (ax_ctrl_type_t)ctype;
                p = pp_int(p, &c->x);
                p = pp_int(p, &c->y);
                p = pp_int(p, &c->w);
                p = pp_int(p, &c->h);
                p = pp_quoted(p, c->text, AX_TEXT_LEN);
                if (idx >= proj->scenes[cur_scene].ctrl_count)
                    proj->scenes[cur_scene].ctrl_count = idx + 1;
            }
        } else if (str_eq(word, "lnode_count")) {
            p = pp_int(p, &ival); proj->lnode_count = ival;
        } else if (str_eq(word, "lnode")) {
            int idx;
            p = pp_int(p, &idx);
            p = pp_word(p, word2, sizeof(word2));
            int ltype = ln_type_from_str(word2);
            if (ltype >= 0 && idx >= 0 && idx < LN_MAX) {
                ax_logic_node_t *n = &proj->lnodes[idx];
                n->active = 1;
                n->type   = (uint8_t)ltype;
                int v;
                p = pp_int(p, &v); n->canvas_x = (int16_t)v;
                p = pp_int(p, &v); n->canvas_y = (int16_t)v;
                p = pp_int(p, &v); n->param[0] = (int16_t)v;
                p = pp_int(p, &v); n->param[1] = (int16_t)v;
                p = pp_quoted(p, n->str, AX_TEXT_LEN);
            }
        } else if (str_eq(word, "lwire_count")) {
            p = pp_int(p, &ival); proj->lwire_count = ival;
        } else if (str_eq(word, "lwire")) {
            int idx;
            p = pp_int(p, &idx);
            if (idx >= 0 && idx < LW_MAX) {
                ax_logic_wire_t *ww = &proj->lwires[idx];
                ww->active = 1;
                int v;
                p = pp_int(p, &v); ww->from_node = (int16_t)v;
                p = pp_int(p, &v); ww->from_port = (uint8_t)v;
                p = pp_int(p, &v); ww->to_node   = (int16_t)v;
            }
        }

        /* advance past end of line */
        while (*p && *p != '\r' && *p != '\n') p++;
        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    return 1;
}

void axapp_gui_launch_file(const char *path) {
    int si = -1;
    for (int i = 0; i < AXAPP_MAX; i++) {
        if (!g_inst[i].active) { si = i; break; }
    }
    if (si < 0) return;

    int len = vfs_cat(path, (uint8_t *)g_pbuf, PARSE_BUF - 1);
    if (len <= 0) return;
    g_pbuf[len] = '\0';

    g_inst[si].active = 1;
    if (!axapp_parse(&g_inst[si].proj)) {
        g_inst[si].active = 0;
        return;
    }
    axapp_launch_slot(si);
}
