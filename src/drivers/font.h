#pragma once

#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

typedef enum {
    FONT_ROLE_UI = 0,
    FONT_ROLE_MONO,
} font_role_t;

typedef struct {
    int pixel_size;
    int line_height;
    int ascent;
    int default_advance;
} font_metrics_t;

typedef struct {
    const uint8_t *bitmap;
    int width;
    int height;
    int bearing_x;
    int bearing_y;
    int advance;
} font_glyph_t;

extern const uint8_t g_font_8x16[128 * 16];

int font_get_metrics(font_role_t role, int pixel_size, font_metrics_t *out);
int font_lookup_glyph(font_role_t role, int pixel_size, char c, font_glyph_t *out);
int font_measure_text(font_role_t role, int pixel_size, const char *text);
int font_char_advance(font_role_t role, int pixel_size);
int font_line_height(font_role_t role, int pixel_size);
int font_ascent(font_role_t role, int pixel_size);
