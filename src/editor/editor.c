#include "editor/editor.h"

#include <stdint.h>

#include "common/colors.h"
#include "cpu/pic.h"
#include "input/input.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "tui/tui.h"

#define EDITOR_BODY_TOP      1
#define EDITOR_BODY_BOTTOM   23
#define EDITOR_VISIBLE_LINES (EDITOR_BODY_BOTTOM - EDITOR_BODY_TOP + 1)
#define EDITOR_MAX_LINES     512
#define EDITOR_LINE_CAP      161
#define EDITOR_TEXT_CAP      (EDITOR_MAX_LINES * EDITOR_LINE_CAP + 1)

/* Line-number gutter: " NNN " = 5 chars, leaving 75 editable columns.
   Remove this define to disable the gutter and restore 80-col editing. */
#define SHOW_LINENOS
#ifdef SHOW_LINENOS
#  define EDITOR_GUTTER_WIDTH 5
#else
#  define EDITOR_GUTTER_WIDTH 0
#endif

static char     g_name[32];
static char     g_lines[EDITOR_MAX_LINES][EDITOR_LINE_CAP];
static uint16_t g_lens[EDITOR_MAX_LINES];
static uint8_t  g_load_buf[EDITOR_TEXT_CAP];
static char     g_save_buf[EDITOR_TEXT_CAP];
static int      g_line_count   = 1;
static int      g_cursor_line  = 0;
static int      g_cursor_col   = 0;
static int      g_view_top     = 0;
static int      g_dirty        = 0;
static int      g_insert_mode  = 1;
static int      g_quit_armed   = 0;
static char     g_message[96];

static int editor_path_is_workspace_target(const char *path) {
  if (!path || !path[0]) return 0;
  if (str_eq(path, "/ROOT")) return 1;
  return str_ncmp(path, "/ROOT", 5) == 0 && path[5] == '/';
}

static void set_message(const char *msg) {
  str_copy(g_message, msg ? msg : "", sizeof(g_message));
}

static void clear_buffer(void) {
  for (int i = 0; i < EDITOR_MAX_LINES; i++) {
    g_lines[i][0] = '\0';
    g_lens[i] = 0;
  }
  g_line_count  = 1;
  g_cursor_line = 0;
  g_cursor_col  = 0;
  g_view_top    = 0;
  g_dirty       = 0;
  g_insert_mode = 1;
  g_quit_armed  = 0;
  g_message[0]  = '\0';
}

static void clamp_cursor(void) {
  if (g_line_count <= 0) g_line_count = 1;
  if (g_cursor_line < 0) g_cursor_line = 0;
  else if (g_cursor_line >= g_line_count) g_cursor_line = g_line_count - 1;
  if (g_cursor_col < 0) g_cursor_col = 0;
  if (g_cursor_col > (int)g_lens[g_cursor_line])
    g_cursor_col = (int)g_lens[g_cursor_line];
}

static void ensure_visible(void) {
  clamp_cursor();
  if (g_cursor_line < g_view_top)
    g_view_top = g_cursor_line;
  if (g_cursor_line >= g_view_top + EDITOR_VISIBLE_LINES)
    g_view_top = g_cursor_line - EDITOR_VISIBLE_LINES + 1;
  if (g_view_top < 0) g_view_top = 0;
  if (g_view_top > g_line_count - 1) g_view_top = g_line_count - 1;
  if (g_view_top < 0) g_view_top = 0;
}

static void append_line_from_raw(const char *src, int len) {
  if (g_line_count >= EDITOR_MAX_LINES) return;
  if (len < 0) len = 0;
  if (len >= EDITOR_LINE_CAP) len = EDITOR_LINE_CAP - 1;

  char *dst = g_lines[g_line_count];
  for (int i = 0; i < len; i++) dst[i] = src[i];
  dst[len] = '\0';
  g_lens[g_line_count] = (uint16_t)len;
  g_line_count++;
}

static void load_file(const char *name) {
  clear_buffer();

  if (!name || !name[0]) {
    str_copy(g_name, "UNTITLED.TXT", sizeof(g_name));
    g_line_count = 1;
    return;
  }

  str_copy(g_name, name, sizeof(g_name));
  g_line_count = 0;

  int read = vfs_cat(name, g_load_buf, (int)sizeof(g_load_buf) - 1);
  if (read < 0) {
    if (!vfs_available()) {
      set_message("No filesystem - new file");
    } else {
      set_message("New file");
    }
    g_line_count = 1;
    g_lines[0][0] = '\0';
    g_lens[0] = 0;
    return;
  }

  if (read == 0) {
    g_line_count = 1;
    g_lines[0][0] = '\0';
    g_lens[0] = 0;
    return;
  }

  g_load_buf[read] = '\0';
  int start = 0;
  for (int i = 0; i <= read; i++) {
    if (i < read && g_load_buf[i] != '\n') continue;

    int len = i - start;
    while (len > 0 && g_load_buf[start + len - 1] == '\r') len--;
    append_line_from_raw((const char *)&g_load_buf[start], len);
    start = i + 1;
    if (g_line_count >= EDITOR_MAX_LINES) break;
  }

  if (g_line_count == 0) {
    g_line_count = 1;
    g_lines[0][0] = '\0';
    g_lens[0] = 0;
  }
}

static int save_file(void) {
  int out = 0;

  for (int i = 0; i < g_line_count; i++) {
    const char *line = g_lines[i];
    for (int j = 0; line[j] && out + 1 < (int)sizeof(g_save_buf); j++)
      g_save_buf[out++] = line[j];
    if (i + 1 < g_line_count && out + 1 < (int)sizeof(g_save_buf))
      g_save_buf[out++] = '\n';
  }

  if (out < 0) out = 0;
  int written = vfs_write(g_name, (const uint8_t *)g_save_buf, (uint32_t)out);
  if (written < 0) {
    if (!vfs_cwd_is_writable() ||
        (g_name[0] == '/' && !editor_path_is_workspace_target(g_name))) {
      set_message("protected system location");
    } else {
      set_message("save failed");
    }
    return 0;
  }

  g_dirty = 0;
  g_quit_armed = 0;
  set_message("saved");
  return 1;
}

static void split_current_line(void) {
  if (g_line_count >= EDITOR_MAX_LINES) {
    set_message("line limit reached");
    return;
  }

  int line = g_cursor_line;
  int col  = g_cursor_col;
  int len  = (int)g_lens[line];
  if (col < 0) col = 0;
  if (col > len) col = len;

  char right[EDITOR_LINE_CAP];
  int right_len = len - col;
  if (right_len < 0) right_len = 0;
  if (right_len >= EDITOR_LINE_CAP) right_len = EDITOR_LINE_CAP - 1;

  for (int i = 0; i < right_len; i++) right[i] = g_lines[line][col + i];
  right[right_len] = '\0';

  g_lines[line][col] = '\0';
  g_lens[line] = (uint16_t)col;

  for (int i = g_line_count; i > line + 1; i--) {
    str_copy(g_lines[i], g_lines[i - 1], EDITOR_LINE_CAP);
    g_lens[i] = g_lens[i - 1];
  }

  str_copy(g_lines[line + 1], right, EDITOR_LINE_CAP);
  g_lens[line + 1] = (uint16_t)right_len;
  g_line_count++;
  g_cursor_line++;
  g_cursor_col = 0;
  g_dirty = 1;
  g_quit_armed = 0;
}

static void merge_with_previous_line(void) {
  if (g_cursor_line <= 0) return;

  int current  = g_cursor_line;
  int previous = current - 1;
  int prev_len = (int)g_lens[previous];
  int cur_len  = (int)g_lens[current];
  int room = EDITOR_LINE_CAP - 1 - prev_len;
  if (room < 0) room = 0;
  if (cur_len > room) cur_len = room;

  for (int i = 0; i < cur_len; i++)
    g_lines[previous][prev_len + i] = g_lines[current][i];
  g_lines[previous][prev_len + cur_len] = '\0';
  g_lens[previous] = (uint16_t)(prev_len + cur_len);

  for (int i = current; i < g_line_count - 1; i++) {
    str_copy(g_lines[i], g_lines[i + 1], EDITOR_LINE_CAP);
    g_lens[i] = g_lens[i + 1];
  }
  g_line_count--;
  if (g_line_count < 1) {
    g_line_count = 1;
    g_lines[0][0] = '\0';
    g_lens[0] = 0;
  }

  g_cursor_line = previous;
  g_cursor_col  = prev_len;
  g_dirty = 1;
  g_quit_armed = 0;
}

static void delete_at_cursor(void) {
  int line = g_cursor_line;
  int col  = g_cursor_col;
  int len  = (int)g_lens[line];

  if (col < len) {
    for (int i = col; i < len; i++) g_lines[line][i] = g_lines[line][i + 1];
    g_lens[line] = (uint16_t)(len - 1);
    g_dirty = 1;
    g_quit_armed = 0;
    return;
  }

  if (line + 1 < g_line_count) {
    int next_len = (int)g_lens[line + 1];
    int room = EDITOR_LINE_CAP - 1 - len;
    if (room < 0) room = 0;
    if (next_len > room) next_len = room;
    for (int i = 0; i < next_len; i++)
      g_lines[line][len + i] = g_lines[line + 1][i];
    g_lines[line][len + next_len] = '\0';
    g_lens[line] = (uint16_t)(len + next_len);

    for (int i = line + 1; i < g_line_count - 1; i++) {
      str_copy(g_lines[i], g_lines[i + 1], EDITOR_LINE_CAP);
      g_lens[i] = g_lens[i + 1];
    }
    g_line_count--;
    g_dirty = 1;
    g_quit_armed = 0;
  }
}

static void backspace_at_cursor(void) {
  if (g_cursor_col > 0) {
    int line = g_cursor_line;
    int col  = g_cursor_col;
    int len  = (int)g_lens[line];
    for (int i = col - 1; i < len; i++) g_lines[line][i] = g_lines[line][i + 1];
    g_lens[line] = (uint16_t)(len - 1);
    g_cursor_col--;
    g_dirty = 1;
    g_quit_armed = 0;
    return;
  }

  merge_with_previous_line();
}

static void insert_char(char c) {
  int line = g_cursor_line;
  int col  = g_cursor_col;
  int len  = (int)g_lens[line];

  if (g_insert_mode) {
    if (len >= EDITOR_LINE_CAP - 1) { set_message("line full"); return; }
    for (int i = len; i > col; i--) g_lines[line][i] = g_lines[line][i - 1];
    g_lines[line][col] = c;
    g_lines[line][len + 1] = '\0';
    g_lens[line] = (uint16_t)(len + 1);
    g_cursor_col++;
  } else {
    if (col < len) {
      g_lines[line][col] = c;
      g_cursor_col++;
    } else {
      if (len >= EDITOR_LINE_CAP - 1) { set_message("line full"); return; }
      g_lines[line][col] = c;
      g_lines[line][col + 1] = '\0';
      g_lens[line] = (uint16_t)(len + 1);
      g_cursor_col++;
    }
  }

  g_dirty = 1;
  g_quit_armed = 0;
}

static void move_left(void) {
  if (g_cursor_col > 0) { g_cursor_col--; return; }
  if (g_cursor_line > 0) {
    g_cursor_line--;
    g_cursor_col = (int)g_lens[g_cursor_line];
  }
}

static void move_right(void) {
  if (g_cursor_col < (int)g_lens[g_cursor_line]) { g_cursor_col++; return; }
  if (g_cursor_line + 1 < g_line_count) { g_cursor_line++; g_cursor_col = 0; }
}

static void move_up(void) {
  if (g_cursor_line > 0) {
    g_cursor_line--;
    if (g_cursor_col > (int)g_lens[g_cursor_line])
      g_cursor_col = (int)g_lens[g_cursor_line];
  }
}

static void move_down(void) {
  if (g_cursor_line + 1 < g_line_count) {
    g_cursor_line++;
    if (g_cursor_col > (int)g_lens[g_cursor_line])
      g_cursor_col = (int)g_lens[g_cursor_line];
  }
}

static void page_up(void) {
  g_cursor_line -= EDITOR_VISIBLE_LINES;
  if (g_cursor_line < 0) g_cursor_line = 0;
  if (g_cursor_col > (int)g_lens[g_cursor_line])
    g_cursor_col = (int)g_lens[g_cursor_line];
  g_view_top -= EDITOR_VISIBLE_LINES;
  if (g_view_top < 0) g_view_top = 0;
}

static void page_down(void) {
  g_cursor_line += EDITOR_VISIBLE_LINES;
  if (g_cursor_line >= g_line_count) g_cursor_line = g_line_count - 1;
  if (g_cursor_col > (int)g_lens[g_cursor_line])
    g_cursor_col = (int)g_lens[g_cursor_line];
  g_view_top += EDITOR_VISIBLE_LINES;
  if (g_view_top > g_line_count - 1) g_view_top = g_line_count - 1;
}

static void draw_status(void) {
  char status[96];
  char tmp[24];

  status[0] = '\0';
  if (g_message[0]) {
    str_copy(status, g_message, sizeof(status));
    str_cat(status, " | ", sizeof(status));
  }

  str_cat(status, g_insert_mode ? "INS" : "OVR", sizeof(status));
  str_cat(status, g_dirty ? " *" : " saved", sizeof(status));
  str_cat(status, " | ", sizeof(status));
  str_cat(status, g_name[0] ? g_name : "UNTITLED.TXT", sizeof(status));
  str_cat(status, " | line ", sizeof(status));
  u32_to_dec((uint32_t)(g_cursor_line + 1), tmp, sizeof(tmp));
  str_cat(status, tmp, sizeof(status));
  str_cat(status, "/", sizeof(status));
  u32_to_dec((uint32_t)g_line_count, tmp, sizeof(tmp));
  str_cat(status, tmp, sizeof(status));
  str_cat(status, " col ", sizeof(status));
  u32_to_dec((uint32_t)(g_cursor_col + 1), tmp, sizeof(tmp));
  str_cat(status, tmp, sizeof(status));
  str_cat(status, " | Ctrl+S save Ctrl+Q quit Del delete", sizeof(status));
  if (g_quit_armed)
    str_cat(status, " | press Ctrl+Q again", sizeof(status));

  tui_status_bar(status);
}

#ifdef SHOW_LINENOS
static void fmt_gutter(char out[6], int line_num) {
  /* Format: " NNN " where NNN is right-aligned 3-digit 1-based line number */
  uint32_t n = (uint32_t)line_num;
  out[0] = ' ';
  out[1] = (n >= 100u) ? (char)('0' + (n / 100u) % 10u) : ' ';
  out[2] = (n >=  10u) ? (char)('0' + (n /  10u) % 10u) : ' ';
  out[3] = (char)('0' + n % 10u);
  out[4] = ' ';
  out[5] = '\0';
}
#endif

static void draw_body(void) {
  uint8_t body_c = vga_make_color(VGA_COLOR_WHITE,     VGA_COLOR_BLACK);
#ifdef SHOW_LINENOS
  uint8_t gut_c  = vga_make_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
#endif

  for (int row = EDITOR_BODY_TOP; row <= EDITOR_BODY_BOTTOM; row++)
    vga_fill_row(row, ' ', body_c);

  for (int vis = 0; vis < EDITOR_VISIBLE_LINES; vis++) {
    int line = g_view_top + vis;
    int row  = EDITOR_BODY_TOP + vis;

#ifdef SHOW_LINENOS
    if (line < g_line_count) {
      char gutter[6];
      fmt_gutter(gutter, line + 1);
      tui_write_at(row, 0, gutter, gut_c);
      tui_write_at(row, EDITOR_GUTTER_WIDTH, g_lines[line], body_c);
    } else {
      tui_write_at(row, 0, "      ", gut_c);
    }
#else
    if (line < g_line_count)
      tui_write_at(row, 0, g_lines[line], body_c);
#endif
  }
}

static void render_editor(void) {
  char header[96];

  ensure_visible();
  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  vga_clear();
  vga_set_scroll_region(EDITOR_BODY_TOP, EDITOR_BODY_BOTTOM);

  tui_header_bar("ASWD OS Editor");
  header[0] = '\0';
  str_copy(header, "Editing ", sizeof(header));
  str_cat(header, g_name[0] ? g_name : "UNTITLED.TXT", sizeof(header));
  tui_write_at(0, 26, header, vga_make_color(VGA_COLOR_YELLOW, VGA_COLOR_BLUE));

  draw_body();
  draw_status();
  vga_set_cursor_pos(EDITOR_BODY_TOP + (g_cursor_line - g_view_top),
                     g_cursor_col + EDITOR_GUTTER_WIDTH);
}

static int handle_key(char c) {
  if (c != 0x11) g_quit_armed = 0;

  switch (c) {
    case KEY_LEFT:      move_left();  break;
    case KEY_RIGHT:     move_right(); break;
    case KEY_UP:        move_up();    break;
    case KEY_DOWN:      move_down();  break;
    case KEY_HOME:      g_cursor_col = 0; break;
    case KEY_END:       g_cursor_col = (int)g_lens[g_cursor_line]; break;
    case KEY_CTRL_HOME:
      g_cursor_line = 0;
      g_cursor_col  = 0;
      break;
    case KEY_CTRL_END:
      g_cursor_line = g_line_count - 1;
      g_cursor_col  = (int)g_lens[g_cursor_line];
      break;
    case KEY_PAGEUP:    page_up();    break;
    case KEY_PAGEDOWN:  page_down();  break;
    case KEY_DELETE:    delete_at_cursor(); break;
    case KEY_INSERT:
      g_insert_mode = !g_insert_mode;
      g_quit_armed  = 0;
      set_message(g_insert_mode ? "insert mode" : "overwrite mode");
      break;
    case '\b':  backspace_at_cursor(); break;
    case '\n':  split_current_line(); break;
    case 0x13:  /* Ctrl+S */ save_file(); break;
    case 0x11:  /* Ctrl+Q */
      if (g_dirty && !g_quit_armed) {
        g_quit_armed = 1;
        set_message("unsaved changes: press Ctrl+Q again");
        break;
      }
      g_quit_armed = 0;
      g_message[0] = '\0';
      return 1;
    default:
      if ((unsigned char)c >= 32 && (unsigned char)c <= 126)
        insert_char(c);
      break;
  }

  ensure_visible();
  render_editor();
  return 0;
}

int editor_open(const char *name) {
  load_file(name);
  pic_clear_mask(1);   /* re-unmask IRQ1 after any disk I/O in load_file */
  render_editor();

  for (;;) {
    char c = input_getchar();
    if (c == '\r') c = '\n';
    if (handle_key(c)) break;
  }

  return 1;
}
