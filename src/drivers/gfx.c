#include "drivers/gfx.h"

#include <stdint.h>

#include "boot/multiboot.h"
#include "boot/videoinfo.h"
#include "drivers/bga.h"
#include "drivers/serial.h"
#include "drivers/font.h"
#include "lib/string.h"

/* Max resolution 1024x768 for backbuffer (3 MB in BSS) */
static uint32_t g_backbuffer[1024 * 768];
static uint32_t *g_lfb;
static uint16_t g_width, g_height, g_pitch;
static gfx_mode_t g_mode = GFX_MODE_TEXT;

int gfx_init(void) {
    serial_write("GFX: init\n");

    /* Tier 1: BGA — protected-mode I/O ports, most reliable in QEMU */
    if (bga_detect()) {
        uint32_t lfb_addr = 0, pitch = 0;
        if (bga_set_mode(800, 600, 32, &lfb_addr, &pitch)) {
            g_lfb    = (uint32_t *)lfb_addr;
            g_width  = 800;
            g_height = 600;
            g_pitch  = (uint16_t)pitch;
            g_mode   = GFX_MODE_GRAPHICS;
            serial_write("GFX: BGA OK\n");
            mem_set(g_backbuffer, 0, (uint32_t)g_width * g_height * 4);
            gfx_swap();
            return 1;
        }
        serial_write("GFX: BGA set_mode failed, trying fallbacks\n");
    }

    /* Tier 2: Multiboot framebuffer (GRUB-provided) */
    if (multiboot_has_framebuffer()) {
        g_lfb    = (uint32_t *)multiboot_fb_addr();
        g_width  = (uint16_t)multiboot_fb_width();
        g_height = (uint16_t)multiboot_fb_height();
        g_pitch  = (uint16_t)multiboot_fb_pitch();
        g_mode   = GFX_MODE_GRAPHICS;
        serial_write("GFX: multiboot fb\n");
        mem_set(g_backbuffer, 0, (uint32_t)g_width * g_height * 4);
        gfx_swap();
        return 1;
    }

    /* Tier 3: Stage2 VBE (USB boot — mode set by bootloader in real mode) */
    if (bootvid_available()) {
        g_lfb    = (uint32_t *)BOOTVID_FB_ADDR;
        g_width  = BOOTVID_WIDTH;
        g_height = BOOTVID_HEIGHT;
        g_pitch  = (uint16_t)BOOTVID_PITCH;
        g_mode   = GFX_MODE_GRAPHICS;
        serial_write("GFX: stage2 VBE\n");
        mem_set(g_backbuffer, 0, (uint32_t)g_width * g_height * 4);
        gfx_swap();
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
    /* Clip */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > g_width  ? g_width  : x + w;
    int y1 = y + h > g_height ? g_height : y + h;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = &g_backbuffer[row * g_width + x0];
        for (int col = x0; col < x1; col++) {
            *dst++ = color;
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

/* --- Phase 2: Font rendering --- */

void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    unsigned char uc = (unsigned char)c;
    if (uc >= 128) uc = '?';

    const uint8_t *glyph = &g_font_8x16[uc * FONT_HEIGHT];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        int py = y + row;
        if (py < 0 || py >= g_height) continue;
        for (int col = 0; col < FONT_WIDTH; col++) {
            int px = x + col;
            if (px < 0 || px >= g_width) continue;
            g_backbuffer[py * g_width + px] = (bits & (0x80 >> col)) ? fg : bg;
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
