#pragma once

#include <stdint.h>

#include "drivers/gfx.h"

/* ── Palette tokens ───────────────────────────────────────────────── */

#define TH_BG_DEEP      gfx_rgb(17,  24,  39)
#define TH_BG_PANEL     gfx_rgb(244, 247, 251)
#define TH_BG_CONTENT   gfx_rgb(250, 251, 254)
#define TH_BG_ROW_ALT   gfx_rgb(238, 244, 251)
#define TH_BG_FIELD     gfx_rgb(238, 244, 251)
#define TH_ACCENT       gfx_rgb(27,  104, 188)
#define TH_ACCENT_HOT   gfx_rgb(37,  99,  235)
#define TH_ACCENT_DARK  gfx_rgb(20,  96,  162)
#define TH_TEXT         gfx_rgb(24,  35,  50)
#define TH_TEXT_DIM     gfx_rgb(100, 116, 139)
#define TH_TEXT_INVERT  gfx_rgb(255, 255, 255)
#define TH_BORDER       gfx_rgb(50,  62,  82)
#define TH_RULE         gfx_rgb(203, 213, 225)
#define TH_FIELD_EDGE   gfx_rgb(170, 192, 218)
#define TH_FIELD_FOCUS  gfx_rgb(59,  130, 246)
#define TH_STATUS_ERR   gfx_rgb(180, 52,  72)
#define TH_DANGER_BG    gfx_rgb(140, 30,  30)
#define TH_DANGER_TEXT  gfx_rgb(255, 200, 200)
#define TH_SEL_BG       gfx_rgb(37,  99,  235)
#define TH_SEL_TXT      gfx_rgb(255, 255, 255)

/* ── Draw helpers ─────────────────────────────────────────────────── */

void th_draw_panel(int x, int y, int w, int h, const char *header);
void th_draw_list_row(int x, int y, int w, int h, const char *text, int selected);
void th_draw_button(int x, int y, int w, int h, const char *label, int hot);
void th_draw_section_header(int x, int y, int w, const char *label, uint32_t bg);
void th_draw_separator(int x, int y, int w);
void th_draw_badge(int x, int y, const char *text, uint32_t bg, uint32_t fg);
void th_draw_field(int x, int y, int w, const char *text, int focused, int masked);
