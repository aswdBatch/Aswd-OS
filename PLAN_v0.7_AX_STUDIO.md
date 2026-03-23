# AX Studio — Visual App Designer + Logic Graph (v0.7)

## Context
User wants a Visual Studio-like app builder for AswdOS with wire-based visual logic (like
Scratch/Blueprints). Design tab: drag-and-drop controls onto a multi-scene form canvas. Logic
tab: place trigger/action/condition blocks, drag wires between ports to define behavior. Run
opens it as a real AswdOS window. Save/load as `.ax` project files.

---

## UI Overview

### Design Tab
```
┌─ AX Studio ──────────────────────────────────────────────────┐
│ [Design][Logic]  [New][Open][Save][Run]  App:[My App_____]   │ ← 32px
├───────────┬──────────────────────────────────────────────────┤
│ Toolbox   │ [Scene 1 ×][Scene 2 ×][+]                        │
│ [►Select] │ ┌────────────────────────────────────────────┐   │
│ [Button]  │ │  [  Click Me  ]                            │   │
│ [Label]   │ │  Hello World                               │   │
│ [TextBox] │ │  [______________________]                  │   │
│ [Checkbox]│ │  ☐ I agree                                 │   │
│           │ └────────────────────────────────────────────┘   │
├───────────┴──────────────────────────────────────────────────┤
│ Text: [Click Me______________]                               │ ← 26px
└──────────────────────────────────────────────────────────────┘
```

### Logic Tab
```
┌─ AX Studio ──────────────────────────────────────────────────┐
│ [Design][Logic]  [New][Open][Save][Run]  App:[My App_____]   │
├───────────┬──────────────────────────────────────────────────┤
│ Blocks    │   Logic Canvas                                   │
│ Triggers: │  ┌────────────────────┐                         │
│ [OnClick] │  │ On Click: [btn1]  ●──────────┐               │
│ [OnStart] │  └────────────────────┘          │               │
│ [OnSubmit]│                         ┌────────●────────────┐  │
│ [OnToggle]│                         │ If Text==: [txt1]   │  │
│ Actions:  │                   true ●──>  false ●──>        │  │
│ [SetText] │                         └────────────────────┘  │
│ [Show]    │                                                   │
│ [Hide]    │                                                   │
│ [Scene]   │                                                   │
│ [Enable]  │                                                   │
│ [Disable] │                                                   │
│ [EvalExpr]│                                                   │
│ Conds:    │                                                   │
│ [IfTextEq]│                                                   │
│ [IfChecked│                                                   │
└───────────┴──────────────────────────────────────────────────┘
```

---

## Data Structures (defined in `axapp_gui.h`)

```c
typedef enum {
    AX_CTRL_BUTTON=0, AX_CTRL_LABEL, AX_CTRL_TEXTBOX, AX_CTRL_CHECKBOX
} ax_ctrl_type_t;

#define AX_MAX_CTRLS   24
#define AX_MAX_SCENES   4
#define AX_TEXT_LEN    48
#define AX_TITLE_LEN   32
#define LN_MAX         48   /* max logic nodes */
#define LW_MAX         96   /* max logic wires */

typedef struct {
    ax_ctrl_type_t type;
    int x, y, w, h;
    char text[AX_TEXT_LEN];
} ax_ctrl_t;

typedef struct {
    char      name[AX_TITLE_LEN];
    ax_ctrl_t ctrls[AX_MAX_CTRLS];
    int       ctrl_count;
} ax_scene_t;

/* Logic node types */
typedef enum {
    /* Triggers: 0 exec-in, 1 exec-out */
    LN_TRIG_CLICK = 0,  /* param[0] = ctrl_idx */
    LN_TRIG_START,      /* no params */
    LN_TRIG_SUBMIT,     /* param[0] = textbox ctrl_idx */
    LN_TRIG_TOGGLE,     /* param[0] = checkbox ctrl_idx */
    /* Actions: 1 exec-in, 1 exec-out */
    LN_ACT_SET_TEXT,    /* param[0]=dst_ctrl, param[1]=src_type(0=literal,1=ctrl_text,2=eval); str=value */
    LN_ACT_SHOW,        /* param[0]=ctrl_idx */
    LN_ACT_HIDE,        /* param[0]=ctrl_idx */
    LN_ACT_SCENE,       /* param[0]=scene_idx */
    LN_ACT_ENABLE,      /* param[0]=ctrl_idx */
    LN_ACT_DISABLE,     /* param[0]=ctrl_idx */
    /* Conditions: 1 exec-in, 2 exec-outs (port0=true, port1=false) */
    LN_COND_IF_TEXT_EQ, /* param[0]=ctrl_idx; str=compare_value */
    LN_COND_IF_CHECKED, /* param[0]=checkbox ctrl_idx */
} ln_type_t;

typedef struct {
    uint8_t  active;
    uint8_t  type;       /* ln_type_t */
    int16_t  canvas_x, canvas_y;
    int16_t  param[2];
    char     str[AX_TEXT_LEN];
} ax_logic_node_t;

typedef struct {
    uint8_t  active;
    uint8_t  from_port;  /* 0 or 1 */
    int16_t  from_node;
    int16_t  to_node;    /* exec-in port is always 0 */
} ax_logic_wire_t;

typedef struct {
    char            title[AX_TITLE_LEN];
    int             form_w, form_h;
    ax_scene_t      scenes[AX_MAX_SCENES];
    int             scene_count;
    ax_logic_node_t lnodes[LN_MAX];
    int             lnode_count;
    ax_logic_wire_t lwires[LW_MAX];
    int             lwire_count;
} ax_project_t;

void axapp_gui_launch(ax_project_t *p);
void axapp_gui_launch_file(const char *path);
```

**Size check**: ax_project_t ≈ 10 KB as static global — fine.

---

## New Files

| File | Purpose | Est. lines |
|------|---------|------------|
| `src/gui/axapp_gui.h` | Shared structs + runner API | 80 |
| `src/gui/axstudio_gui.h` | `void axstudio_gui_launch(void);` | 5 |
| `src/gui/axstudio_gui.c` | Designer: Design+Logic tabs, save/load | ~700 |
| `src/gui/axapp_gui.c` | Runner window + logic execution engine | ~350 |

## Modified Files

| File | Change |
|------|--------|
| `src/gui/gui.h` | Add `GUI_ICON_AXSTUDIO` to enum |
| `src/gui/gui.c` | Add app entry + icon color `gfx_rgb(50,110,220)` |
| `Makefile` | Add `axstudio_gui.c` + `axapp_gui.c` |
| `src/shell/commands.c` | Add `axapp` shell command |
| `src/common/config.h` | v0.7 |
| `src/common/changelog.c` | v0.7 entry |

---

## axstudio_gui.c — Designer

### Window constants
```c
#define STUDIO_WIN_W      660
#define STUDIO_WIN_H      500
#define STUDIO_TOPBAR_H    32   /* tab row + toolbar combined */
#define STUDIO_PROPS_H     26
#define STUDIO_TOOLBOX_W   90
#define SCENE_TAB_H        20
#define LN_W              160   /* logic node width */
#define LN_H               44   /* logic node height */
#define PORT_R              5   /* port circle radius */
```

### State globals
```c
static ax_project_t g_proj;
static int g_win_id = -1;
static int g_view   = 0;        /* 0=Design, 1=Logic */
static char g_filepath[64];     /* /ROOT/UNTITLED.AX */

/* Design view */
static int g_scene    = 0;      /* active scene index */
static int g_selected = -1;     /* selected ctrl index in current scene */
static int g_tool     = -1;     /* -1=select, 0-3=ctrl type to place */
static int g_dragging = 0;
static int g_drag_ox, g_drag_oy;
static int g_editing_title = 0;
static char g_prop_buf[AX_TEXT_LEN];
static int  g_prop_len;

/* Logic view */
static int g_lsel   = -1;       /* selected logic node index */
static int g_ldrag  = 0;
static int g_ldrag_ox, g_ldrag_oy;
static int g_wiring = 0;        /* 1 while dragging a wire */
static int g_wire_src = -1;     /* source node index */
static int g_wire_srcp = 0;     /* source port (0 or 1) */
static int g_wire_ex, g_wire_ey;/* rubber-band endpoint */
static int g_placing = -1;      /* ln_type_t to place next, -1=none */
```

### Internal helpers
```c
/* Bresenham line via gfx_put_pixel */
static void draw_line(int x0, int y0, int x1, int y1, uint32_t col);

/* Outline rect (4x gfx_fill_rect, 1px) */
static void draw_rect_outline(int x, int y, int w, int h, uint32_t col);

/* Render one design control on canvas */
static void draw_ctrl(ax_ctrl_t *c, int ox, int oy, int sel);

/* Render one logic node */
static void draw_lnode(int ni, int canvas_left, int canvas_top);

/* Get screen position of a port */
static void port_screen_pos(int ni, int is_output, int port_idx,
                             int canvas_left, int canvas_top, int *px, int *py);
```

### Design tab — on_paint
1. Fill toolbox strip with 4 tool buttons (Select/Button/Label/TextBox/Checkbox)
2. Draw scene tabs row (`[Scene N ×]` per scene + `[+]`)
3. Draw white form rect in canvas area
4. `for each ctrl in scenes[g_scene]` → draw_ctrl()
5. Props bar: show g_prop_buf edit field (or title if g_editing_title)

### Design tab — on_mouse
- **Toolbox zone**: set g_tool
- **Scene tabs**: switch g_scene; `×` deletes; `+` appends new scene
- **Canvas, g_tool>=0**: place ctrl at (mx−form_ox, my−form_oy), g_selected=new, g_tool=−1
- **Canvas, g_tool==−1**:
  - Button-down on ctrl → select, start drag (g_dragging=1, record offset)
  - Mouse-move + drag → update ctrl x,y (clamped to form bounds)
  - Button-up → g_dragging=0; commit g_prop_buf → selected ctrl text

### Design tab — on_key
- g_editing_title: chars + backspace + Enter/Esc edit g_proj.title
- g_selected>=0: chars + backspace edit g_prop_buf; Enter commits to ctrl→text
- Delete (no edit active + g_selected>=0): shift array, g_selected=−1

### Logic tab — on_paint
1. Fill palette with block type buttons (color-coded: green=triggers, blue=actions, orange=conditions)
2. For each active lnode: draw_lnode()
3. For each active lwire: compute port positions → draw 3-segment wire
   - `mid_x = (px0+px1)/2`; draw H+V+H segments via draw_line()
4. g_wiring: draw rubber-band from src port to (g_wire_ex, g_wire_ey)

### Logic tab — on_mouse
- **Palette zone**: g_placing = block type
- **Canvas, g_placing>=0**: allocate lnode at (mx−canvas_left, my−canvas_top), g_placing=−1
- **Canvas, near output port (within PORT_R+3)**: g_wiring=1, record g_wire_src/srcp
- **Canvas, g_wiring + near input port on mouse-up**: create lwire; g_wiring=0
- **Canvas, g_wiring + miss on mouse-up**: cancel g_wiring=0
- **Canvas, click node body**: g_lsel=ni, start node drag
- **Canvas miss**: g_lsel=−1

### Logic tab — on_key
- Delete: remove g_lsel node + invalidate wires referencing it
- Escape: g_placing=−1; g_wiring=0
- Arrow keys (g_lsel>=0): cycle param[0] (ctrl index) for triggers/actions

### Tab switching
Clicking [Design] or [Logic] sets g_view and repaints.
Both tabs share the same toolbar buttons (New/Open/Save/Run/title).

---

## axapp_gui.c — Runner + Logic Engine

### Instance pool
```c
#define AXAPP_MAX 4
static struct {
    int  active, win_id;
    ax_project_t proj;
    int  active_scene;
    int  focused_box, pressed_btn;
    uint8_t visible[AX_MAX_SCENES][AX_MAX_CTRLS]; /* init all 1 */
    uint8_t enabled[AX_MAX_SCENES][AX_MAX_CTRLS]; /* init all 1 */
    uint8_t checked[AX_MAX_CTRLS];                /* checkbox bool state */
    char    box_vals[AX_MAX_CTRLS][AX_TEXT_LEN];
    int     box_lens[AX_MAX_CTRLS];
} g_inst[AXAPP_MAX];
```

### on_paint
- Background fill + white form rect
- For each ctrl in scenes[s.active_scene]:
  - Skip if !visible[s.active_scene][i]
  - **LABEL**: gfx_draw_string
  - **BUTTON**: gfx_fill_rect (gray if !enabled), outline, centered text; darker fill if pressed_btn==i
  - **TEXTBOX**: white rect, outline (accent blue if focused_box==i), box_vals[i] text + cursor `|`
  - **CHECKBOX**: 16×16 outlined box, `x` drawn if checked[i], label text to the right

### on_mouse
- Hit-test ctrls in active scene:
  - BUTTON (if enabled): pressed_btn=i → `logic_fire(slot, LN_TRIG_CLICK, i)`
  - TEXTBOX: focused_box=i
  - CHECKBOX (if enabled): toggle checked[i] → `logic_fire(slot, LN_TRIG_TOGGLE, i)`
  - Miss: focused_box=−1; pressed_btn=−1

### on_key
- focused_box>=0: printable → append to box_vals; backspace → trim; Enter → `logic_fire(LN_TRIG_SUBMIT, focused_box)`

### Logic execution engine
```c
static void logic_fire(int si, ln_type_t ttype, int tparam) {
    for each active lnode n where n.type==ttype && n.param[0]==tparam:
        logic_exec(si, n_idx, 32);  /* max 32 steps */
}

static void logic_exec(int si, int ni, int budget) {
    if (budget <= 0 || ni < 0) return;
    ax_logic_node_t *n = &g_inst[si].proj.lnodes[ni];
    int s = g_inst[si].active_scene;

    switch ((ln_type_t)n->type) {
    case LN_ACT_SET_TEXT:
        str_copy(g_inst[si].box_vals[n->param[0]],
                 resolve_val(si, n), AX_TEXT_LEN);
        break;
    case LN_ACT_SHOW:    g_inst[si].visible[s][n->param[0]] = 1; break;
    case LN_ACT_HIDE:    g_inst[si].visible[s][n->param[0]] = 0; break;
    case LN_ACT_SCENE:   g_inst[si].active_scene = n->param[0]; break;
    case LN_ACT_ENABLE:  g_inst[si].enabled[s][n->param[0]] = 1; break;
    case LN_ACT_DISABLE: g_inst[si].enabled[s][n->param[0]] = 0; break;
    case LN_COND_IF_TEXT_EQ: {
        int eq = str_ncmp(g_inst[si].box_vals[n->param[0]], n->str, AX_TEXT_LEN)==0;
        fire_next_wire(si, ni, eq ? 0 : 1, budget-1);
        return;
    }
    case LN_COND_IF_CHECKED: {
        int ch = g_inst[si].checked[n->param[0]];
        fire_next_wire(si, ni, ch ? 0 : 1, budget-1);
        return;
    }
    default: break;
    }
    fire_next_wire(si, ni, 0, budget-1); /* follow exec-out */
}

static void fire_next_wire(int si, int from_node, int from_port, int budget) {
    for each active lwire w where w.from_node==from_node && w.from_port==from_port:
        logic_exec(si, w.to_node, budget);
}
```

### resolve_val (for LN_ACT_SET_TEXT)
```c
static const char *resolve_val(int si, ax_logic_node_t *n) {
    if (n->param[1] == 0) return n->str;                          /* literal */
    if (n->param[1] == 1) return g_inst[si].box_vals[n->param[0]]; /* ctrl text */
    if (n->param[1] == 2) {                                       /* eval expr */
        static char rbuf[20];
        int32_t v = eval_expr_str(g_inst[si].box_vals[n->param[0]]);
        /* format int into rbuf */
        return rbuf;
    }
    return "";
}
```

### Expression evaluator
Adapted from calc_gui.c: recursive descent over a static input string.
`eval_factor` / `eval_term` / `eval_expr_str(const char *s)`.
Handles: integers, `+`, `-`, `*`, `/`, `()`. Sanitized — non-matching chars → error (return 0).

### axapp_gui_launch_file
Read with `vfs_cat`, parse same format as studio save, call `axapp_gui_launch()`.

### On App Start
Immediately after launch setup, before first paint:
```c
logic_fire(slot, LN_TRIG_START, 0);
```

---

## Shell Command: `axapp`

In `src/shell/commands.c`:
```c
#include "gui/axapp_gui.h"
static void cmd_axapp(int argc, char *argv[]) {
    if (argc < 2) { console_writeln("usage: axapp <file.ax>"); return; }
    axapp_gui_launch_file(argv[1]);
}
/* table entry: {"axapp", "Open an AX app", cmd_axapp} */
```

---

## App Registration (gui.c)

```c
{"axstudio","AX Studio","AX Studio",GUI_ICON_AXSTUDIO,1,0,0,0,axstudio_gui_launch},
```
Runner NOT in app table — launched via Studio Run button or `axapp` shell command only.
AXSTUDIO icon badge: `gfx_rgb(50,110,220)`.

---

## Save File Format (`.ax` project)

```
ax_studio 1
form "My App" 400 300
scene_count 2

scene 0 "Main"
ctrl 0 label 20 20 160 16 "Enter a number:"
ctrl 1 textbox 20 44 160 22 ""
ctrl 2 button 20 80 100 26 "Calculate"
ctrl 3 checkbox 20 116 16 16 "Show work"

scene 1 "Result"
ctrl 0 label 20 20 240 16 "= ?"
ctrl 1 button 20 60 80 26 "Back"

lnode_count 4
lnode 0 trig_click 10 10 2 0 ""
lnode 1 act_set_text 200 10 0 1 ""
lnode 2 act_scene 200 60 1 0 ""
lnode 3 trig_click 10 80 5 0 ""

lwire_count 3
lwire 0 0 1
lwire 1 0 2
lwire 3 0 2
```

Node type keywords: `trig_click`, `trig_start`, `trig_submit`, `trig_toggle`,
`act_set_text`, `act_show`, `act_hide`, `act_scene`, `act_enable`, `act_disable`,
`cond_if_text_eq`, `cond_if_checked`

---

## Verification

1. `build.bat` — clean build, no warnings
2. Open AX Studio from desktop
3. **Design tab**: place Button, Label, TextBox, Checkbox. Add Scene 2 via [+]. Switch scenes.
4. **Logic tab**: place `On Click [btn]` trigger, drag wire to `Set Text [lbl] = "Done!"` action
5. Wire `Set Text` output to `Switch Scene 1` action
6. Add `On Toggle [checkbox]` → `If Checked` → true→`Enable [btn]`, false→`Disable [btn]`
7. Click Save → `/ROOT/UNTITLED.AX` in Files app
8. Click Run → scene 1 shows; checkbox toggle enables/disables button; click button → text changes + switches to scene 2
9. From terminal: `axapp /ROOT/UNTITLED.AX` → same app opens
10. Place Eval Expression block: textbox "2+3*4" → result label shows "14"
