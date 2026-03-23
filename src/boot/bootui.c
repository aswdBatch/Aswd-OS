#include "boot/bootui.h"

#include "common/colors.h"
#include "common/config.h"
#include "cpu/ports.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "lib/string.h"
#include "tui/tui.h"

#define SPLASH_TIMEOUT_SECS 3u

#define GFX_BG      0x111827
#define GFX_FG      0xeff6ff
#define GFX_DIM     0x6b7280
#define GFX_ACCENT  0x38bdf8
#define GFX_PANEL   0x172554
#define GFX_SEL_BG  0x2563eb
#define GFX_SEL_FG  0xffffff

static int is_up_key(char c) {
  return c == KEY_UP || c == 'w' || c == 'k';
}

static int is_down_key(char c) {
  return c == KEY_DOWN || c == 's' || c == 'j';
}

static uint8_t rtc_read(uint8_t reg) {
  outb(0x70, reg);
  return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t value) {
  return (uint8_t)(((value >> 4) * 10u) + (value & 0x0Fu));
}

static uint8_t rtc_seconds_now(void) {
  while (rtc_read(0x0A) & 0x80u) {
  }

  if (rtc_read(0x0B) & 0x04u) {
    return rtc_read(0x00);
  }
  return bcd_to_bin(rtc_read(0x00));
}

static uint8_t seconds_elapsed(uint8_t start, uint8_t now) {
  if (now >= start) return (uint8_t)(now - start);
  return (uint8_t)(60u - start + now);
}

static char bootkbd_translate(uint8_t sc) {
  static uint8_t shift = 0;
  static uint8_t e0 = 0;

  if (sc == 0xE0u) {
    e0 = 1;
    return 0;
  }

  if (e0) {
    e0 = 0;
    if (sc == 0x48u) return KEY_UP;
    if (sc == 0x50u) return KEY_DOWN;
    if (sc == 0x4Bu) return KEY_LEFT;
    if (sc == 0x4Du) return KEY_RIGHT;
    return 0;
  }

  if (sc == 0x2Au || sc == 0x36u) {
    shift = 1;
    return 0;
  }
  if (sc == 0xAAu || sc == 0xB6u) {
    shift = 0;
    return 0;
  }
  if (sc & 0x80u) {
    return 0;
  }

  switch (sc) {
    case 0x01u: return 0x1B;
    case 0x1Cu: return '\n';
    case 0x39u: return ' ';
    case 0x11u: return shift ? 'W' : 'w';
    case 0x1Fu: return shift ? 'S' : 's';
    case 0x24u: return shift ? 'J' : 'j';
    case 0x25u: return shift ? 'K' : 'k';
    default:    return 0;
  }
}

static int bootkbd_try_getchar(char *out) {
  if (!out) return 0;
  while (inb(0x64) & 0x01u) {
    char c = bootkbd_translate(inb(0x60));
    if (c) {
      *out = c;
      return 1;
    }
  }
  return 0;
}

static void gfx_center(int y, const char *text, uint32_t fg, uint32_t bg) {
  int sw = gfx_width() ? gfx_width() : 800;
  int len = (int)str_len(text);
  int x = (sw - len * FONT_WIDTH) / 2;
  if (x < 0) x = 0;
  gfx_draw_string(x, y, text, fg, bg);
}

static void txt_center(int row, const char *text, uint8_t color) {
  int len = (int)str_len(text);
  int col = (80 - len) / 2;
  if (col < 0) col = 0;
  tui_write_at(row, col, text, color);
}

static const char *target_name(boot_target_t target) {
  switch (target) {
    case BOOT_TARGET_NORMAL_GUI: return "Normal";
    case BOOT_TARGET_TUI_LEGACY: return "TUI (Legacy)";
    case BOOT_TARGET_SHELL_ONLY: return "Shell Only";
    case BOOT_TARGET_FS_LAB:     return "FS Lab";
    default:                     return "Normal";
  }
}

static void draw_loading(const char *stage) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    gfx_fill_rect(0, 0, gfx_width(), gfx_height(), GFX_BG);
    gfx_center(gfx_height() / 2 - 20, ASWD_OS_BANNER, GFX_FG, GFX_BG);
    gfx_center(gfx_height() / 2 + 8, stage ? stage : "Starting...", GFX_ACCENT, GFX_BG);
    gfx_swap();
    return;
  }

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  txt_center(11, ASWD_OS_BANNER, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
  txt_center(13, stage ? stage : "Starting...", vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
}

void boot_loading_begin(void) {
  draw_loading("Starting...");
}

void boot_loading_step(const char *stage) {
  draw_loading(stage);
}

void boot_loading_finish(void) {
  if (gfx_get_mode() != GFX_MODE_GRAPHICS) {
    vga_set_scroll_region(0, 24);
  }
}

static void draw_splash(void) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    int sw = gfx_width() ? gfx_width() : 800;
    int sh = gfx_height() ? gfx_height() : 600;
    gfx_fill_rect(0, 0, sw, sh, GFX_BG);
    gfx_center(sh / 2 - 12, ASWD_OS_BANNER, GFX_FG, GFX_BG);
    gfx_center(sh - 40, "Press Space for boot options", GFX_DIM, GFX_BG);
    gfx_swap();
    return;
  }

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  txt_center(11, ASWD_OS_BANNER, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
  txt_center(15, "Space for boot options", vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
}

static void draw_chooser(int selected) {
  static const boot_target_t k_targets[] = {
      BOOT_TARGET_NORMAL_GUI,
      BOOT_TARGET_TUI_LEGACY,
      BOOT_TARGET_SHELL_ONLY,
      BOOT_TARGET_FS_LAB,
  };
  const int target_count = (int)(sizeof(k_targets) / sizeof(k_targets[0]));

  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    int sw = gfx_width() ? gfx_width() : 800;
    int sh = gfx_height() ? gfx_height() : 600;
    gfx_fill_rect(0, 0, sw, sh, GFX_BG);
    gfx_center(90, ASWD_OS_BANNER, GFX_FG, GFX_BG);
    gfx_center(124, "Boot Options", GFX_ACCENT, GFX_BG);

    for (int i = 0; i < target_count; i++) {
      int y = 200 + i * 44;
      uint32_t bg = (i == selected) ? GFX_SEL_BG : GFX_PANEL;
      uint32_t fg = (i == selected) ? GFX_SEL_FG : GFX_FG;
      gfx_fill_rect(sw / 2 - 170, y, 340, 30, bg);
      gfx_center(y + 7, target_name(k_targets[i]), fg, bg);
    }

    gfx_center(sh - 48, "Up/Down to choose, Enter to boot, Esc for Normal",
               GFX_DIM, GFX_BG);
    gfx_swap();
    return;
  }

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  tui_header_bar(ASWD_OS_BANNER);
  txt_center(4, "Boot Options", vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));

  for (int i = 0; i < target_count; i++) {
    uint8_t color = (i == selected)
        ? vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE)
        : vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_fill_row(9 + i * 2, ' ', color);
    txt_center(9 + i * 2, target_name(k_targets[i]), color);
  }

  txt_center(20, "Up/Down choose  Enter boot  Esc Normal",
             vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
}

static boot_target_t boot_choose_target(void) {
  static const boot_target_t k_targets[] = {
      BOOT_TARGET_NORMAL_GUI,
      BOOT_TARGET_TUI_LEGACY,
      BOOT_TARGET_SHELL_ONLY,
      BOOT_TARGET_FS_LAB,
  };
  const int target_count = (int)(sizeof(k_targets) / sizeof(k_targets[0]));
  int selected = 0;

  draw_chooser(selected);
  for (;;) {
    char c;
    if (!bootkbd_try_getchar(&c)) {
      io_wait();
      continue;
    }

    if (is_up_key(c)) {
      if (selected > 0) selected--;
      draw_chooser(selected);
      continue;
    }
    if (is_down_key(c)) {
      if (selected + 1 < target_count) selected++;
      draw_chooser(selected);
      continue;
    }
    if (c == 0x1B) {
      return BOOT_TARGET_NORMAL_GUI;
    }
    if (c == '\r' || c == '\n') {
      return k_targets[selected];
    }
  }
}

void boot_launcher_run(boot_selection_t *selection) {
  boot_selection_t sel;
  uint8_t start_sec;

  if (!selection) return;

  sel.target = BOOT_TARGET_NORMAL_GUI;
  sel.bugcheck_style = BOOT_BUGCHECK_MODERN;
  sel.test_mode = DIAGNOSTIC_TEST_NONE;

  draw_splash();
  start_sec = rtc_seconds_now();

  for (;;) {
    char c;

    if (seconds_elapsed(start_sec, rtc_seconds_now()) >= SPLASH_TIMEOUT_SECS) {
      break;
    }

    if (!bootkbd_try_getchar(&c)) {
      io_wait();
      continue;
    }

    if (c == ' ') {
      sel.target = boot_choose_target();
      *selection = sel;
      return;
    }
  }

  *selection = sel;
}
