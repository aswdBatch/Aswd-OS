#pragma once

#include <stdint.h>

typedef enum {
  CONSOLE_MODE_SHELL = 0,
  CONSOLE_MODE_SCRIPT = 1,
} console_mode_t;

void console_init(void);
void console_set_mode(console_mode_t mode);
console_mode_t console_get_mode(void);

void console_set_color(uint8_t fg, uint8_t bg);

void console_putc(char c);
void console_write(const char *s);
void console_writeln(const char *s);

void console_write_colored(const char *s, uint8_t fg, uint8_t bg);
void console_writeln_colored(const char *s, uint8_t fg, uint8_t bg);

/* Redirect console output to a winconsole (NULL = back to VGA) */
struct winconsole_t;
void console_set_target(struct winconsole_t *wc);
struct winconsole_t *console_get_target(void);

