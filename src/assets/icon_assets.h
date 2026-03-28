#pragma once

#include <stdint.h>

#include "drivers/icon.h"

typedef enum {
    ICON_ASSET_ALPHA = 0,
    ICON_ASSET_RGBA = 1,
} icon_asset_format_t;

typedef struct {
    icon_asset_id_t id;
    uint8_t pixel_size;
    uint8_t width;
    uint8_t height;
    uint8_t format;
    const uint8_t *pixels;
} icon_asset_variant_t;

extern const icon_asset_variant_t g_icon_asset_variants[];
extern const int g_icon_asset_variant_count;
