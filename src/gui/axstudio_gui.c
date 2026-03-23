#include "gui/axstudio_gui.h"
#include "gui/axapp_gui.h"

#include <stdint.h>

#include "drivers/font.h"
#include "drivers/gfx.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "lib/string.h"

/* ---- Layout ---- */
#define STUDIO_TB_H    32
#define STUDIO_PROPS_H 26
#define STUDIO_TBX_W   90
#define SCENE_TAB_H    22
#define SCENE_TAB_W    74
#define LN_W          160
#define LN_H           46
#define PORT_R          5
#define TOOL_H         26
#define STUDIO_W      660
#define STUDIO_H      500

/* ---- Colors ---- */
#define COL_TB_BG      gfx_rgb(26, 32, 50)
#define COL_BTN_DEF    gfx_rgb(48, 62, 88)
#define COL_BTN_ACT    gfx_rgb(50, 110, 220)
#define COL_BTN_TXT    gfx_rgb(255, 255, 255)
#define COL_TBX_BG     gfx_rgb(22, 28, 44)
#define COL_TBX_TXT    gfx_rgb(195, 212, 240)
#define COL_TBX_SEL    gfx_rgb(50, 110, 220)
#define COL_CANVAS_BG  gfx_rgb(168, 182, 202)
#define COL_FORM_BG    gfx_rgb(245, 248, 252)
#define COL_FORM_BDR   gfx_rgb(80, 100, 132)
#define COL_SEL        gfx_rgb(255, 160, 30)
#define COL_CTRL_BTN   gfx_rgb(59, 130, 246)
#define COL_CTRL_TXT   gfx_rgb(20, 30, 50)
#define COL_PROPS_BG   gfx_rgb(36, 46, 66)
#define COL_PROPS_TXT  gfx_rgb(210, 222, 245)
#define COL_FIELD_BG   gfx_rgb(255, 255, 255)
#define COL_FIELD_TXT  gfx_rgb(20, 30, 50)
#define COL_FIELD_BDR  gfx_rgb(80, 110, 160)
#define COL_FIELD_FBDR gfx_rgb(50, 110, 220)
#define COL_LOGIC_BG   gfx_rgb(18, 24, 38)
#define COL_PAL_BG     gfx_rgb(24, 32, 48)
#define COL_PAL_TXT    gfx_rgb(185, 205, 235)
#define COL_PAL_HDR    gfx_rgb(100, 120, 160)
#define COL_PAL_SEL    gfx_rgb(50, 100, 180)
#define COL_LN_TRIG    gfx_rgb(32, 140, 58)
#define COL_LN_ACT     gfx_rgb(37, 99, 200)
#define COL_LN_COND    gfx_rgb(180, 110, 24)
#define COL_LN_TXT     gfx_rgb(255, 255, 255)
#define COL_LN_INFO    gfx_rgb(210, 230, 255)
#define COL_WIRE       gfx_rgb(100, 190, 255)
#define COL_WIRE_RB    gfx_rgb(255, 200, 60)
#define COL_PORT_BG    gfx_rgb(240, 245, 255)
#define COL_PORT_BDR   gfx_rgb(20, 28, 45)
#define COL_SCENETAB   gfx_rgb(35, 50, 76)
#define COL_SCENETAB_A gfx_rgb(50, 110, 220)
#define COL_SCENETAB_T gfx_rgb(220, 232, 255)

/* ---- String tables ---- */
static const char * const g_ln_disp[LN_TYPE_COUNT] = {
    "On Click", "On Start", "On Submit", "On Toggle",
    "Set Text", "Show Ctrl", "Hide Ctrl", "Switch Scene",
    "Enable", "Disable",
    "If Text ==", "If Checked"
};
static const char * const g_ln_save[LN_TYPE_COUNT] = {
    "trig_click","trig_start","trig_submit","trig_toggle",
    "act_set_text","act_show","act_hide","act_scene",
    "act_enable","act_disable",
    "cond_if_text_eq","cond_if_checked"
};
static const char * const g_ctrl_save[4] = {
    "button","label","textbox","checkbox"
};
/* Palette items: -1 = section header, >= 0 = ln_type_t */
static const int g_pal_items[] = {
    -1, /* Triggers */
    LN_TRIG_CLICK, LN_TRIG_START, LN_TRIG_SUBMIT, LN_TRIG_TOGGLE,
    -2, /* Actions */
    LN_ACT_SET_TEXT, LN_ACT_SHOW, LN_ACT_HIDE, LN_ACT_SCENE,
    LN_ACT_ENABLE, LN_ACT_DISABLE,
    -3, /* Conditions */
    LN_COND_IF_TEXT_EQ, LN_COND_IF_CHECKED
};
#define PAL_ITEMS_COUNT 15

/* ---- State ---- */
static ax_project_t g_proj;
static int g_win_id = -1;
static int g_view   = 0;     /* 0=Design, 1=Logic */
static char g_filepath[64];

/* Design view */
static int g_scene    = 0;
static int g_selected = -1;  /* selected ctrl index */
static int g_tool     = -1;  /* -1=select, 0..3=ctrl type to place */
static int g_dragging = 0;
static int g_drag_ox, g_drag_oy;

/* Logic view */
static int g_lsel    = -1;   /* selected logic node */
static int g_ldrag   = 0;
static int g_ldrag_ox, g_ldrag_oy;
static int g_wiring  = 0;
static int g_wsrc    = -1;
static int g_wsrcp   = 0;
static int g_wex, g_wey;
static int g_placing = -1;  /* ln_type_t to place, -1=none */

/* Props bar */
static char g_edit_buf[AX_TEXT_LEN];
static int  g_edit_len;
static int  g_edit_mode;  /* 0=ctrl text, 1=logic str, 2=title, 3=open path */

/* Save buffer */
static char g_save_buf[8192];
static int  g_sbi;

/* ---- Drawing helpers ---- */
static void draw_line(int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = adx - ady;
    for (;;) {
        gfx_put_pixel(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -ady) { err -= ady; x0 += sx; }
        if (e2 <  adx) { err += adx; y0 += sy; }
    }
}

static void draw_rect_out(int x, int y, int w, int h, uint32_t col) {
    gfx_fill_rect(x,     y,     w, 1, col);
    gfx_fill_rect(x,     y+h-1, w, 1, col);
    gfx_fill_rect(x,     y,     1, h, col);
    gfx_fill_rect(x+w-1, y,     1, h, col);
}

static void str_center(int x, int y, int w, const char *s, uint32_t fg, uint32_t bg) {
    int tlen = (int)str_len(s);
    int tw = tlen * FONT_WIDTH;
    int tx = x + (w - tw) / 2;
    if (tx < x) tx = x;
    gfx_draw_string(tx, y, s, fg, bg);
}

static void draw_btn(int x, int y, int w, int h, const char *lbl, int active) {
    uint32_t bg = active ? COL_BTN_ACT : COL_BTN_DEF;
    gfx_fill_rect(x, y, w, h, bg);
    draw_rect_out(x, y, w, h, gfx_rgb(15, 20, 35));
    str_center(x, y + (h - FONT_HEIGHT) / 2, w, lbl, COL_BTN_TXT, bg);
}

static void draw_wire(int px0, int py0, int px1, int py1, uint32_t col) {
    int mx = (px0 + px1) / 2;
    draw_line(px0, py0, mx, py0, col);
    draw_line(mx,  py0, mx, py1, col);
    draw_line(mx,  py1, px1, py1, col);
}

static void draw_circle(int cx, int cy, int r, uint32_t fg, uint32_t bg) {
    gfx_fill_rect(cx - r, cy - r, r*2, r*2, bg);
    draw_rect_out(cx - r, cy - r, r*2, r*2, fg);
}

/* ---- Project helpers ---- */
static ax_scene_t *cur_scene(void) {
    if (g_scene < 0 || g_scene >= g_proj.scene_count) return 0;
    return &g_proj.scenes[g_scene];
}

static const char *ctrl_disp_name(int sci, int idx) {
    if (sci < 0 || sci >= g_proj.scene_count) return "?";
    ax_scene_t *sc = &g_proj.scenes[sci];
    if (idx < 0 || idx >= sc->ctrl_count) return "?";
    const char *t = sc->ctrls[idx].text;
    return (t && t[0]) ? t : g_ctrl_save[sc->ctrls[idx].type];
}

static uint32_t ln_color(int type) {
    if (type <= LN_TRIG_TOGGLE) return COL_LN_TRIG;
    if (type <= LN_ACT_DISABLE) return COL_LN_ACT;
    return COL_LN_COND;
}

static int ln_has_input(int type) {
    return type > (int)LN_TRIG_TOGGLE;
}

static int ln_out_count(int type) {
    return (type >= (int)LN_COND_IF_TEXT_EQ) ? 2 : 1;
}

static void port_pos(int ni, int is_out, int port_idx, int cleft, int ctop, int *px, int *py) {
    ax_logic_node_t *n = &g_proj.lnodes[ni];
    int nx = cleft + (int)n->canvas_x;
    int ny = ctop  + (int)n->canvas_y;
    int nouts = ln_out_count(n->type);
    if (!is_out) {
        *px = nx;
        *py = ny + LN_H / 2;
    } else {
        *px = nx + LN_W;
        *py = ny + (port_idx + 1) * LN_H / (nouts + 1);
    }
}

static int near_port(int px, int py, int mx, int my) {
    int dx = px - mx, dy = py - my;
    return dx*dx + dy*dy <= (PORT_R + 4) * (PORT_R + 4);
}

/* ---- ctrl default sizes ---- */
static void ctrl_defaults(ax_ctrl_t *c, int type, int x, int y) {
    c->type = (ax_ctrl_type_t)type;
    c->x = x; c->y = y;
    if (type == AX_CTRL_BUTTON)   { c->w = 100; c->h = 26; str_copy(c->text, "Button",   AX_TEXT_LEN); }
    else if (type == AX_CTRL_LABEL)    { c->w = 120; c->h = 16; str_copy(c->text, "Label",    AX_TEXT_LEN); }
    else if (type == AX_CTRL_TEXTBOX)  { c->w = 160; c->h = 22; str_copy(c->text, "",         AX_TEXT_LEN); }
    else                               { c->w = 120; c->h = 18; str_copy(c->text, "Checkbox",  AX_TEXT_LEN); }
}

/* ---- Draw control on design canvas ---- */
static void draw_ctrl_canvas(ax_ctrl_t *c, int fox, int foy, int sel) {
    int cx = fox + c->x;
    int cy = foy + c->y;
    int ty = cy + (c->h - FONT_HEIGHT) / 2;

    switch (c->type) {
    case AX_CTRL_BUTTON:
        gfx_fill_rect(cx, cy, c->w, c->h, COL_CTRL_BTN);
        draw_rect_out(cx, cy, c->w, c->h, gfx_rgb(28, 78, 172));
        str_center(cx, ty, c->w, c->text, gfx_rgb(255,255,255), COL_CTRL_BTN);
        break;
    case AX_CTRL_LABEL:
        gfx_draw_string(cx, ty, c->text, COL_CTRL_TXT, COL_FORM_BG);
        break;
    case AX_CTRL_TEXTBOX:
        gfx_fill_rect(cx, cy, c->w, c->h, COL_FIELD_BG);
        draw_rect_out(cx, cy, c->w, c->h, gfx_rgb(130, 155, 200));
        if (c->text[0])
            gfx_draw_string(cx+4, ty, c->text, gfx_rgb(150,162,185), COL_FIELD_BG);
        break;
    case AX_CTRL_CHECKBOX: {
        int bx = cx, by = cy + (c->h - 14) / 2;
        gfx_fill_rect(bx, by, 14, 14, COL_FIELD_BG);
        draw_rect_out(bx, by, 14, 14, gfx_rgb(100, 120, 165));
        gfx_draw_string(cx + 18, ty, c->text, COL_CTRL_TXT, COL_FORM_BG);
        break;
    }
    }

    if (sel) {
        draw_rect_out(cx - 2, cy - 2, c->w + 4, c->h + 4, COL_SEL);
        draw_rect_out(cx - 3, cy - 3, c->w + 6, c->h + 6, gfx_rgb(180,100,0));
    }
}

/* ---- Draw one logic node ---- */
static void draw_lnode(int ni, int cleft, int ctop) {
    ax_logic_node_t *n = &g_proj.lnodes[ni];
    if (!n->active) return;
    int nx = cleft + (int)n->canvas_x;
    int ny = ctop  + (int)n->canvas_y;
    uint32_t col = ln_color(n->type);

    gfx_fill_rect(nx, ny, LN_W, LN_H, col);
    if (ni == g_lsel)
        draw_rect_out(nx-2, ny-2, LN_W+4, LN_H+4, COL_SEL);
    else
        draw_rect_out(nx, ny, LN_W, LN_H, gfx_rgb(0,0,0));

    /* Title */
    gfx_draw_string(nx + 6, ny + 4, g_ln_disp[n->type], COL_LN_TXT, col);

    /* Info line */
    char info[40];
    info[0] = '\0';
    if (n->type == LN_TRIG_CLICK || n->type == LN_TRIG_SUBMIT || n->type == LN_TRIG_TOGGLE
        || n->type == LN_ACT_SHOW || n->type == LN_ACT_HIDE
        || n->type == LN_ACT_ENABLE || n->type == LN_ACT_DISABLE
        || n->type == LN_COND_IF_CHECKED) {
        str_copy(info, ctrl_disp_name(0, (int)n->param[0]), sizeof(info));
    } else if (n->type == LN_ACT_SET_TEXT) {
        str_copy(info, ctrl_disp_name(0, (int)n->param[0]), sizeof(info));
        str_cat(info, "=", sizeof(info));
        str_cat(info, n->str[0] ? n->str : "...", sizeof(info));
    } else if (n->type == LN_ACT_SCENE) {
        if ((int)n->param[0] >= 0 && (int)n->param[0] < g_proj.scene_count)
            str_copy(info, g_proj.scenes[(int)n->param[0]].name, sizeof(info));
    } else if (n->type == LN_COND_IF_TEXT_EQ) {
        str_copy(info, ctrl_disp_name(0, (int)n->param[0]), sizeof(info));
        str_cat(info, "=", sizeof(info));
        str_cat(info, n->str, sizeof(info));
    }
    if (info[0])
        gfx_draw_string(nx + 6, ny + 24, info, COL_LN_INFO, col);

    /* Input port (left side, middle) */
    if (ln_has_input(n->type)) {
        int px, py; port_pos(ni, 0, 0, cleft, ctop, &px, &py);
        draw_circle(px, py, PORT_R, COL_PORT_BDR, COL_PORT_BG);
    }
    /* Output ports (right side) */
    int nout = ln_out_count(n->type);
    for (int p = 0; p < nout; p++) {
        int px, py; port_pos(ni, 1, p, cleft, ctop, &px, &py);
        draw_circle(px, py, PORT_R, COL_PORT_BDR, COL_PORT_BG);
        if (nout == 2) {
            const char *lbl = (p == 0) ? "T" : "F";
            gfx_draw_string(px + PORT_R + 2, py - FONT_HEIGHT/2, lbl,
                            gfx_rgb(220,240,160), COL_LOGIC_BG);
        }
    }
}

/* ---- Save/Load buffer helpers ---- */
static void sb_c(char c)      { if (g_sbi < 8190) g_save_buf[g_sbi++] = c; }
static void sb_s(const char *s) { while (*s && g_sbi < 8190) sb_c(*s++); }
static void sb_nl(void)       { sb_c('\n'); }
static void sb_sp(void)       { sb_c(' ');  }
static void sb_i(int v) {
    char b[12];
    if (v < 0) { sb_c('-'); v = -v; }
    u32_to_dec((uint32_t)v, b, sizeof(b));
    sb_s(b);
}
static void sb_q(const char *s) {
    sb_c('"');
    while (*s && g_sbi < 8190) { if (*s == '"') sb_c('\\'); sb_c(*s++); }
    sb_c('"');
}

static void studio_save(void) {
    g_sbi = 0;
    sb_s("ax_studio 1"); sb_nl();
    sb_s("form "); sb_q(g_proj.title); sb_sp(); sb_i(g_proj.form_w); sb_sp(); sb_i(g_proj.form_h); sb_nl();
    sb_s("scene_count "); sb_i(g_proj.scene_count); sb_nl();

    for (int si = 0; si < g_proj.scene_count; si++) {
        ax_scene_t *sc = &g_proj.scenes[si];
        sb_s("scene "); sb_i(si); sb_sp(); sb_q(sc->name); sb_nl();
        for (int ci = 0; ci < sc->ctrl_count; ci++) {
            ax_ctrl_t *c = &sc->ctrls[ci];
            sb_s("ctrl "); sb_i(ci); sb_sp();
            sb_s(g_ctrl_save[c->type]); sb_sp();
            sb_i(c->x); sb_sp(); sb_i(c->y); sb_sp();
            sb_i(c->w); sb_sp(); sb_i(c->h); sb_sp();
            sb_q(c->text); sb_nl();
        }
    }

    sb_s("lnode_count "); sb_i(g_proj.lnode_count); sb_nl();
    for (int i = 0; i < g_proj.lnode_count; i++) {
        ax_logic_node_t *n = &g_proj.lnodes[i];
        if (!n->active) continue;
        sb_s("lnode "); sb_i(i); sb_sp();
        sb_s(g_ln_save[n->type]); sb_sp();
        sb_i((int)n->canvas_x); sb_sp(); sb_i((int)n->canvas_y); sb_sp();
        sb_i((int)n->param[0]); sb_sp(); sb_i((int)n->param[1]); sb_sp();
        sb_q(n->str); sb_nl();
    }

    sb_s("lwire_count "); sb_i(g_proj.lwire_count); sb_nl();
    for (int i = 0; i < g_proj.lwire_count; i++) {
        ax_logic_wire_t *w = &g_proj.lwires[i];
        if (!w->active) continue;
        sb_s("lwire "); sb_i((int)w->from_node); sb_sp();
        sb_i((int)w->from_port); sb_sp(); sb_i((int)w->to_node); sb_nl();
    }
    g_save_buf[g_sbi] = '\0';
    vfs_write(g_filepath, (const uint8_t*)g_save_buf, (uint32_t)g_sbi);
}

/* ---- Load/parse ---- */
static const char *g_pp;

static void pp_skip_ws(void)  { while (*g_pp == ' ' || *g_pp == '\t') g_pp++; }
static void pp_skip_line(void){ while (*g_pp && *g_pp != '\n') g_pp++; if (*g_pp=='\n') g_pp++; }

static int pp_int(int *out) {
    pp_skip_ws();
    int neg = 0;
    if (*g_pp == '-') { neg = 1; g_pp++; }
    if (*g_pp < '0' || *g_pp > '9') return 0;
    int v = 0;
    while (*g_pp >= '0' && *g_pp <= '9') v = v*10 + (*g_pp++ - '0');
    *out = neg ? -v : v;
    return 1;
}

static int pp_word(char *out, int maxlen) {
    pp_skip_ws();
    int i = 0;
    while (*g_pp && *g_pp != ' ' && *g_pp != '\t' && *g_pp != '\n' && i < maxlen-1)
        out[i++] = *g_pp++;
    out[i] = '\0';
    return i > 0;
}

static int pp_quoted(char *out, int maxlen) {
    pp_skip_ws();
    if (*g_pp != '"') return pp_word(out, maxlen);
    g_pp++;
    int i = 0;
    while (*g_pp && *g_pp != '"' && i < maxlen-1) {
        if (*g_pp == '\\') g_pp++;
        out[i++] = *g_pp++;
    }
    if (*g_pp == '"') g_pp++;
    out[i] = '\0';
    return 1;
}

static int pp_kw(const char *kw) {
    pp_skip_ws();
    int klen = (int)str_len(kw);
    if (str_ncmp(g_pp, kw, (size_t)klen) != 0) return 0;
    char next = g_pp[klen];
    if (next != ' ' && next != '\t' && next != '\n' && next != '\0') return 0;
    g_pp += klen;
    return 1;
}

static int ln_type_from_str(const char *s) {
    for (int i = 0; i < LN_TYPE_COUNT; i++)
        if (str_eq(g_ln_save[i], s)) return i;
    return -1;
}

static int ctrl_type_from_str(const char *s) {
    for (int i = 0; i < 4; i++)
        if (str_eq(g_ctrl_save[i], s)) return i;
    return -1;
}

static void studio_load(const char *path) {
    static uint8_t lbuf[8192];
    int n = vfs_cat(path, lbuf, 8191);
    if (n <= 0) return;
    lbuf[n] = '\0';
    g_pp = (const char *)lbuf;

    mem_set(&g_proj, 0, sizeof(g_proj));
    str_copy(g_proj.title, "Untitled", AX_TITLE_LEN);
    g_proj.form_w = 400; g_proj.form_h = 300;

    while (*g_pp) {
        pp_skip_ws();
        if (*g_pp == '\n') { g_pp++; continue; }
        if (*g_pp == '\0') break;

        if (pp_kw("ax_studio")) { pp_skip_line(); continue; }

        if (pp_kw("form")) {
            pp_quoted(g_proj.title, AX_TITLE_LEN);
            pp_int(&g_proj.form_w);
            pp_int(&g_proj.form_h);
            pp_skip_line(); continue;
        }
        if (pp_kw("scene_count")) {
            pp_int(&g_proj.scene_count);
            if (g_proj.scene_count < 1) g_proj.scene_count = 1;
            if (g_proj.scene_count > AX_MAX_SCENES) g_proj.scene_count = AX_MAX_SCENES;
            pp_skip_line(); continue;
        }
        if (pp_kw("scene")) {
            int si;
            if (!pp_int(&si) || si < 0 || si >= AX_MAX_SCENES) { pp_skip_line(); continue; }
            g_scene = si;
            pp_quoted(g_proj.scenes[si].name, AX_TITLE_LEN);
            pp_skip_line(); continue;
        }
        if (pp_kw("ctrl")) {
            /* ctrl <idx> <type_str> <x> <y> <w> <h> "<text>" */
            int ci, x, y, w, h; char tstr[16], txt[AX_TEXT_LEN];
            if (!pp_int(&ci)) { pp_skip_line(); continue; }
            pp_word(tstr, sizeof(tstr));
            int ct = ctrl_type_from_str(tstr);
            if (ct < 0) { pp_skip_line(); continue; }
            pp_int(&x); pp_int(&y); pp_int(&w); pp_int(&h);
            pp_quoted(txt, AX_TEXT_LEN);
            /* find scene for this ctrl — stored in most recently parsed scene */
            /* We need to track which scene we're in. Let's use scene_count-1 heuristic:
               walk through scenes to find which one to append to */
            /* Actually, we need the current scene context.
               Track it with a local variable: use g_scene as we parse sequentially. */
            /* Use g_scene (set when we parsed "scene N") */
            ax_scene_t *sc = &g_proj.scenes[g_scene];
            if (ci >= 0 && ci < AX_MAX_CTRLS) {
                sc->ctrls[ci].type = (ax_ctrl_type_t)ct;
                sc->ctrls[ci].x = x; sc->ctrls[ci].y = y;
                sc->ctrls[ci].w = w; sc->ctrls[ci].h = h;
                str_copy(sc->ctrls[ci].text, txt, AX_TEXT_LEN);
                if (ci + 1 > sc->ctrl_count) sc->ctrl_count = ci + 1;
            }
            pp_skip_line(); continue;
        }
        if (pp_kw("lnode_count")) { pp_int(&g_proj.lnode_count); pp_skip_line(); continue; }
        if (pp_kw("lnode")) {
            int idx, cx, cy, p0, p1; char tstr[32], sv[AX_TEXT_LEN];
            if (!pp_int(&idx) || idx < 0 || idx >= LN_MAX) { pp_skip_line(); continue; }
            pp_word(tstr, sizeof(tstr));
            int lt = ln_type_from_str(tstr);
            if (lt < 0) { pp_skip_line(); continue; }
            pp_int(&cx); pp_int(&cy); pp_int(&p0); pp_int(&p1);
            pp_quoted(sv, AX_TEXT_LEN);
            ax_logic_node_t *nd = &g_proj.lnodes[idx];
            nd->active   = 1;
            nd->type     = (uint8_t)lt;
            nd->canvas_x = (int16_t)cx;
            nd->canvas_y = (int16_t)cy;
            nd->param[0] = (int16_t)p0;
            nd->param[1] = (int16_t)p1;
            str_copy(nd->str, sv, AX_TEXT_LEN);
            pp_skip_line(); continue;
        }
        if (pp_kw("lwire_count")) { pp_int(&g_proj.lwire_count); pp_skip_line(); continue; }
        if (pp_kw("lwire")) {
            int fn, fp, tn;
            if (!pp_int(&fn) || !pp_int(&fp) || !pp_int(&tn)) { pp_skip_line(); continue; }
            /* find free slot */
            for (int i = 0; i < LW_MAX; i++) {
                if (!g_proj.lwires[i].active) {
                    g_proj.lwires[i].active    = 1;
                    g_proj.lwires[i].from_node = (int16_t)fn;
                    g_proj.lwires[i].from_port = (uint8_t)fp;
                    g_proj.lwires[i].to_node   = (int16_t)tn;
                    break;
                }
            }
            pp_skip_line(); continue;
        }
        /* scene context for ctrl parsing */
        {
            /* check if this is a "scene N" line we missed */
            pp_skip_line();
        }
    }
    /* ensure scene_count consistent */
    if (g_proj.scene_count < 1) g_proj.scene_count = 1;
}

/* ---- Project reset ---- */
static void project_new(void) {
    mem_set(&g_proj, 0, sizeof(g_proj));
    str_copy(g_proj.title,           "Untitled",  AX_TITLE_LEN);
    str_copy(g_proj.scenes[0].name,  "Scene 1",   AX_TITLE_LEN);
    g_proj.form_w    = 400;
    g_proj.form_h    = 300;
    g_proj.scene_count = 1;
    g_scene    = 0;
    g_selected = -1;
    g_lsel     = -1;
    g_tool     = -1;
    g_placing  = -1;
    g_wiring   = 0;
    str_copy(g_filepath, "/ROOT/UNTITLED.AX", sizeof(g_filepath));
    g_edit_buf[0] = '\0'; g_edit_len = 0; g_edit_mode = 0;
}

/* ---- on_paint ---- */
static void studio_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);

    int tb_y    = r.y;
    int main_y  = r.y + STUDIO_TB_H;
    int main_h  = r.h - STUDIO_TB_H - STUDIO_PROPS_H;
    int props_y = r.y + r.h - STUDIO_PROPS_H;
    int tbx_x   = r.x;
    int tbx_w   = STUDIO_TBX_W;
    int cv_x    = r.x + tbx_w;
    int cv_w    = r.w - tbx_w;

    /* ---- Top bar ---- */
    gfx_fill_rect(r.x, tb_y, r.w, STUDIO_TB_H, COL_TB_BG);
    draw_btn(r.x,      tb_y + 2, 54, STUDIO_TB_H-4, "Design", g_view == 0);
    draw_btn(r.x + 54, tb_y + 2, 50, STUDIO_TB_H-4, "Logic",  g_view == 1);
    gfx_fill_rect(r.x + 108, tb_y + 4, 2, STUDIO_TB_H-8, gfx_rgb(60,75,105));
    draw_btn(r.x + 114, tb_y + 2, 36, STUDIO_TB_H-4, "New",  0);
    draw_btn(r.x + 153, tb_y + 2, 44, STUDIO_TB_H-4, "Open", 0);
    draw_btn(r.x + 200, tb_y + 2, 44, STUDIO_TB_H-4, "Save", 0);
    draw_btn(r.x + 247, tb_y + 2, 40, STUDIO_TB_H-4, "Run",  0);
    gfx_draw_string(r.x + 295, tb_y + 8, "App:", gfx_rgb(180,200,240), COL_TB_BG);
    int tf_x = r.x + 328, tf_w = r.w - 328 - 4;
    gfx_fill_rect(tf_x, tb_y + 4, tf_w, STUDIO_TB_H-8, COL_FIELD_BG);
    draw_rect_out(tf_x, tb_y + 4, tf_w, STUDIO_TB_H-8,
                  g_edit_mode == 2 ? COL_FIELD_FBDR : COL_FIELD_BDR);
    gfx_draw_string(tf_x + 3, tb_y + 8, g_proj.title, COL_FIELD_TXT, COL_FIELD_BG);

    /* ---- Props bar ---- */
    gfx_fill_rect(r.x, props_y, r.w, STUDIO_PROPS_H, COL_PROPS_BG);
    if (g_edit_mode == 3) {
        gfx_draw_string(r.x + 4, props_y + 5, "Open:", COL_PROPS_TXT, COL_PROPS_BG);
        int px = r.x + 44, pw = r.w - 48;
        gfx_fill_rect(px, props_y + 3, pw, STUDIO_PROPS_H-6, COL_FIELD_BG);
        draw_rect_out(px, props_y + 3, pw, STUDIO_PROPS_H-6, COL_FIELD_FBDR);
        gfx_draw_string(px + 3, props_y + 5, g_edit_buf, COL_FIELD_TXT, COL_FIELD_BG);
    } else if (g_placing >= 0) {
        gfx_draw_string(r.x + 4, props_y + 5, "Click logic canvas to place:", COL_PROPS_TXT, COL_PROPS_BG);
        gfx_draw_string(r.x + 240, props_y + 5, g_ln_disp[g_placing], gfx_rgb(255,220,80), COL_PROPS_BG);
    } else if (g_view == 0 && g_selected >= 0) {
        /* Design: edit ctrl text */
        ax_scene_t *sc = cur_scene();
        gfx_draw_string(r.x + 4, props_y + 5, "Text:", COL_PROPS_TXT, COL_PROPS_BG);
        int px = r.x + 44, pw = r.w - 48;
        gfx_fill_rect(px, props_y + 3, pw, STUDIO_PROPS_H-6, COL_FIELD_BG);
        draw_rect_out(px, props_y + 3, pw, STUDIO_PROPS_H-6,
                      g_edit_mode == 0 ? COL_FIELD_FBDR : COL_FIELD_BDR);
        const char *ptxt = (sc && g_selected < sc->ctrl_count)
                           ? sc->ctrls[g_selected].text : g_edit_buf;
        gfx_draw_string(px + 3, props_y + 5,
                        g_edit_mode == 0 ? g_edit_buf : ptxt,
                        COL_FIELD_TXT, COL_FIELD_BG);
    } else if (g_view == 1 && g_lsel >= 0) {
        /* Logic: edit node str param */
        ax_logic_node_t *nd = &g_proj.lnodes[g_lsel];
        /* Show param[0] with arrows */
        char pinfo[32]; pinfo[0] = '\0';
        sb_s("Ctrl:"); /* reuse to build pinfo inline */
        /* Actually build pinfo cleanly: */
        pinfo[0] = '\0';
        str_copy(pinfo, "< Ctrl:", sizeof(pinfo));
        char pnum[8]; u32_to_dec((uint32_t)(int)nd->param[0], pnum, sizeof(pnum));
        str_cat(pinfo, pnum, sizeof(pinfo));
        str_cat(pinfo, " >", sizeof(pinfo));
        gfx_draw_string(r.x + 4, props_y + 5, pinfo, COL_PROPS_TXT, COL_PROPS_BG);
        /* String param field */
        gfx_draw_string(r.x + 140, props_y + 5, "Str:", COL_PROPS_TXT, COL_PROPS_BG);
        int px = r.x + 176, pw = r.w - 180;
        gfx_fill_rect(px, props_y + 3, pw, STUDIO_PROPS_H-6, COL_FIELD_BG);
        draw_rect_out(px, props_y + 3, pw, STUDIO_PROPS_H-6,
                      g_edit_mode == 1 ? COL_FIELD_FBDR : COL_FIELD_BDR);
        gfx_draw_string(px + 3, props_y + 5,
                        g_edit_mode == 1 ? g_edit_buf : nd->str,
                        COL_FIELD_TXT, COL_FIELD_BG);
    } else {
        /* Default hint */
        gfx_draw_string(r.x + 4, props_y + 5,
                        g_view==0 ? "Click ctrl to select | Del to remove | Arrow keys move"
                                  : "Click palette to choose block | Click canvas to place | Drag ports to wire",
                        gfx_rgb(140, 155, 185), COL_PROPS_BG);
    }

    /* ---- View-specific ---- */
    if (g_view == 0) {
        /* ---- Design view ---- */

        /* Toolbox */
        gfx_fill_rect(tbx_x, main_y, tbx_w, main_h, COL_TBX_BG);
        static const char * const tool_labels[] = {
            ">Select", "Button", "Label", "TextBox", "Checkbox"
        };
        for (int i = 0; i < 5; i++) {
            int ty = main_y + 2 + i * (TOOL_H + 2);
            int active = (i == 0 && g_tool == -1) || (i > 0 && g_tool == i-1);
            uint32_t bg = active ? COL_TBX_SEL : COL_TBX_BG;
            gfx_fill_rect(tbx_x, ty, tbx_w-2, TOOL_H, bg);
            gfx_draw_string(tbx_x + 4, ty + (TOOL_H - FONT_HEIGHT)/2,
                            tool_labels[i], COL_TBX_TXT, bg);
        }

        /* Scene tabs */
        int stab_y = main_y;
        gfx_fill_rect(cv_x, stab_y, cv_w, SCENE_TAB_H, COL_SCENETAB);
        int stab_x = cv_x;
        for (int si = 0; si < g_proj.scene_count; si++) {
            uint32_t tbg = (si == g_scene) ? COL_SCENETAB_A : COL_SCENETAB;
            gfx_fill_rect(stab_x, stab_y, SCENE_TAB_W-2, SCENE_TAB_H-1, tbg);
            draw_rect_out(stab_x, stab_y, SCENE_TAB_W-2, SCENE_TAB_H-1, gfx_rgb(20,28,45));
            /* scene name (truncated) */
            char sname[12];
            str_copy(sname, g_proj.scenes[si].name, 10);
            gfx_draw_string(stab_x + 3, stab_y + 3, sname, COL_SCENETAB_T, tbg);
            /* × button */
            if (g_proj.scene_count > 1) {
                gfx_draw_string(stab_x + SCENE_TAB_W - 14, stab_y + 3, "x",
                                gfx_rgb(220,100,100), tbg);
            }
            stab_x += SCENE_TAB_W;
        }
        /* [+] add scene */
        if (g_proj.scene_count < AX_MAX_SCENES) {
            gfx_fill_rect(stab_x, stab_y, 22, SCENE_TAB_H-1, COL_SCENETAB);
            draw_rect_out(stab_x, stab_y, 22, SCENE_TAB_H-1, gfx_rgb(20,28,45));
            gfx_draw_string(stab_x + 5, stab_y + 3, "+", gfx_rgb(120,220,120), COL_SCENETAB);
        }

        /* Canvas */
        int can_y  = main_y + SCENE_TAB_H;
        int can_h  = main_h - SCENE_TAB_H;
        gfx_fill_rect(cv_x, can_y, cv_w, can_h, COL_CANVAS_BG);

        int fox = cv_x + 8;
        int foy = can_y + 8;
        /* Form boundary */
        gfx_fill_rect(fox, foy, g_proj.form_w, g_proj.form_h, COL_FORM_BG);
        draw_rect_out(fox, foy, g_proj.form_w, g_proj.form_h, COL_FORM_BDR);

        /* Controls */
        ax_scene_t *sc = cur_scene();
        if (sc) {
            for (int ci = 0; ci < sc->ctrl_count; ci++)
                draw_ctrl_canvas(&sc->ctrls[ci], fox, foy, ci == g_selected);
        }

    } else {
        /* ---- Logic view ---- */
        int cleft = cv_x;
        int ctop  = main_y;

        /* Palette */
        gfx_fill_rect(tbx_x, main_y, tbx_w, main_h, COL_PAL_BG);
        int py = main_y + 2;
        for (int pi = 0; pi < PAL_ITEMS_COUNT; pi++) {
            int item = g_pal_items[pi];
            if (item < 0) {
                /* Section header */
                static const char * const headers[] = {"Triggers","Actions","Conditions"};
                int hdr = (-item) - 1;
                if (hdr >= 0 && hdr < 3) {
                    gfx_fill_rect(tbx_x, py, tbx_w, 16, COL_PAL_BG);
                    gfx_draw_string(tbx_x + 2, py, headers[hdr], COL_PAL_HDR, COL_PAL_BG);
                    py += 16;
                }
            } else {
                int active = (g_placing == item);
                uint32_t bg = active ? COL_PAL_SEL : COL_PAL_BG;
                gfx_fill_rect(tbx_x, py, tbx_w-2, TOOL_H, bg);
                gfx_draw_string(tbx_x + 3, py + (TOOL_H-FONT_HEIGHT)/2,
                                g_ln_disp[item], COL_PAL_TXT, bg);
                py += TOOL_H + 1;
            }
        }

        /* Logic canvas */
        gfx_fill_rect(cleft, ctop, cv_w, main_h, COL_LOGIC_BG);

        /* Wires */
        for (int i = 0; i < LW_MAX; i++) {
            ax_logic_wire_t *w = &g_proj.lwires[i];
            if (!w->active) continue;
            int fn = (int)w->from_node, tn = (int)w->to_node;
            if (fn < 0 || fn >= LN_MAX || tn < 0 || tn >= LN_MAX) continue;
            if (!g_proj.lnodes[fn].active || !g_proj.lnodes[tn].active) continue;
            int px0, py0, px1, py1;
            port_pos(fn, 1, (int)w->from_port, cleft, ctop, &px0, &py0);
            port_pos(tn, 0, 0, cleft, ctop, &px1, &py1);
            draw_wire(px0, py0, px1, py1, COL_WIRE);
        }

        /* Nodes */
        for (int i = 0; i < LN_MAX; i++)
            draw_lnode(i, cleft, ctop);

        /* Rubber-band wire */
        if (g_wiring && g_wsrc >= 0) {
            int px0, py0;
            port_pos(g_wsrc, 1, g_wsrcp, cleft, ctop, &px0, &py0);
            draw_line(px0, py0, g_wex, g_wey, COL_WIRE_RB);
        }
    }
}

/* ---- on_mouse ---- */
static void studio_on_mouse(int win_id, int mx, int my, uint8_t buttons) {
    gui_rect_t r = gui_window_content(win_id);
    int tb_y     = r.y;
    int main_y   = r.y + STUDIO_TB_H;
    int main_h   = r.h - STUDIO_TB_H - STUDIO_PROPS_H;
    int props_y  = r.y + r.h - STUDIO_PROPS_H;
    int tbx_w    = STUDIO_TBX_W;
    int cv_x     = r.x + tbx_w;
    int cv_w     = r.w - tbx_w;

    int pressed  = (buttons & 1) && !(buttons >> 4 & 1);  /* left button down */
    int released = (buttons >> 4 & 1);                     /* just released */

    /* Ignore mouse-up + move outside if not dragging/wiring */
    if (!buttons && !g_dragging && !g_wiring) return;

    /* ---- Top bar ---- */
    if (buttons && my >= tb_y && my < tb_y + STUDIO_TB_H) {
        if (!pressed) return;
        /* View tabs */
        if (mx >= r.x && mx < r.x + 54)       { g_view = 0; g_selected=-1; g_lsel=-1; gui_repaint(); return; }
        if (mx >= r.x+54 && mx < r.x+104)     { g_view = 1; g_selected=-1; gui_repaint(); return; }
        /* Action buttons */
        if (mx >= r.x+114 && mx < r.x+150)    { project_new(); gui_repaint(); return; }
        if (mx >= r.x+153 && mx < r.x+197)    {
            g_edit_mode = 3;
            str_copy(g_edit_buf, g_filepath, sizeof(g_edit_buf));
            g_edit_len = (int)str_len(g_edit_buf);
            gui_repaint(); return;
        }
        if (mx >= r.x+200 && mx < r.x+244)    { studio_save(); gui_repaint(); return; }
        if (mx >= r.x+247 && mx < r.x+287)    { axapp_gui_launch(&g_proj); return; }
        /* Title field */
        if (mx >= r.x+328) {
            g_edit_mode = 2;
            str_copy(g_edit_buf, g_proj.title, AX_TEXT_LEN);
            g_edit_len = (int)str_len(g_edit_buf);
            gui_repaint(); return;
        }
        return;
    }

    /* ---- Props bar ---- */
    if (buttons && my >= props_y) {
        if (!pressed) return;
        if (g_view == 0 && g_selected >= 0) {
            ax_scene_t *sc = cur_scene();
            if (sc && g_selected < sc->ctrl_count) {
                g_edit_mode = 0;
                str_copy(g_edit_buf, sc->ctrls[g_selected].text, AX_TEXT_LEN);
                g_edit_len = (int)str_len(g_edit_buf);
            }
        } else if (g_view == 1 && g_lsel >= 0) {
            int str_x = r.x + 176;
            if (mx >= str_x) {
                g_edit_mode = 1;
                str_copy(g_edit_buf, g_proj.lnodes[g_lsel].str, AX_TEXT_LEN);
                g_edit_len = (int)str_len(g_edit_buf);
            }
        }
        gui_repaint(); return;
    }

    /* ---- Design view ---- */
    if (g_view == 0) {
        int stab_y = main_y;
        int can_y  = main_y + SCENE_TAB_H;
        int can_h  = main_h - SCENE_TAB_H;
        int fox    = cv_x + 8;
        int foy    = can_y + 8;

        /* Toolbox */
        if (buttons && mx >= r.x && mx < r.x + tbx_w && my >= main_y && my < main_y + main_h) {
            if (!pressed) return;
            int ti = (my - main_y - 2) / (TOOL_H + 2);
            if (ti == 0)       g_tool = -1;
            else if (ti <= 4)  g_tool = ti - 1;
            g_selected = -1;
            gui_repaint(); return;
        }

        /* Scene tabs */
        if (buttons && my >= stab_y && my < stab_y + SCENE_TAB_H &&
            mx >= cv_x && mx < cv_x + cv_w) {
            if (!pressed) return;
            /* Check [+] */
            int tab_end = cv_x + g_proj.scene_count * SCENE_TAB_W;
            if (mx >= tab_end && mx < tab_end + 22 && g_proj.scene_count < AX_MAX_SCENES) {
                int ns = g_proj.scene_count++;
                char sname[AX_TITLE_LEN];
                str_copy(sname, "Scene ", AX_TITLE_LEN);
                char num[4]; u32_to_dec((uint32_t)(ns+1), num, sizeof(num));
                str_cat(sname, num, AX_TITLE_LEN);
                str_copy(g_proj.scenes[ns].name, sname, AX_TITLE_LEN);
                g_scene = ns;
                g_selected = -1;
                gui_repaint(); return;
            }
            /* Check tab click / × */
            for (int si = 0; si < g_proj.scene_count; si++) {
                int tx = cv_x + si * SCENE_TAB_W;
                if (mx >= tx && mx < tx + SCENE_TAB_W) {
                    /* × button at right of tab */
                    if (mx >= tx + SCENE_TAB_W - 14 && g_proj.scene_count > 1) {
                        /* Delete scene si */
                        for (int k = si; k < g_proj.scene_count - 1; k++)
                            g_proj.scenes[k] = g_proj.scenes[k+1];
                        g_proj.scene_count--;
                        if (g_scene >= g_proj.scene_count) g_scene = g_proj.scene_count-1;
                    } else {
                        g_scene = si;
                        g_selected = -1;
                    }
                    gui_repaint(); return;
                }
            }
            return;
        }

        /* Canvas */
        if (mx >= cv_x && mx < cv_x + cv_w && my >= can_y && my < can_y + can_h) {
            ax_scene_t *sc = cur_scene();

            /* Place mode */
            if (pressed && g_tool >= 0 && sc) {
                if (sc->ctrl_count < AX_MAX_CTRLS) {
                    int ci = sc->ctrl_count++;
                    int cx = mx - fox; if (cx < 0) cx = 0;
                    int cy = my - foy; if (cy < 0) cy = 0;
                    ctrl_defaults(&sc->ctrls[ci], g_tool, cx, cy);
                    g_selected = ci;
                    g_tool = -1;
                    g_edit_mode = 0;
                    str_copy(g_edit_buf, sc->ctrls[ci].text, AX_TEXT_LEN);
                    g_edit_len = (int)str_len(g_edit_buf);
                }
                gui_repaint(); return;
            }

            /* Select / drag */
            if (pressed && sc) {
                /* Hit-test back-to-front */
                int hit = -1;
                for (int ci = sc->ctrl_count - 1; ci >= 0; ci--) {
                    ax_ctrl_t *c = &sc->ctrls[ci];
                    if (mx >= fox + c->x && mx < fox + c->x + c->w &&
                        my >= foy + c->y && my < foy + c->y + c->h) {
                        hit = ci; break;
                    }
                }
                g_selected = hit;
                if (hit >= 0) {
                    g_dragging = 1;
                    g_drag_ox = mx - (fox + sc->ctrls[hit].x);
                    g_drag_oy = my - (foy + sc->ctrls[hit].y);
                    g_edit_mode = 0;
                    str_copy(g_edit_buf, sc->ctrls[hit].text, AX_TEXT_LEN);
                    g_edit_len = (int)str_len(g_edit_buf);
                }
                gui_repaint(); return;
            }

            /* Drag motion */
            if (g_dragging && g_selected >= 0 && sc) {
                ax_ctrl_t *c = &sc->ctrls[g_selected];
                int nx = mx - fox - g_drag_ox;
                int ny = my - foy - g_drag_oy;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx + c->w > g_proj.form_w) nx = g_proj.form_w - c->w;
                if (ny + c->h > g_proj.form_h) ny = g_proj.form_h - c->h;
                c->x = nx; c->y = ny;
                gui_repaint(); return;
            }
        }

        if (released) { g_dragging = 0; }
        return;
    }

    /* ---- Logic view ---- */
    {
        int cleft = cv_x;
        int ctop  = main_y;

        /* Palette */
        if (buttons && mx >= r.x && mx < r.x + tbx_w && my >= main_y) {
            if (!pressed) return;
            int py = main_y + 2;
            for (int pi = 0; pi < PAL_ITEMS_COUNT; pi++) {
                int item = g_pal_items[pi];
                if (item < 0) { py += 16; continue; }
                if (my >= py && my < py + TOOL_H + 1) {
                    g_placing = (g_placing == item) ? -1 : item;
                    gui_repaint(); return;
                }
                py += TOOL_H + 1;
            }
            return;
        }

        /* Logic canvas */
        if (mx < cv_x || mx >= cv_x + cv_w || my < main_y || my >= main_y + main_h) {
            if (released) { g_wiring = 0; g_ldrag = 0; }
            return;
        }

        /* Place node */
        if (pressed && g_placing >= 0) {
            int idx = -1;
            for (int i = 0; i < LN_MAX; i++)
                if (!g_proj.lnodes[i].active) { idx = i; break; }
            if (idx >= 0) {
                ax_logic_node_t *nd = &g_proj.lnodes[idx];
                mem_set(nd, 0, sizeof(*nd));
                nd->active   = 1;
                nd->type     = (uint8_t)g_placing;
                nd->canvas_x = (int16_t)(mx - cleft - LN_W/2);
                nd->canvas_y = (int16_t)(my - ctop  - LN_H/2);
                if (nd->canvas_x < 0) nd->canvas_x = 0;
                if (nd->canvas_y < 0) nd->canvas_y = 0;
                if (idx >= g_proj.lnode_count) g_proj.lnode_count = idx + 1;
                g_lsel = idx;
                g_placing = -1;
                g_edit_mode = 1;
                g_edit_buf[0] = '\0'; g_edit_len = 0;
            }
            gui_repaint(); return;
        }

        /* Wiring: check output ports on button down */
        if (pressed && !g_wiring) {
            for (int i = 0; i < LN_MAX; i++) {
                if (!g_proj.lnodes[i].active) continue;
                int nout = ln_out_count(g_proj.lnodes[i].type);
                for (int p = 0; p < nout; p++) {
                    int px, py;
                    port_pos(i, 1, p, cleft, ctop, &px, &py);
                    if (near_port(px, py, mx, my)) {
                        g_wiring = 1; g_wsrc = i; g_wsrcp = p;
                        g_wex = mx; g_wey = my;
                        gui_repaint(); return;
                    }
                }
            }
        }

        /* Wiring: mouse move */
        if (g_wiring) {
            g_wex = mx; g_wey = my;
            if (released) {
                /* Check if near an input port */
                for (int i = 0; i < LN_MAX; i++) {
                    if (!g_proj.lnodes[i].active) continue;
                    if (!ln_has_input(g_proj.lnodes[i].type)) continue;
                    int px, py;
                    port_pos(i, 0, 0, cleft, ctop, &px, &py);
                    if (near_port(px, py, mx, my) && i != g_wsrc) {
                        /* Create wire */
                        int slot = -1;
                        for (int k = 0; k < LW_MAX; k++)
                            if (!g_proj.lwires[k].active) { slot = k; break; }
                        if (slot >= 0) {
                            g_proj.lwires[slot].active    = 1;
                            g_proj.lwires[slot].from_node = (int16_t)g_wsrc;
                            g_proj.lwires[slot].from_port = (uint8_t)g_wsrcp;
                            g_proj.lwires[slot].to_node   = (int16_t)i;
                            if (slot >= g_proj.lwire_count) g_proj.lwire_count = slot + 1;
                        }
                        break;
                    }
                }
                g_wiring = 0;
            }
            gui_repaint(); return;
        }

        /* Select node / drag */
        if (pressed) {
            int hit = -1;
            for (int i = LN_MAX - 1; i >= 0; i--) {
                if (!g_proj.lnodes[i].active) continue;
                ax_logic_node_t *nd = &g_proj.lnodes[i];
                int nx = cleft + (int)nd->canvas_x;
                int ny = ctop  + (int)nd->canvas_y;
                if (mx >= nx && mx < nx + LN_W && my >= ny && my < ny + LN_H) {
                    hit = i; break;
                }
            }
            g_lsel = hit;
            if (hit >= 0) {
                g_ldrag = 1;
                ax_logic_node_t *nd = &g_proj.lnodes[hit];
                g_ldrag_ox = mx - (cleft + (int)nd->canvas_x);
                g_ldrag_oy = my - (ctop  + (int)nd->canvas_y);
                g_edit_mode = 1;
                str_copy(g_edit_buf, nd->str, AX_TEXT_LEN);
                g_edit_len = (int)str_len(g_edit_buf);
            } else {
                g_edit_mode = -1;
            }
            gui_repaint(); return;
        }

        /* Node drag motion */
        if (g_ldrag && g_lsel >= 0) {
            ax_logic_node_t *nd = &g_proj.lnodes[g_lsel];
            int nx = mx - cleft - g_ldrag_ox;
            int ny = my - ctop  - g_ldrag_oy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            nd->canvas_x = (int16_t)nx;
            nd->canvas_y = (int16_t)ny;
            gui_repaint(); return;
        }

        if (released) { g_ldrag = 0; }
    }
}

/* ---- on_key ---- */
static void studio_on_key(int win_id, char key) {
    (void)win_id;

    /* Commit helpers */
    int commit_title = (g_edit_mode == 2 && (key == '\n' || key == '\r' || key == 27));
    int commit_open  = (g_edit_mode == 3 && (key == '\n' || key == '\r'));
    int commit_ctrl  = (g_edit_mode == 0 && (key == '\n' || key == '\r'));
    int commit_lstr  = (g_edit_mode == 1 && (key == '\n' || key == '\r'));

    /* Arrow keys for logic param cycling */
    if (g_view == 1 && g_lsel >= 0 && g_edit_mode == 1) {
        ax_logic_node_t *nd = &g_proj.lnodes[g_lsel];
        ax_scene_t *sc0 = (g_proj.scene_count > 0) ? &g_proj.scenes[0] : 0;
        int max_ctrl = sc0 ? sc0->ctrl_count : 0;
        int max_scene = g_proj.scene_count;

        if (key == '\x4b' /* KEY_LEFT */ || key == '\x4d' /* KEY_RIGHT */) {
            int delta = (key == '\x4b') ? -1 : 1;
            int max = (nd->type == LN_ACT_SCENE) ? max_scene : max_ctrl;
            if (max < 1) max = 1;
            nd->param[0] = (int16_t)(((int)nd->param[0] + delta + max) % max);
            gui_repaint(); return;
        }
    }

    /* Delete node or ctrl */
    if (key == 127 || key == '\x08') {
        /* Backspace in edit mode */
        if (g_edit_mode >= 0 && g_edit_len > 0) {
            g_edit_buf[--g_edit_len] = '\0';
            gui_repaint(); return;
        }
        /* Not in edit mode: delete selected item */
        if (g_view == 0 && g_selected >= 0 && g_edit_mode < 0) {
            ax_scene_t *sc = cur_scene();
            if (sc && g_selected < sc->ctrl_count) {
                for (int i = g_selected; i < sc->ctrl_count - 1; i++)
                    sc->ctrls[i] = sc->ctrls[i+1];
                sc->ctrl_count--;
                g_selected = -1;
            }
            gui_repaint(); return;
        }
        if (g_view == 1 && g_lsel >= 0 && g_edit_mode < 0) {
            g_proj.lnodes[g_lsel].active = 0;
            /* Remove wires referencing this node */
            for (int i = 0; i < LW_MAX; i++) {
                ax_logic_wire_t *w = &g_proj.lwires[i];
                if (w->active && ((int)w->from_node == g_lsel || (int)w->to_node == g_lsel))
                    w->active = 0;
            }
            g_lsel = -1;
            gui_repaint(); return;
        }
    }

    /* Escape: cancel edit or placing */
    if (key == 27) {
        if (g_placing >= 0) { g_placing = -1; gui_repaint(); return; }
        if (g_wiring) { g_wiring = 0; gui_repaint(); return; }
        if (g_edit_mode >= 0) { g_edit_mode = -1; gui_repaint(); return; }
        return;
    }

    /* Commit */
    if (commit_title) {
        str_copy(g_proj.title, g_edit_buf, AX_TITLE_LEN);
        g_edit_mode = -1; gui_repaint(); return;
    }
    if (commit_open) {
        str_copy(g_filepath, g_edit_buf, sizeof(g_filepath));
        studio_load(g_filepath);
        g_edit_mode = -1; gui_repaint(); return;
    }
    if (commit_ctrl) {
        ax_scene_t *sc = cur_scene();
        if (sc && g_selected >= 0 && g_selected < sc->ctrl_count)
            str_copy(sc->ctrls[g_selected].text, g_edit_buf, AX_TEXT_LEN);
        g_edit_mode = -1; gui_repaint(); return;
    }
    if (commit_lstr) {
        if (g_lsel >= 0)
            str_copy(g_proj.lnodes[g_lsel].str, g_edit_buf, AX_TEXT_LEN);
        g_edit_mode = -1; gui_repaint(); return;
    }

    /* Printable characters */
    if (g_edit_mode >= 0 && (unsigned char)key >= 32 && (unsigned char)key < 127) {
        if (g_edit_len < AX_TEXT_LEN - 1) {
            g_edit_buf[g_edit_len++] = key;
            g_edit_buf[g_edit_len]   = '\0';
        }
        gui_repaint();
    }
}

/* ---- on_close ---- */
static void studio_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

/* ---- Launch ---- */
void axstudio_gui_launch(void) {
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_rect_t rect;
    gui_window_suggest_rect(STUDIO_W, STUDIO_H + TITLE_BAR_HEIGHT, &rect);
    int wid = gui_window_create("AX Studio", rect.x, rect.y,
                                STUDIO_W, STUDIO_H + TITLE_BAR_HEIGHT);
    if (wid < 0) return;
    g_win_id = wid;
    project_new();

    gui_window_t *w = gui_get_window(wid);
    w->icon_kind = GUI_ICON_AXSTUDIO;
    w->on_paint  = studio_on_paint;
    w->on_key    = studio_on_key;
    w->on_mouse  = studio_on_mouse;
    w->on_close  = studio_on_close;
}
