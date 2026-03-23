#pragma once

#include <stdint.h>

#define VTC_COLS 80
#define VTC_ROWS 25

void vtc_init(void);
void vtc_put_char_at(char c, uint8_t color, int row, int col);
void vtc_fill_row(int row, char c, uint8_t color);
void vtc_clear(uint8_t color);
void vtc_set_cursor(int row, int col);
void vtc_auto_flush(void);
uint16_t *vtc_shadow(void);
