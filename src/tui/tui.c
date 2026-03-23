#include "tui/tui.h"

#include "common/colors.h"
#include "common/config.h"
#include "drivers/vga.h"
#include "lib/string.h"

static const int TUI_WIDTH = 80;
static const int TUI_HEIGHT = 25;
static const int TUI_HEADER_ROW = 0;
static const int TUI_BODY_TOP = 1;
static const int TUI_BODY_BOTTOM = 23;
static const int TUI_STATUS_ROW = 24;

static void draw_row_text(int row, int col, const char *text, uint8_t color) {
  if (row < 0 || row >= TUI_HEIGHT || col >= TUI_WIDTH) {
    return;
  }

  int x = col;
  for (size_t i = 0; text && text[i] && x < TUI_WIDTH; i++, x++) {
    vga_put_char_at(text[i], color, row, x);
  }
}

void tui_fill_rect(int row, int col, int width, int height, char c, uint8_t color) {
  if (width <= 0 || height <= 0) {
    return;
  }

  for (int y = 0; y < height; y++) {
    int screen_row = row + y;
    if (screen_row < 0 || screen_row >= TUI_HEIGHT) {
      continue;
    }
    for (int x = 0; x < width; x++) {
      int screen_col = col + x;
      if (screen_col < 0 || screen_col >= TUI_WIDTH) {
        continue;
      }
      vga_put_char_at(c, color, screen_row, screen_col);
    }
  }
}

void tui_write_at(int row, int col, const char *text, uint8_t color) {
  draw_row_text(row, col, text, color);
}

void tui_draw_box(int row, int col, int width, int height, uint8_t color) {
  if (width < 2 || height < 2) {
    return;
  }

  const char tl = (char)0xDA;
  const char tr = (char)0xBF;
  const char bl = (char)0xC0;
  const char br = (char)0xD9;
  const char h = (char)0xC4;
  const char v = (char)0xB3;

  for (int x = 1; x < width - 1; x++) {
    vga_put_char_at(h, color, row, col + x);
    vga_put_char_at(h, color, row + height - 1, col + x);
  }
  for (int y = 1; y < height - 1; y++) {
    vga_put_char_at(v, color, row + y, col);
    vga_put_char_at(v, color, row + y, col + width - 1);
  }

  vga_put_char_at(tl, color, row, col);
  vga_put_char_at(tr, color, row, col + width - 1);
  vga_put_char_at(bl, color, row + height - 1, col);
  vga_put_char_at(br, color, row + height - 1, col + width - 1);
}

void tui_header_bar(const char *title) {
  uint8_t bg = vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
  vga_fill_row(TUI_HEADER_ROW, ' ', bg);
  tui_write_at(TUI_HEADER_ROW, 1, title ? title : ASWD_OS_BANNER, bg);
}

void tui_status_bar(const char *status) {
  uint8_t bg = vga_make_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
  vga_fill_row(TUI_STATUS_ROW, ' ', bg);
  tui_write_at(TUI_STATUS_ROW, 1, status ? status : "", bg);
}

void tui_shell_frame(const char *cwd) {
  char status[128];

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  vga_set_scroll_region(TUI_BODY_TOP, TUI_BODY_BOTTOM);

  tui_header_bar(ASWD_OS_BANNER);
  tui_write_at(TUI_HEADER_ROW, 60, "32-bit PM", vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE));

  status[0] = '\0';
  str_copy(status, "cwd: ", sizeof(status));
  str_cat(status, cwd ? cwd : "/", sizeof(status));
  tui_status_bar(status);

  vga_set_cursor_pos(TUI_BODY_TOP, 0);
}
