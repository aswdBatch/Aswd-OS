#include "console/console.h"

#include <stddef.h>

#include "drivers/vga.h"
#include "drivers/serial.h"
#include "gui/winconsole.h"

static winconsole_t *g_console_target = 0;
static console_mode_t g_mode = CONSOLE_MODE_SHELL;
static uint8_t g_fg = 15;
static uint8_t g_bg = 0;
static int g_at_line_start = 1;

static void console_apply_color(void) {
  vga_set_color(g_fg, g_bg);
}

static void console_maybe_prefix(void) {
  if (g_mode != CONSOLE_MODE_SCRIPT) {
    return;
  }
  if (!g_at_line_start) {
    return;
  }

  console_apply_color();
  vga_putchar((char)0xDB);
  vga_putchar(' ');
  g_at_line_start = 0;
}

void console_init(void) {
  g_mode = CONSOLE_MODE_SHELL;
  g_fg = 15;
  g_bg = 0;
  g_at_line_start = 1;
  console_apply_color();
}

void console_set_mode(console_mode_t mode) {
  g_mode = mode;
  g_at_line_start = 1;
}

console_mode_t console_get_mode(void) {
  return g_mode;
}

void console_set_color(uint8_t fg, uint8_t bg) {
  g_fg = fg;
  g_bg = bg;
  console_apply_color();
}

void console_set_target(winconsole_t *wc) {
  g_console_target = wc;
}

winconsole_t *console_get_target(void) {
  return g_console_target;
}

void console_putc(char c) {
  /* If redirected to a winconsole, route there instead */
  if (g_console_target) {
    wc_set_color(g_console_target, g_fg, g_bg);
    wc_putc(g_console_target, c);
    if (c == '\n') g_at_line_start = 1;
    else g_at_line_start = 0;
    return;
  }

  if (c == '\n') {
    console_apply_color();
    vga_putchar('\n');
    g_at_line_start = 1;
    if (serial_is_enabled()) {
      serial_write_char('\r');
      serial_write_char('\n');
    }
    return;
  }
  if (c == '\b') {
    console_apply_color();
    vga_putchar('\b');
    if (serial_is_enabled()) {
      serial_write_char('\b');
      serial_write_char(' ');
      serial_write_char('\b');
    }
    return;
  }

  console_maybe_prefix();
  console_apply_color();
  vga_putchar(c);
  g_at_line_start = 0;
  if (serial_is_enabled()) {
    serial_write_char(c);
  }
}

void console_write(const char *s) {
  for (size_t i = 0; s && s[i]; i++) {
    console_putc(s[i]);
  }
}

void console_writeln(const char *s) {
  console_maybe_prefix();
  console_write(s);
  console_putc('\n');
}

void console_write_colored(const char *s, uint8_t fg, uint8_t bg) {
  uint8_t old_fg = g_fg;
  uint8_t old_bg = g_bg;
  console_set_color(fg, bg);
  console_write(s);
  console_set_color(old_fg, old_bg);
}

void console_writeln_colored(const char *s, uint8_t fg, uint8_t bg) {
  uint8_t old_fg = g_fg;
  uint8_t old_bg = g_bg;
  console_set_color(fg, bg);
  console_writeln(s);
  console_set_color(old_fg, old_bg);
}
