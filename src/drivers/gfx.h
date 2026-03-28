#pragma once

#include <stdint.h>

#include "drivers/font.h"

typedef enum { GFX_MODE_TEXT = 0, GFX_MODE_GRAPHICS = 1 } gfx_mode_t;
typedef enum {
    GFX_ASPECT_4_3 = 0,
    GFX_ASPECT_16_10,
    GFX_ASPECT_16_9,
} gfx_aspect_bucket_t;
typedef enum {
    GFX_DENSITY_COMPACT = 0,
    GFX_DENSITY_NORMAL,
    GFX_DENSITY_COMFORTABLE,
} gfx_density_t;

typedef struct {
    uint16_t framebuffer_w;
    uint16_t framebuffer_h;
    uint16_t pitch;
    uint8_t  bpp;
    gfx_aspect_bucket_t aspect;
    gfx_density_t density;
} gfx_display_profile_t;

int         gfx_init(void);
gfx_mode_t  gfx_get_mode(void);
uint16_t    gfx_width(void);
uint16_t    gfx_height(void);
const gfx_display_profile_t *gfx_display_profile(void);

void     gfx_put_pixel(int x, int y, uint32_t color);
void     gfx_put_pixel_front(int x, int y, uint32_t color);
void     gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void     gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void     gfx_fill_rect_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void     gfx_fill_rect_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right);
void     gfx_blit(int dx, int dy, int w, int h, const uint32_t *src, int src_pitch);
void     gfx_swap(void);
void     gfx_present_rect(int x, int y, int w, int h);
uint32_t *gfx_backbuffer(void);

void gfx_draw_char_role(int x, int y, char c, font_role_t role, int font_px,
                        uint32_t fg, uint32_t bg);
void gfx_draw_string_role(int x, int y, const char *str, font_role_t role, int font_px,
                          uint32_t fg, uint32_t bg);
void gfx_draw_char_role_transparent(int x, int y, char c, font_role_t role, int font_px,
                                    uint32_t fg);
void gfx_draw_string_role_transparent(int x, int y, const char *str, font_role_t role, int font_px,
                                      uint32_t fg);
int  gfx_measure_text(font_role_t role, int font_px, const char *str);
int  gfx_font_line_height(font_role_t role, int font_px);
int  gfx_font_char_width(font_role_t role, int font_px);

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);
void gfx_draw_char_transparent(int x, int y, char c, uint32_t fg);
void gfx_draw_string_transparent(int x, int y, const char *str, uint32_t fg);
void gfx_draw_char_scaled(int x, int y, char c, int font_px, uint32_t fg, uint32_t bg);
void gfx_draw_string_scaled(int x, int y, const char *str, int font_px, uint32_t fg, uint32_t bg);
void gfx_draw_char_scaled_transparent(int x, int y, char c, int font_px, uint32_t fg);
void gfx_draw_string_scaled_transparent(int x, int y, const char *str, int font_px, uint32_t fg);
int  gfx_text_width(const char *str, int font_px);
int  gfx_fit_rect_aspect(int outer_x, int outer_y, int outer_w, int outer_h,
                         int design_w, int design_h, int allow_stretch_pct,
                         int *out_x, int *out_y, int *out_w, int *out_h);

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);
void gfx_darken_screen(void);

static inline uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
