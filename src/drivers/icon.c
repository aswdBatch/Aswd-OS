#include "drivers/icon.h"

#include "assets/icon_assets.h"
#include "drivers/gfx.h"

static int icon_abs(int v) {
    return v < 0 ? -v : v;
}

static uint32_t icon_blend(uint32_t dst, uint32_t src, uint8_t alpha) {
    uint32_t inv = 255u - alpha;
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;
    uint32_t sr = (src >> 16) & 0xFFu;
    uint32_t sg = (src >> 8) & 0xFFu;
    uint32_t sb = src & 0xFFu;
    uint32_t r = (dr * inv + sr * alpha + 127u) / 255u;
    uint32_t g = (dg * inv + sg * alpha + 127u) / 255u;
    uint32_t b = (db * inv + sb * alpha + 127u) / 255u;
    return (r << 16) | (g << 8) | b;
}

static const icon_asset_variant_t *icon_find_variant(icon_asset_id_t id, int desired_size) {
    const icon_asset_variant_t *best = 0;
    int best_diff = 0x7FFFFFFF;

    for (int i = 0; i < g_icon_asset_variant_count; i++) {
        const icon_asset_variant_t *variant = &g_icon_asset_variants[i];
        int diff;

        if (variant->id != id) continue;
        diff = icon_abs((int)variant->pixel_size - desired_size);
        if (!best || diff < best_diff ||
            (diff == best_diff && variant->pixel_size > best->pixel_size)) {
            best = variant;
            best_diff = diff;
        }
    }
    return best;
}

int icon_best_variant_size(icon_asset_id_t id, int desired_size) {
    const icon_asset_variant_t *variant = icon_find_variant(id, desired_size);
    return variant ? variant->pixel_size : 0;
}

void icon_draw(int x, int y, int size, icon_asset_id_t id, uint32_t tint) {
    const icon_asset_variant_t *variant;
    uint32_t *backbuffer;
    int sw;
    int sh;

    if (id == ICON_NONE || size <= 0) return;

    variant = icon_find_variant(id, size);
    if (!variant) return;

    backbuffer = gfx_backbuffer();
    sw = (int)gfx_width();
    sh = (int)gfx_height();
    if (!backbuffer || sw <= 0 || sh <= 0) return;

    for (int dy = 0; dy < size; dy++) {
        int sy = (dy * variant->height) / size;
        int py = y + dy;

        if (py < 0 || py >= sh) continue;
        for (int dx = 0; dx < size; dx++) {
            int sx = (dx * variant->width) / size;
            int px = x + dx;

            if (px < 0 || px >= sw) continue;

            if (variant->format == ICON_ASSET_ALPHA) {
                uint8_t alpha = variant->pixels[sy * variant->width + sx];
                uint32_t *dst;
                if (!alpha) continue;
                dst = &backbuffer[py * sw + px];
                *dst = (alpha == 255) ? tint : icon_blend(*dst, tint, alpha);
            } else {
                const uint8_t *src = &variant->pixels[(sy * variant->width + sx) * 4];
                uint8_t alpha = src[3];
                uint32_t color;
                uint32_t *dst;

                if (!alpha) continue;
                color = ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
                dst = &backbuffer[py * sw + px];
                *dst = (alpha == 255) ? color : icon_blend(*dst, color, alpha);
            }
        }
    }
}
