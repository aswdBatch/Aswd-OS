#pragma once

#include <stdint.h>

/* ---- Control types ---- */
typedef enum {
    AX_CTRL_BUTTON   = 0,
    AX_CTRL_LABEL    = 1,
    AX_CTRL_TEXTBOX  = 2,
    AX_CTRL_CHECKBOX = 3
} ax_ctrl_type_t;

#define AX_MAX_CTRLS   24
#define AX_MAX_SCENES   4
#define AX_TEXT_LEN    48
#define AX_TITLE_LEN   32
#define LN_MAX         48
#define LW_MAX         96

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

/* Logic node types
 * Triggers: 0 exec-inputs, 1 exec-output
 * Actions:  1 exec-input,  1 exec-output
 * Conditions: 1 exec-input, 2 exec-outputs (port 0=true, port 1=false)
 */
typedef enum {
    LN_TRIG_CLICK       = 0,  /* param[0] = ctrl_idx                          */
    LN_TRIG_START       = 1,  /* no params                                     */
    LN_TRIG_SUBMIT      = 2,  /* param[0] = textbox ctrl_idx                  */
    LN_TRIG_TOGGLE      = 3,  /* param[0] = checkbox ctrl_idx                 */
    LN_ACT_SET_TEXT     = 4,  /* param[0]=dst_ctrl, param[1]=src_type(0=lit,1=ctrl,2=eval), str=value */
    LN_ACT_SHOW         = 5,  /* param[0] = ctrl_idx                          */
    LN_ACT_HIDE         = 6,  /* param[0] = ctrl_idx                          */
    LN_ACT_SCENE        = 7,  /* param[0] = scene_idx                         */
    LN_ACT_ENABLE       = 8,  /* param[0] = ctrl_idx                          */
    LN_ACT_DISABLE      = 9,  /* param[0] = ctrl_idx                          */
    LN_COND_IF_TEXT_EQ  = 10, /* param[0]=ctrl_idx, str=compare_value         */
    LN_COND_IF_CHECKED  = 11  /* param[0] = checkbox ctrl_idx                 */
} ln_type_t;

#define LN_TYPE_COUNT 12

typedef struct {
    uint8_t active;
    uint8_t type;        /* ln_type_t */
    int16_t canvas_x, canvas_y;
    int16_t param[2];
    char    str[AX_TEXT_LEN];
} ax_logic_node_t;

typedef struct {
    uint8_t active;
    uint8_t from_port;   /* 0 or 1 */
    int16_t from_node;
    int16_t to_node;     /* exec-in port is always 0 */
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
