#pragma once

#include <stdint.h>

#include "drivers/font.h"

typedef struct {
    uint32_t bitmap_offset;
    uint16_t width;
    uint16_t height;
    int16_t  bearing_x;
    int16_t  bearing_y;
    uint16_t advance;
} font_asset_glyph_t;

typedef struct {
    font_role_t role;
    uint8_t pixel_size;
    uint8_t line_height;
    uint8_t ascent;
    uint16_t default_advance;
    const font_asset_glyph_t *glyphs;
    const uint8_t *bitmap;
} font_asset_face_t;

extern const font_asset_face_t g_font_asset_faces[];
extern const int g_font_asset_face_count;
