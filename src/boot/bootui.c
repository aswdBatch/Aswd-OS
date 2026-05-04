#include "boot/bootui.h"

#include "boot/multiboot.h"
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
#define BOOT_PROGRESS_STAGES 7u

#define GFX_BG_TOP      0xf3f6fb
#define GFX_BG_BOTTOM   0xe3ebf5
#define GFX_FG          0x1d2a3a
#define GFX_FG_SOFT     0x506173
#define GFX_DIM         0x77869a
#define GFX_ACCENT      0x6aa7ea
#define GFX_ACCENT_DEEP 0x3f79c7
#define GFX_PANEL       0xfbfdff
#define GFX_SEL_BG      0x4e8dd7
#define GFX_SEL_FG      0xffffff
#define GFX_TRACK       0xd8e2ef

typedef enum {
  BOOT_STAGE_STORAGE = 0,
  BOOT_STAGE_FILESYSTEM,
  BOOT_STAGE_TIMER_INPUT,
  BOOT_STAGE_PCI,
  BOOT_STAGE_USB,
  BOOT_STAGE_NETWORK,
  BOOT_STAGE_READY,
} boot_progress_stage_t;

static boot_progress_stage_t g_loading_stage = BOOT_STAGE_STORAGE;
static const char *g_loading_text = "Starting...";

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

static int gfx_text_x_centered(const char *text, int font_px) {
  int sw = gfx_width() ? gfx_width() : 800;
  int text_w = gfx_text_width(text, font_px);
  int x = (sw - text_w) / 2;
  return x < 0 ? 0 : x;
}

static int boot_radius(int w, int h, int radius) {
  int limit = w < h ? w : h;
  limit /= 2;
  if (radius < 0) radius = 0;
  if (radius > limit) radius = limit;
  return radius;
}

static void boot_fill_round_rect(int x, int y, int w, int h, int radius,
                                 uint32_t color) {
  int r = boot_radius(w, h, radius);
  if (w <= 0 || h <= 0) return;
  if (r <= 0) {
    gfx_fill_rect(x, y, w, h, color);
    return;
  }

  gfx_fill_rect(x + r, y, w - r * 2, h, color);
  gfx_fill_rect(x, y + r, r, h - r * 2, color);
  gfx_fill_rect(x + w - r, y + r, r, h - r * 2, color);
  for (int dy = 0; dy < r; dy++) {
    int remain = r * r - (r - dy - 1) * (r - dy - 1);
    int inset = 0;
    while (inset * inset < remain) inset++;
    inset = r - inset;
    gfx_fill_rect(x + inset, y + dy, w - inset * 2, 1, color);
    gfx_fill_rect(x + inset, y + h - 1 - dy, w - inset * 2, 1, color);
  }
}

static void boot_fill_round_rect_alpha(int x, int y, int w, int h, int radius,
                                       uint32_t color, uint8_t alpha) {
  int r = boot_radius(w, h, radius);
  if (w <= 0 || h <= 0 || alpha == 0) return;
  if (r <= 0) {
    gfx_fill_rect_alpha(x, y, w, h, color, alpha);
    return;
  }

  gfx_fill_rect_alpha(x + r, y, w - r * 2, h, color, alpha);
  gfx_fill_rect_alpha(x, y + r, r, h - r * 2, color, alpha);
  gfx_fill_rect_alpha(x + w - r, y + r, r, h - r * 2, color, alpha);
  for (int dy = 0; dy < r; dy++) {
    int remain = r * r - (r - dy - 1) * (r - dy - 1);
    int inset = 0;
    while (inset * inset < remain) inset++;
    inset = r - inset;
    gfx_fill_rect_alpha(x + inset, y + dy, w - inset * 2, 1, color, alpha);
    gfx_fill_rect_alpha(x + inset, y + h - 1 - dy, w - inset * 2, 1, color, alpha);
  }
}

static void boot_draw_round_outline(int x, int y, int w, int h, int radius,
                                    uint32_t color) {
  int r = boot_radius(w, h, radius);
  if (w <= 1 || h <= 1) return;
  if (r <= 0) {
    gfx_draw_rect(x, y, w, h, color);
    return;
  }
  gfx_fill_rect(x + r, y, w - r * 2, 1, color);
  gfx_fill_rect(x + r, y + h - 1, w - r * 2, 1, color);
  gfx_fill_rect(x, y + r, 1, h - r * 2, color);
  gfx_fill_rect(x + w - 1, y + r, 1, h - r * 2, color);
  for (int dy = 0; dy < r; dy++) {
    int remain = r * r - (r - dy - 1) * (r - dy - 1);
    int inset = 0;
    while (inset * inset < remain) inset++;
    inset = r - inset;
    gfx_fill_rect(x + inset, y + dy, 1, 1, color);
    gfx_fill_rect(x + w - 1 - inset, y + dy, 1, 1, color);
    gfx_fill_rect(x + inset, y + h - 1 - dy, 1, 1, color);
    gfx_fill_rect(x + w - 1 - inset, y + h - 1 - dy, 1, 1, color);
  }
}

static void gfx_center_scaled_transparent(int y, const char *text, int font_px,
                                          uint32_t fg) {
  gfx_draw_string_scaled_transparent(gfx_text_x_centered(text, font_px),
                                     y, text, font_px, fg);
}

static void gfx_center_role_transparent(int y, const char *text,
                                        font_role_t role, int font_px,
                                        uint32_t fg) {
  int sw = gfx_width() ? gfx_width() : 800;
  int text_w = gfx_measure_text(role, font_px, text);
  int x = (sw - text_w) / 2;
  if (x < 0) x = 0;
  gfx_draw_string_role_transparent(x, y, text, role, font_px, fg);
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

static boot_progress_stage_t boot_stage_for_text(const char *stage) {
  if (!stage) return BOOT_STAGE_STORAGE;
  if (str_eq(stage, "Initializing storage")) return BOOT_STAGE_STORAGE;
  if (str_eq(stage, "Mounting filesystem") ||
      str_eq(stage, "Preparing users") ||
      str_eq(stage, "Filesystem unavailable") ||
      str_eq(stage, "Storage unavailable")) {
    return BOOT_STAGE_FILESYSTEM;
  }
  if (str_eq(stage, "Starting timer") ||
      str_eq(stage, "Starting keyboard")) {
    return BOOT_STAGE_TIMER_INPUT;
  }
  if (str_eq(stage, "Scanning PCI")) return BOOT_STAGE_PCI;
  if (str_eq(stage, "Initializing USB")) return BOOT_STAGE_USB;
  if (str_eq(stage, "Initializing network")) return BOOT_STAGE_NETWORK;
  if (str_eq(stage, "Ready")) return BOOT_STAGE_READY;
  return g_loading_stage;
}

static const char *boot_progress_title(boot_progress_stage_t stage) {
  static const char *k_titles[BOOT_PROGRESS_STAGES] = {
      "Storage",
      "Filesystem",
      "Timer + Input",
      "PCI Discovery",
      "USB Stack",
      "Network",
      "Ready",
  };
  if ((unsigned)stage >= BOOT_PROGRESS_STAGES) return "Boot";
  return k_titles[stage];
}

static void boot_progress_label(char *out, size_t out_size,
                                boot_progress_stage_t stage) {
  char num[8];
  if (!out || out_size == 0) return;
  out[0] = '\0';
  str_copy(out, "Stage ", out_size);
  u32_to_dec((uint32_t)stage + 1u, num, sizeof(num));
  str_cat(out, num, out_size);
  str_cat(out, "/", out_size);
  u32_to_dec(BOOT_PROGRESS_STAGES, num, sizeof(num));
  str_cat(out, num, out_size);
}

static void draw_progress_bar_graphics(int x, int y, int w, int h,
                                       boot_progress_stage_t stage,
                                       int pulse) {
  int fill_w;
  int glow_w;
  if (w <= 0 || h <= 0) return;
  boot_fill_round_rect(x, y, w, h, 8, GFX_TRACK);
  fill_w = ((int)stage + 1) * (w - 4) / (int)BOOT_PROGRESS_STAGES;
  if (fill_w < 8) fill_w = 8;
  if (fill_w > w - 4) fill_w = w - 4;
  glow_w = fill_w - 48 + pulse * 8;
  if (glow_w < 20) glow_w = 20;
  if (glow_w > fill_w) glow_w = fill_w;
  boot_fill_round_rect(x + 2, y + 2, fill_w, h - 4, 6, GFX_ACCENT_DEEP);
  boot_fill_round_rect_alpha(x + 2 + fill_w - glow_w, y + 2, glow_w, h - 4, 6, 0xffffff, 44);
  boot_draw_round_outline(x, y, w, h, 8, 0xc6d4e8);
}

static void draw_boot_backdrop_graphics(int pulse) {
  int sw = gfx_width() ? gfx_width() : 800;
  int sh = gfx_height() ? gfx_height() : 600;
  int band_h = sh / 3;
  int glow = 22 + pulse * 8;

  gfx_fill_rect_gradient_v(0, 0, sw, sh, GFX_BG_TOP, GFX_BG_BOTTOM);
  boot_fill_round_rect_alpha(sw / 8, sh / 7, sw / 3, band_h, 40, 0xd7e6f8, 64);
  boot_fill_round_rect_alpha(sw / 2 - sw / 7, sh / 3, sw / 2, band_h, 56, 0xc9ddf5, (uint8_t)(54 + glow));
  boot_fill_round_rect_alpha(sw - sw / 3, sh / 5, sw / 4, band_h / 2, 40, 0xeaf2fb, 64);
}

static void draw_intro_graphics(int pulse) {
  int sw = gfx_width() ? gfx_width() : 800;
  int sh = gfx_height() ? gfx_height() : 600;
  int panel_w = sw > 960 ? 720 : sw - 120;
  int panel_x;
  int panel_y = sh / 2 - 128;

  if (panel_w < 360) panel_w = 360;
  panel_x = (sw - panel_w) / 2;
  if (panel_x < 24) panel_x = 24;

  draw_boot_backdrop_graphics(pulse);
  boot_fill_round_rect_alpha(panel_x + 3, panel_y + 10, panel_w, 238, 28, 0x15202f, 14);
  boot_fill_round_rect(panel_x, panel_y, panel_w, 238, 28, GFX_PANEL);
  boot_draw_round_outline(panel_x, panel_y, panel_w, 238, 28, 0xd1dbe8);

  boot_fill_round_rect(panel_x + 42, panel_y + 44, 54, 54, 16, GFX_ACCENT);
  boot_fill_round_rect(panel_x + 54, panel_y + 56, 54, 54, 16, GFX_ACCENT_DEEP);
  boot_fill_round_rect_alpha(panel_x + 66, panel_y + 68, 54, 54, 16, 0xffffff, 52);

  gfx_center_scaled_transparent(panel_y + 52, ASWD_OS_NAME, 28, GFX_FG);
  gfx_center_role_transparent(panel_y + 102, ASWD_OS_VERSION,
                              FONT_ROLE_UI, 16, GFX_ACCENT);
  gfx_center_role_transparent(panel_y + 132, "Minimal boot flow",
                              FONT_ROLE_UI, 14, GFX_FG_SOFT);
  gfx_center_role_transparent(panel_y + 156, "Press Space within 3 seconds for boot options",
                              FONT_ROLE_UI, 12, GFX_DIM);

  draw_progress_bar_graphics(panel_x + 40, panel_y + 192, panel_w - 80, 18,
                             BOOT_STAGE_STORAGE, pulse);
  gfx_center_role_transparent(sh - 54, "Space opens boot options",
                              FONT_ROLE_UI, 12, GFX_DIM);
}

static void draw_loading(const char *stage) {
  g_loading_text = stage ? stage : "Starting...";
  g_loading_stage = boot_stage_for_text(stage);

  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    int sw = gfx_width() ? gfx_width() : 800;
    int sh = gfx_height() ? gfx_height() : 600;
    int panel_w = sw > 980 ? 760 : sw - 120;
    int panel_x;
    int panel_y = sh / 2 - 110;
    char stage_label[24];

    if (panel_w < 360) panel_w = 360;
    panel_x = (sw - panel_w) / 2;

    draw_boot_backdrop_graphics((int)g_loading_stage & 1);
    boot_fill_round_rect_alpha(panel_x + 3, panel_y + 10, panel_w, 236, 28, 0x15202f, 12);
    boot_fill_round_rect(panel_x, panel_y, panel_w, 236, 28, GFX_PANEL);
    boot_draw_round_outline(panel_x, panel_y, panel_w, 236, 28, 0xd1dbe8);

    gfx_center_scaled_transparent(panel_y + 36, ASWD_OS_NAME, 24, GFX_FG);
    gfx_center_role_transparent(panel_y + 76, ASWD_OS_VERSION,
                                FONT_ROLE_UI, 14, GFX_ACCENT);

    boot_progress_label(stage_label, sizeof(stage_label), g_loading_stage);
    gfx_center_role_transparent(panel_y + 112, stage_label,
                                FONT_ROLE_UI, 12, GFX_DIM);
    gfx_center_role_transparent(panel_y + 138, boot_progress_title(g_loading_stage),
                                FONT_ROLE_UI, 16, GFX_FG_SOFT);
    gfx_center_role_transparent(panel_y + 168, g_loading_text,
                                FONT_ROLE_UI, 12, GFX_ACCENT);

    draw_progress_bar_graphics(panel_x + 48, panel_y + 198, panel_w - 96, 18,
                               g_loading_stage, (int)g_loading_stage & 1);
    gfx_swap();
    return;
  }

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  txt_center(6, ASWD_OS_NAME, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
  txt_center(7, ASWD_OS_VERSION, vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
  txt_center(10, boot_progress_title(g_loading_stage),
             vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
  txt_center(12, g_loading_text,
             vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
  txt_center(15, "Loading hardware before desktop",
             vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
  {
    char stage_label[24];
    boot_progress_label(stage_label, sizeof(stage_label), g_loading_stage);
    txt_center(17, stage_label,
               vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
  }
}

void boot_loading_begin(void) {
  if (multiboot_boot_quiet()) {
    return;
  }
  draw_loading("Starting...");
}

void boot_loading_step(const char *stage) {
  if (multiboot_boot_quiet()) {
    return;
  }
  draw_loading(stage);
}

void boot_loading_finish(void) {
  if (gfx_get_mode() != GFX_MODE_GRAPHICS) {
    vga_set_scroll_region(0, 24);
  }
}

static void draw_splash(void) {
  if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
    draw_intro_graphics(0);
    gfx_swap();
    return;
  }

  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  txt_center(7, ASWD_OS_NAME, vga_make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
  txt_center(8, ASWD_OS_VERSION, vga_make_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
  txt_center(11, "Fast full-init startup", vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
  txt_center(13, "Desktop waits for hardware", vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
  txt_center(17, "Space for boot options", vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
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
    gfx_fill_rect_gradient_v(0, 0, sw, sh, GFX_BG_TOP, GFX_BG_BOTTOM);
    boot_fill_round_rect_alpha(sw / 5 + 4, 58, sw * 3 / 5, sh - 96, 28, 0x15202f, 12);
    boot_fill_round_rect(sw / 5, 48, sw * 3 / 5, sh - 96, 28, GFX_PANEL);
    boot_draw_round_outline(sw / 5, 48, sw * 3 / 5, sh - 96, 28, 0xd1dbe8);
    gfx_center_scaled_transparent(86, ASWD_OS_NAME, 24, GFX_FG);
    gfx_center_role_transparent(126, ASWD_OS_VERSION, FONT_ROLE_UI, 14, GFX_ACCENT);
    gfx_center_role_transparent(152, "Boot Options", FONT_ROLE_UI, 16, GFX_FG_SOFT);

    for (int i = 0; i < target_count; i++) {
      int y = 206 + i * 44;
      uint32_t bg = (i == selected) ? GFX_SEL_BG : 0xf3f7fc;
      uint32_t fg = (i == selected) ? GFX_SEL_FG : GFX_FG;
      boot_fill_round_rect(sw / 2 - 170, y, 340, 30, 12, bg);
      boot_draw_round_outline(sw / 2 - 170, y, 340, 30, 12,
                              (i == selected) ? GFX_SEL_BG : 0xd3ddea);
      gfx_center(y + 7, target_name(k_targets[i]), fg, bg);
    }

    gfx_center_role_transparent(sh - 60,
                                "Up/Down to choose, Enter to boot, Esc for Normal",
                                FONT_ROLE_UI, 12, GFX_DIM);
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
  uint32_t frame = 0;

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

    if (gfx_get_mode() == GFX_MODE_GRAPHICS) {
      uint32_t next_frame = frame / 6000u;
      static uint32_t last_pulse = 0xffffffffu;
      if (next_frame != last_pulse) {
        last_pulse = next_frame;
        draw_intro_graphics((int)(next_frame & 3u));
      }
      frame++;
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
