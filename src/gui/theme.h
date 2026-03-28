#pragma once

#include <stdint.h>

#include "drivers/gfx.h"

#define TH_BG_DEEP      gfx_rgb(15,  23,  38)
#define TH_BG_PANEL     gfx_rgb(245, 248, 252)
#define TH_BG_CONTENT   gfx_rgb(249, 250, 253)
#define TH_BG_ROW_ALT   gfx_rgb(236, 242, 250)
#define TH_BG_FIELD     gfx_rgb(243, 246, 251)
#define TH_BG_CARD      gfx_rgb(255, 255, 255)
#define TH_BG_CARD_ALT  gfx_rgb(237, 243, 251)
#define TH_BG_SIDEBAR   gfx_rgb(238, 243, 249)
#define TH_BG_TOOLBAR   gfx_rgb(28,  77, 140)
#define TH_BG_STATUS    gfx_rgb(232, 238, 247)
#define TH_ACCENT       gfx_rgb(30,  106, 196)
#define TH_ACCENT_HOT   gfx_rgb(45,  121, 224)
#define TH_ACCENT_DARK  gfx_rgb(22,  86,  164)
#define TH_TEXT         gfx_rgb(20,  31,  48)
#define TH_TEXT_DIM     gfx_rgb(96,  109, 130)
#define TH_TEXT_INVERT  gfx_rgb(255, 255, 255)
#define TH_BORDER       gfx_rgb(77,  92, 118)
#define TH_RULE         gfx_rgb(208, 218, 230)
#define TH_FIELD_EDGE   gfx_rgb(169, 186, 210)
#define TH_FIELD_FOCUS  gfx_rgb(61,  126, 218)
#define TH_STATUS_ERR   gfx_rgb(180, 52,  72)
#define TH_DANGER_BG    gfx_rgb(140, 30,  30)
#define TH_DANGER_TEXT  gfx_rgb(255, 200, 200)
#define TH_SEL_BG       gfx_rgb(34,  107, 207)
#define TH_SEL_TXT      gfx_rgb(255, 255, 255)

typedef struct {
    int gap_xs;
    int gap_sm;
    int gap_md;
    int gap_lg;
    int gap_xl;
    int min_hit;
    int button_h;
    int field_h;
    int list_row_h;
    int toolbar_h;
    int header_h;
    int status_h;
    int sidebar_w;
    int tab_h;
    int card_pad;
    int font_small;
    int font_body;
    int font_title;
    int font_hero;
    int mild_stretch_pct;
} th_metrics_t;

typedef enum {
    TH_LAYOUT_COMPACT = 0,
    TH_LAYOUT_COMFORTABLE,
    TH_LAYOUT_WIDE,
} th_layout_bucket_t;

const th_metrics_t *th_metrics(void);
void th_refresh_metrics(void);

th_layout_bucket_t th_layout_bucket_for_width(int width);
int  th_page_header_height(void);
int  th_info_strip_height(void);
void th_measure_grid(int width, int min_cell_w, int gap, int max_cols,
                     int *out_cols, int *out_cell_w);
uint8_t  th_anim_progress(uint32_t start_tick, uint32_t duration_ticks, int opening);
uint8_t  th_anim_ease(uint8_t progress);
int      th_lerp_int(int from, int to, uint8_t progress);
uint32_t th_lerp_color(uint32_t from, uint32_t to, uint8_t progress);

int  th_text_width(const char *text, int font_px);
void th_draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg, int font_px);
void th_draw_text_center(int x, int y, int w, const char *text, uint32_t fg, uint32_t bg, int font_px);
int  th_fit_aspect_rect(int outer_x, int outer_y, int outer_w, int outer_h,
                        int design_w, int design_h, int allow_stretch_pct,
                        int *out_x, int *out_y, int *out_w, int *out_h);

void th_draw_surface(int x, int y, int w, int h, uint32_t bg);
void th_draw_panel(int x, int y, int w, int h, const char *header);
void th_draw_dialog(int x, int y, int w, int h, const char *title);
void th_draw_card(int x, int y, int w, int h, const char *title, uint32_t bg, int active);
void th_draw_page_header(int x, int y, int w,
                         const char *eyebrow,
                         const char *title,
                         const char *subtitle);
void th_draw_info_strip(int x, int y, int w,
                        const char *left,
                        const char *center,
                        const char *right);
void th_draw_empty_state(int x, int y, int w, int h,
                         const char *title,
                         const char *body);
void th_draw_auth_card(int x, int y, int w, int h,
                       const char *title,
                       const char *subtitle);
void th_draw_sidebar(int x, int y, int w, int h, const char *title);
void th_draw_toolbar(int x, int y, int w, const char *title);
void th_draw_statusbar(int x, int y, int w, int h, const char *text);
void th_draw_tab(int x, int y, int w, int h, const char *label, int active);
void th_draw_list_row(int x, int y, int w, int h, const char *text, int selected);
void th_draw_button(int x, int y, int w, int h, const char *label, int hot);
void th_draw_section_header(int x, int y, int w, const char *label, uint32_t bg);
void th_draw_separator(int x, int y, int w);
void th_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg);
void th_draw_field(int x, int y, int w, const char *text, int focused, int masked);
void th_draw_table_header(int x, int y, int w, int h);
void th_draw_scrollbar(int x, int y, int h, int content_extent, int view_extent, int scroll);
