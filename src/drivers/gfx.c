#include "drivers/gfx.h"

#include <stdint.h>

#include "boot/multiboot.h"
#include "boot/videoinfo.h"
#include "drivers/bga.h"
#include "drivers/font.h"
#include "drivers/serial.h"
#include "lib/string.h"

enum {
    GFX_MAX_W = 1600,
    GFX_MAX_H = 900,
    GFX_SCALE_FP_ONE = 1024,
};

typedef struct {
    uint16_t w;
    uint16_t h;
} gfx_mode_pref_t;

/* Covers the preferred mode chain plus common GRUB/VBE fallbacks. */
static uint32_t g_backbuffer[GFX_MAX_W * GFX_MAX_H];
static uint32_t *g_lfb;
static uint16_t g_width, g_height, g_pitch;
static gfx_mode_t g_mode = GFX_MODE_TEXT;
static gfx_display_profile_t g_profile;

static const gfx_mode_pref_t g_mode_chain[] = {
    { 1366, 768 },
    { 1280, 800 },
    { 1280, 720 },
    { 1024, 768 },
    {  800, 600 },
};

static int gfx_abs32(int v) {
    return v < 0 ? -v : v;
}

static uint32_t gfx_blend_color(uint32_t dst, uint32_t src, uint8_t alpha) {
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

static uint32_t gfx_lerp_color(uint32_t a, uint32_t b, int num, int den) {
    uint32_t ar = (a >> 16) & 0xFFu;
    uint32_t ag = (a >> 8) & 0xFFu;
    uint32_t ab = a & 0xFFu;
    uint32_t br = (b >> 16) & 0xFFu;
    uint32_t bg = (b >> 8) & 0xFFu;
    uint32_t bb = b & 0xFFu;
    uint32_t r = (ar * (uint32_t)(den - num) + br * (uint32_t)num) / (uint32_t)den;
    uint32_t g = (ag * (uint32_t)(den - num) + bg * (uint32_t)num) / (uint32_t)den;
    uint32_t bl = (ab * (uint32_t)(den - num) + bb * (uint32_t)num) / (uint32_t)den;
    return (r << 16) | (g << 8) | bl;
}

static int gfx_mode_chain_count(void) {
    return (int)(sizeof(g_mode_chain) / sizeof(g_mode_chain[0]));
}

static gfx_aspect_bucket_t gfx_pick_aspect(uint16_t w, uint16_t h) {
    int d43 = gfx_abs32((int)w * 3 - (int)h * 4);
    int d1610 = gfx_abs32((int)w * 10 - (int)h * 16);
    int d169 = gfx_abs32((int)w * 9 - (int)h * 16);

    if (d43 <= d1610 && d43 <= d169) return GFX_ASPECT_4_3;
    if (d1610 <= d169) return GFX_ASPECT_16_10;
    return GFX_ASPECT_16_9;
}

static gfx_density_t gfx_pick_density(uint16_t w, uint16_t h) {
    if (h <= 600 || w <= 800) return GFX_DENSITY_COMPACT;
    if (h <= 720 || w <= 1280) return GFX_DENSITY_NORMAL;
    return GFX_DENSITY_COMFORTABLE;
}

static int gfx_bga_mode_sane(const gfx_mode_pref_t *req, const bga_mode_info_t *mode) {
    if (!req || !mode) return 0;
    if (!mode->lfb_addr || mode->bpp != 32) return 0;
    if (mode->xres < 1 || mode->yres < 1 || mode->xres > GFX_MAX_W || mode->yres > GFX_MAX_H) return 0;
    if (mode->yres != req->h) return 0;
    if (mode->virt_width < mode->xres || mode->virt_height < mode->yres) return 0;
    if (mode->pitch_bytes < (uint32_t)mode->xres * 4u) return 0;
    if (gfx_pick_aspect(mode->xres, mode->yres) != gfx_pick_aspect(req->w, req->h)) return 0;
    if (gfx_abs32((int)mode->xres - (int)req->w) > 64) return 0;
    return 1;
}

static int gfx_commit_mode(uint32_t *lfb, uint16_t w, uint16_t h, uint16_t pitch, uint8_t bpp) {
    if (!lfb || w < 1 || h < 1 || w > GFX_MAX_W || h > GFX_MAX_H || pitch < w * 4u || bpp != 32) {
        serial_write("GFX: unsupported framebuffer mode\n");
        return 0;
    }

    g_lfb = lfb;
    g_width = w;
    g_height = h;
    g_pitch = pitch;
    g_mode = GFX_MODE_GRAPHICS;

    g_profile.framebuffer_w = w;
    g_profile.framebuffer_h = h;
    g_profile.pitch = pitch;
    g_profile.bpp = bpp;
    g_profile.aspect = gfx_pick_aspect(w, h);
    g_profile.density = gfx_pick_density(w, h);

    mem_set(g_backbuffer, 0, (uint32_t)g_width * g_height * 4);
    gfx_swap();
    return 1;
}

int gfx_init(void) {
    serial_write("GFX: init\n");
    mem_set(&g_profile, 0, sizeof(g_profile));

    /* Tier 1: BGA protected-mode ports, usually best inside QEMU. */
    if (bga_detect()) {
        bga_mode_info_t mode;

        for (int i = 0; i < gfx_mode_chain_count(); i++) {
            if (!bga_set_mode(g_mode_chain[i].w, g_mode_chain[i].h, 32, &mode)) {
                continue;
            }
            if (!gfx_bga_mode_sane(&g_mode_chain[i], &mode)) {
                serial_write("GFX: reject rounded BGA mode\n");
                continue;
            }
            if (gfx_commit_mode((uint32_t *)mode.lfb_addr, mode.xres, mode.yres,
                                (uint16_t)mode.pitch_bytes, mode.bpp)) {
                serial_write("GFX: BGA OK\n");
                return 1;
            }
        }
        serial_write("GFX: BGA mode chain failed, trying fallbacks\n");
    }

    /* Tier 2: GRUB-provided linear framebuffer. */
    if (multiboot_has_framebuffer() &&
        gfx_commit_mode((uint32_t *)multiboot_fb_addr(),
                        (uint16_t)multiboot_fb_width(),
                        (uint16_t)multiboot_fb_height(),
                        (uint16_t)multiboot_fb_pitch(),
                        multiboot_fb_bpp())) {
        serial_write("GFX: multiboot fb\n");
        return 1;
    }

    /* Tier 3: BIOS stage2 VBE mode chosen in real mode. */
    if (bootvid_available() &&
        gfx_commit_mode((uint32_t *)BOOTVID_FB_ADDR,
                        BOOTVID_WIDTH,
                        BOOTVID_HEIGHT,
                        (uint16_t)BOOTVID_PITCH,
                        BOOTVID_BPP)) {
        serial_write("GFX: stage2 VBE\n");
        return 1;
    }

    serial_write("GFX: no framebuffer\n");
    return 0;
}

gfx_mode_t gfx_get_mode(void) {
    return g_mode;
}

uint16_t gfx_width(void) {
    return g_width;
}

uint16_t gfx_height(void) {
    return g_height;
}

const gfx_display_profile_t *gfx_display_profile(void) {
    return &g_profile;
}

uint32_t *gfx_backbuffer(void) {
    return g_backbuffer;
}

void gfx_put_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= g_width || y >= g_height) return;
    g_backbuffer[y * g_width + x] = color;
}

void gfx_put_pixel_front(int x, int y, uint32_t color) {
    uint32_t *dst;

    if (x < 0 || y < 0 || x >= g_width || y >= g_height || !g_lfb) return;
    dst = (uint32_t *)((uint8_t *)g_lfb + y * g_pitch + x * 4);
    *dst = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_width ? g_width : x + w;
    int y1 = y + h > g_height ? g_height : y + h;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = &g_backbuffer[row * g_width + x0];
        for (int col = x0; col < x1; col++) {
            *dst++ = color;
        }
    }
}

void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int x0;
    int y0;
    int x1;
    int y1;

    if (alpha == 0) return;
    if (alpha == 255) {
        gfx_fill_rect(x, y, w, h, color);
        return;
    }

    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w > g_width ? g_width : x + w;
    y1 = y + h > g_height ? g_height : y + h;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = &g_backbuffer[row * g_width + x0];
        for (int col = x0; col < x1; col++) {
            *dst = gfx_blend_color(*dst, color, alpha);
            dst++;
        }
    }
}

void gfx_fill_rect_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_width ? g_width : x + w;
    int y1 = y + h > g_height ? g_height : y + h;

    if (w <= 0 || h <= 0 || x0 >= x1 || y0 >= y1) return;
    if (h == 1) {
        gfx_fill_rect(x, y, w, h, top);
        return;
    }

    for (int row = y0; row < y1; row++) {
        uint32_t color = gfx_lerp_color(top, bottom, row - y, h - 1);
        uint32_t *dst = &g_backbuffer[row * g_width + x0];
        for (int col = x0; col < x1; col++) {
            *dst++ = color;
        }
    }
}

void gfx_fill_rect_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_width ? g_width : x + w;
    int y1 = y + h > g_height ? g_height : y + h;

    if (w <= 0 || h <= 0 || x0 >= x1 || y0 >= y1) return;
    if (w == 1) {
        gfx_fill_rect(x, y, w, h, left);
        return;
    }

    for (int col = x0; col < x1; col++) {
        uint32_t color = gfx_lerp_color(left, right, col - x, w - 1);
        for (int row = y0; row < y1; row++) {
            g_backbuffer[row * g_width + col] = color;
        }
    }
}

void gfx_blit(int dx, int dy, int w, int h, const uint32_t *src, int src_pitch) {
    for (int row = 0; row < h; row++) {
        int sy = dy + row;
        if (sy < 0 || sy >= g_height) continue;
        for (int col = 0; col < w; col++) {
            int sx = dx + col;
            if (sx < 0 || sx >= g_width) continue;
            g_backbuffer[sy * g_width + sx] = src[row * src_pitch + col];
        }
    }
}

void gfx_swap(void) {
    if (!g_lfb) return;

    for (int y = 0; y < g_height; y++) {
        uint8_t *dst = (uint8_t *)g_lfb + y * g_pitch;
        uint32_t *src = &g_backbuffer[y * g_width];
        mem_copy(dst, src, (uint32_t)g_width * 4);
    }
}

void gfx_present_rect(int x, int y, int w, int h) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_width ? g_width : x + w;
    int y1 = y + h > g_height ? g_height : y + h;

    if (!g_lfb || x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int row = y0; row < y1; row++) {
        uint8_t *dst = (uint8_t *)g_lfb + row * g_pitch + x0 * 4;
        uint32_t *src = &g_backbuffer[row * g_width + x0];
        mem_copy(dst, src, (uint32_t)(x1 - x0) * 4);
    }
}

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) uc = '?';

    {
        const uint8_t *glyph = &g_font_8x16[uc * FONT_HEIGHT];

        for (int row = 0; row < FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            int py = y + row;
            if (py < 0 || py >= g_height) continue;
            for (int col = 0; col < FONT_WIDTH; col++) {
                int px = x + col;
                if (px < 0 || px >= g_width) continue;
                g_backbuffer[py * g_width + px] = (bits & (0x80u >> col)) ? fg : bg;
            }
        }
    }
}

void gfx_draw_string(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    int cx = x;

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            y += FONT_HEIGHT;
            continue;
        }
        gfx_draw_char(cx, y, str[i], fg, bg);
        cx += FONT_WIDTH;
    }
}

void gfx_draw_char_transparent(int x, int y, char c, uint32_t fg) {
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) uc = '?';

    {
        const uint8_t *glyph = &g_font_8x16[uc * FONT_HEIGHT];

        for (int row = 0; row < FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            int py = y + row;
            if (py < 0 || py >= g_height) continue;
            for (int col = 0; col < FONT_WIDTH; col++) {
                int px = x + col;
                if (px < 0 || px >= g_width) continue;
                if (bits & (0x80u >> col)) {
                    g_backbuffer[py * g_width + px] = fg;
                }
            }
        }
    }
}

void gfx_draw_string_transparent(int x, int y, const char *str, uint32_t fg) {
    int cx = x;

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            y += FONT_HEIGHT;
            continue;
        }
        gfx_draw_char_transparent(cx, y, str[i], fg);
        cx += FONT_WIDTH;
    }
}

static int gfx_scaled_char_w(int font_px) {
    int w = (font_px * FONT_WIDTH + FONT_HEIGHT - 1) / FONT_HEIGHT;
    if (w < 1) w = 1;
    return w;
}

void gfx_draw_char_scaled(int x, int y, char c, int font_px, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    int char_w;
    const uint8_t *glyph;

    if (font_px <= 0) return;
    if (uc >= 128) uc = '?';

    char_w = gfx_scaled_char_w(font_px);
    glyph = &g_font_8x16[uc * FONT_HEIGHT];

    for (int dy = 0; dy < font_px; dy++) {
        int sy = (dy * FONT_HEIGHT) / font_px;
        uint8_t bits = glyph[sy];
        int py = y + dy;
        if (py < 0 || py >= g_height) continue;
        for (int dx = 0; dx < char_w; dx++) {
            int sx = (dx * FONT_WIDTH) / char_w;
            int px = x + dx;
            if (px < 0 || px >= g_width) continue;
            g_backbuffer[py * g_width + px] = (bits & (0x80u >> sx)) ? fg : bg;
        }
    }
}

void gfx_draw_string_scaled(int x, int y, const char *str, int font_px, uint32_t fg, uint32_t bg) {
    int cx = x;
    int char_w = gfx_scaled_char_w(font_px);

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            y += font_px;
            continue;
        }
        gfx_draw_char_scaled(cx, y, str[i], font_px, fg, bg);
        cx += char_w;
    }
}

void gfx_draw_char_scaled_transparent(int x, int y, char c, int font_px, uint32_t fg) {
    unsigned char uc = (unsigned char)c;
    int char_w;
    const uint8_t *glyph;

    if (font_px <= 0) return;
    if (uc >= 128) uc = '?';

    char_w = gfx_scaled_char_w(font_px);
    glyph = &g_font_8x16[uc * FONT_HEIGHT];

    for (int dy = 0; dy < font_px; dy++) {
        int sy = (dy * FONT_HEIGHT) / font_px;
        uint8_t bits = glyph[sy];
        int py = y + dy;
        if (py < 0 || py >= g_height) continue;
        for (int dx = 0; dx < char_w; dx++) {
            int sx = (dx * FONT_WIDTH) / char_w;
            int px = x + dx;
            if (px < 0 || px >= g_width) continue;
            if (bits & (0x80u >> sx)) {
                g_backbuffer[py * g_width + px] = fg;
            }
        }
    }
}

void gfx_draw_string_scaled_transparent(int x, int y, const char *str, int font_px, uint32_t fg) {
    int cx = x;
    int char_w = gfx_scaled_char_w(font_px);

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            y += font_px;
            continue;
        }
        gfx_draw_char_scaled_transparent(cx, y, str[i], font_px, fg);
        cx += char_w;
    }
}

void gfx_draw_char_role_transparent(int x, int y, char c, font_role_t role, int font_px, uint32_t fg) {
    font_glyph_t glyph;
    font_metrics_t metrics;
    int baseline;
    int draw_x;
    int draw_y;

    if (font_px <= 0) font_px = FONT_HEIGHT;
    font_get_metrics(role, font_px, &metrics);
    if (!font_lookup_glyph(role, font_px, c, &glyph)) {
        gfx_draw_char_scaled_transparent(x, y, c, font_px, fg);
        return;
    }

    baseline = y + metrics.ascent;
    draw_x = x + glyph.bearing_x;
    draw_y = baseline - glyph.bearing_y;

    for (int row = 0; row < glyph.height; row++) {
        int py = draw_y + row;
        if (py < 0 || py >= g_height) continue;
        for (int col = 0; col < glyph.width; col++) {
            int px = draw_x + col;
            uint8_t alpha;
            uint32_t *dst;

            if (px < 0 || px >= g_width) continue;
            alpha = glyph.bitmap[row * glyph.width + col];
            if (!alpha) continue;
            dst = &g_backbuffer[py * g_width + px];
            *dst = (alpha == 255) ? fg : gfx_blend_color(*dst, fg, alpha);
        }
    }
}

void gfx_draw_char_role(int x, int y, char c, font_role_t role, int font_px,
                        uint32_t fg, uint32_t bg) {
    int advance;
    int line_h;

    if (font_px <= 0) font_px = FONT_HEIGHT;
    advance = font_char_advance(role, font_px);
    line_h = font_line_height(role, font_px);
    if (advance < 1) advance = gfx_scaled_char_w(font_px);
    if (line_h < 1) line_h = font_px;
    gfx_fill_rect(x, y, advance, line_h, bg);
    gfx_draw_char_role_transparent(x, y, c, role, font_px, fg);
}

void gfx_draw_string_role_transparent(int x, int y, const char *str, font_role_t role, int font_px,
                                      uint32_t fg) {
    int cx = x;
    int line_h;
    font_glyph_t glyph;

    if (font_px <= 0) font_px = FONT_HEIGHT;
    line_h = font_line_height(role, font_px);
    if (line_h < 1) line_h = font_px;

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            y += line_h;
            continue;
        }
        gfx_draw_char_role_transparent(cx, y, str[i], role, font_px, fg);
        if (font_lookup_glyph(role, font_px, str[i], &glyph) && glyph.advance > 0) {
            cx += glyph.advance;
        } else {
            cx += font_char_advance(role, font_px);
        }
    }
}

void gfx_draw_string_role(int x, int y, const char *str, font_role_t role, int font_px,
                          uint32_t fg, uint32_t bg) {
    int w;
    int h;
    int lines = 1;

    if (font_px <= 0) font_px = FONT_HEIGHT;
    w = font_measure_text(role, font_px, str);
    h = font_line_height(role, font_px);
    if (h < 1) h = font_px;
    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') lines++;
    }
    if (w > 0 && h > 0) gfx_fill_rect(x, y, w, h * lines, bg);
    gfx_draw_string_role_transparent(x, y, str, role, font_px, fg);
}

int gfx_measure_text(font_role_t role, int font_px, const char *str) {
    if (font_px <= 0) font_px = FONT_HEIGHT;
    return font_measure_text(role, font_px, str);
}

int gfx_font_line_height(font_role_t role, int font_px) {
    if (font_px <= 0) font_px = FONT_HEIGHT;
    return font_line_height(role, font_px);
}

int gfx_font_char_width(font_role_t role, int font_px) {
    if (font_px <= 0) font_px = FONT_HEIGHT;
    return font_char_advance(role, font_px);
}

int gfx_text_width(const char *str, int font_px) {
    int char_w = gfx_scaled_char_w(font_px);
    int line = 0;
    int best = 0;

    for (int i = 0; str && str[i]; i++) {
        if (str[i] == '\n') {
            if (line > best) best = line;
            line = 0;
            continue;
        }
        line += char_w;
    }
    if (line > best) best = line;
    return best;
}

int gfx_fit_rect_aspect(int outer_x, int outer_y, int outer_w, int outer_h,
                        int design_w, int design_h, int allow_stretch_pct,
                        int *out_x, int *out_y, int *out_w, int *out_h) {
    int scale_fp;
    int w;
    int h;

    if (outer_w < 1 || outer_h < 1 || design_w < 1 || design_h < 1) return 0;

    scale_fp = (outer_w * GFX_SCALE_FP_ONE) / design_w;
    {
        int scale_h = (outer_h * GFX_SCALE_FP_ONE) / design_h;
        if (scale_h < scale_fp) scale_fp = scale_h;
    }
    if (scale_fp < 1) scale_fp = 1;

    w = (design_w * scale_fp) / GFX_SCALE_FP_ONE;
    h = (design_h * scale_fp) / GFX_SCALE_FP_ONE;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (allow_stretch_pct > 0) {
        int max_w = w + (w * allow_stretch_pct) / 100;
        int max_h = h + (h * allow_stretch_pct) / 100;
        if (max_w > outer_w) max_w = outer_w;
        if (max_h > outer_h) max_h = outer_h;
        if (outer_w - max_w < outer_w - w) w = max_w;
        if (outer_h - max_h < outer_h - h) h = max_h;
    }

    if (out_x) *out_x = outer_x + (outer_w - w) / 2;
    if (out_y) *out_y = outer_y + (outer_h - h) / 2;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return 1;
}

void gfx_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        gfx_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    gfx_fill_rect(x, y, w, 1, color);
    gfx_fill_rect(x, y + h - 1, w, 1, color);
    gfx_fill_rect(x, y, 1, h, color);
    gfx_fill_rect(x + w - 1, y, 1, h, color);
}

void gfx_darken_screen(void) {
    int total = (int)g_width * (int)g_height;
    uint32_t *buf = g_backbuffer;

    for (int i = 0; i < total; i++) {
        uint32_t c = buf[i];
        buf[i] = (c >> 1) & 0x007F7F7Fu;
    }
}
