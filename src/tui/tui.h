#pragma once

#include <stdint.h>

void tui_draw_box(int row, int col, int width, int height, uint8_t color);
void tui_write_at(int row, int col, const char *text, uint8_t color);
void tui_fill_rect(int row, int col, int width, int height, char c, uint8_t color);
void tui_header_bar(const char *title);
void tui_status_bar(const char *status);
void tui_shell_frame(const char *cwd);
