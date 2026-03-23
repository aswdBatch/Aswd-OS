#include "drivers/vga.h"

#include <stddef.h>

#include "common/colors.h"
#include "cpu/ports.h"
#include "drivers/gfx.h"
#include "drivers/vtconsole.h"
#include "lib/string.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static uint16_t *const vga_buffer = (uint16_t *)0xB8000;
static size_t vga_row = 0;
static size_t vga_col = 0;
static uint8_t vga_color = 0;
static size_t vga_scroll_top = 0;
static size_t vga_scroll_bottom = VGA_HEIGHT - 1;

static void vga_update_cursor(void) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_set_cursor((int)vga_row, (int)vga_col);
    vtc_auto_flush();
    return;
  }
  uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
  outb(0x3D4, 0x0F);
  outb(0x3D5, (uint8_t)(pos & 0xFF));
  outb(0x3D4, 0x0E);
  outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static uint16_t vga_entry(unsigned char uc, uint8_t color) {
  return (uint16_t)uc | (uint16_t)color << 8;
}

static void vga_scroll_if_needed(void) {
  if (vga_row <= vga_scroll_bottom) {
    return;
  }

  if (vga_scroll_top >= vga_scroll_bottom) {
    vga_row = vga_scroll_bottom;
    return;
  }

  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    uint16_t *shadow = vtc_shadow();
    for (size_t y = vga_scroll_top + 1; y <= vga_scroll_bottom; y++) {
      for (size_t x = 0; x < VGA_WIDTH; x++) {
        uint16_t cell = shadow[y * VGA_WIDTH + x];
        vtc_put_char_at((char)(cell & 0xFF), (uint8_t)(cell >> 8),
                        (int)(y - 1), (int)x);
      }
    }
    vtc_fill_row((int)vga_scroll_bottom, ' ', vga_color);
    vga_row = vga_scroll_bottom;
    return;
  }

  for (size_t y = vga_scroll_top + 1; y <= vga_scroll_bottom; y++) {
    for (size_t x = 0; x < VGA_WIDTH; x++) {
      vga_buffer[(y - 1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
    }
  }

  for (size_t x = 0; x < VGA_WIDTH; x++) {
    vga_buffer[vga_scroll_bottom * VGA_WIDTH + x] = vga_entry(' ', vga_color);
  }

  vga_row = vga_scroll_bottom;
}

void vga_init(void) {
  vga_row = 0;
  vga_col = 0;
  vga_color = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_scroll_top = 0;
  vga_scroll_bottom = VGA_HEIGHT - 1;
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_init();
  }
}

void vga_set_color(uint8_t fg, uint8_t bg) {
  vga_color = vga_make_color(fg, bg);
}

void vga_clear(void) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_clear(vga_color);
    vtc_auto_flush();
    vga_row = 0;
    vga_col = 0;
    return;
  }
  for (size_t y = 0; y < VGA_HEIGHT; y++) {
    for (size_t x = 0; x < VGA_WIDTH; x++) {
      vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', vga_color);
    }
  }
  vga_row = 0;
  vga_col = 0;
  vga_update_cursor();
}

void vga_put_char_at(char c, uint8_t color, int row, int col) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_put_char_at(c, color, row, col);
    return;
  }
  if (row < 0 || col < 0) {
    return;
  }
  if (row >= (int)VGA_HEIGHT || col >= (int)VGA_WIDTH) {
    return;
  }
  vga_buffer[(size_t)row * VGA_WIDTH + (size_t)col] = vga_entry((unsigned char)c, color);
}

void vga_fill_row(int row, char c, uint8_t color) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_fill_row(row, c, color);
    return;
  }
  if (row < 0 || row >= (int)VGA_HEIGHT) {
    return;
  }
  for (size_t x = 0; x < VGA_WIDTH; x++) {
    vga_buffer[(size_t)row * VGA_WIDTH + x] = vga_entry((unsigned char)c, color);
  }
}

void vga_set_scroll_region(int top, int bottom) {
  if (top < 0 || bottom < 0 || top >= (int)VGA_HEIGHT || bottom >= (int)VGA_HEIGHT || top > bottom) {
    vga_scroll_top = 0;
    vga_scroll_bottom = VGA_HEIGHT - 1;
    return;
  }

  vga_scroll_top = (size_t)top;
  vga_scroll_bottom = (size_t)bottom;
  if (vga_row < vga_scroll_top) {
    vga_row = vga_scroll_top;
  }
  if (vga_row > vga_scroll_bottom) {
    vga_row = vga_scroll_bottom;
  }
  if (vga_col >= VGA_WIDTH) {
    vga_col = VGA_WIDTH - 1;
  }
}

void vga_set_cursor_pos(int row, int col) {
  if (row < 0) {
    row = 0;
  } else if (row >= (int)VGA_HEIGHT) {
    row = (int)VGA_HEIGHT - 1;
  }
  if (col < 0) {
    col = 0;
  } else if (col >= (int)VGA_WIDTH) {
    col = (int)VGA_WIDTH - 1;
  }

  vga_row = (size_t)row;
  vga_col = (size_t)col;
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    vtc_set_cursor(row, col);
    return;
  }
  vga_update_cursor();
}

void vga_putchar(char c) {
  if (c == '\n') {
    vga_col = 0;
    vga_row++;
    vga_scroll_if_needed();
    vga_update_cursor();
    return;
  }
  if (c == '\r') {
    vga_col = 0;
    vga_update_cursor();
    return;
  }
  if (c == '\b') {
    if (vga_col > 0) {
      vga_col--;
    } else if (vga_row > vga_scroll_top) {
      vga_row--;
      vga_col = VGA_WIDTH - 1;
    } else {
      return;
    }
    vga_put_char_at(' ', vga_color, (int)vga_row, (int)vga_col);
    vga_update_cursor();
    return;
  }

  vga_put_char_at(c, vga_color, (int)vga_row, (int)vga_col);
  vga_col++;
  if (vga_col >= VGA_WIDTH) {
    vga_col = 0;
    vga_row++;
    vga_scroll_if_needed();
  }
  vga_update_cursor();
}

void vga_print(const char *str) {
  for (size_t i = 0; str && str[i]; i++) {
    vga_putchar(str[i]);
  }
}

void vga_println(const char *str) {
  vga_print(str);
  vga_putchar('\n');
}

void vga_print_colored(const char *str, uint8_t fg, uint8_t bg) {
  uint8_t old = vga_color;
  vga_set_color(fg, bg);
  vga_print(str);
  vga_color = old;
}

void vga_print_script_line(const char *line) {
  vga_putchar((char)0xDB);
  vga_putchar(' ');
  vga_print(line);
  vga_putchar('\n');
}
