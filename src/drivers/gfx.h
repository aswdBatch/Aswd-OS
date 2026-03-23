#pragma once

#include <stdint.h>

typedef enum { GFX_MODE_TEXT = 0, GFX_MODE_GRAPHICS = 1 } gfx_mode_t;

int         gfx_init(void);
gfx_mode_t  gfx_get_mode(void);
uint16_t    gfx_width(void);
uint16_t    gfx_height(void);

void     gfx_put_pixel(int x, int y, uint32_t color);
void     gfx_put_pixel_front(int x, int y, uint32_t color);
void     gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void     gfx_blit(int dx, int dy, int w, int h, const uint32_t *src, int src_pitch);
void     gfx_swap(void);
void     gfx_present_rect(int x, int y, int w, int h);
uint32_t *gfx_backbuffer(void);

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg);

static inline uint32_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
