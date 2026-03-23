#pragma once

#include <stdint.h>

void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_print(const char *str);
void vga_println(const char *str);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_print_colored(const char *str, uint8_t fg, uint8_t bg);
void vga_print_script_line(const char *line);

/* Direct-write helpers used by TUI and editor */
void vga_put_char_at(char c, uint8_t color, int row, int col);
void vga_fill_row(int row, char c, uint8_t color);
void vga_set_scroll_region(int top, int bottom); /* rows [top, bottom] inclusive */
void vga_set_cursor_pos(int row, int col);

