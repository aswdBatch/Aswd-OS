#include "input/input.h"

#include "console/console.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/serial.h"
#include "lib/string.h"

#define HISTORY_MAX 16
#define HISTORY_BUF 256
static char g_history[HISTORY_MAX][HISTORY_BUF];
static int  g_hist_count = 0;
static int  g_hist_head  = 0;

static void history_push(const char *line) {
    if (!line || !line[0]) return;
    int last = (g_hist_head - 1 + HISTORY_MAX) % HISTORY_MAX;
    if (g_hist_count > 0 && str_eq(g_history[last], line)) return;
    str_copy(g_history[g_hist_head], line, HISTORY_BUF);
    g_hist_head = (g_hist_head + 1) % HISTORY_MAX;
    if (g_hist_count < HISTORY_MAX) g_hist_count++;
}

static const char *history_get(int offset) {
    if (offset < 0 || offset >= g_hist_count) return (void*)0;
    int idx = (g_hist_head - 1 - offset + HISTORY_MAX * 2) % HISTORY_MAX;
    return g_history[idx];
}

int input_try_getchar(char *out) {
  if (serial_try_getchar(out)) return 1;
  if (keyboard_try_getchar(out)) return 1;
  return 0;
}

int input_try_get_event(input_event_t *out) {
  char c;
  mouse_event_t mouse;
  if (!out) return 0;

  if (serial_try_getchar(&c) || keyboard_try_getchar(&c)) {
    out->type = INPUT_EVENT_KEY;
    out->key.ch = c;
    out->key.pressed = 1;
    return 1;
  }

  if (mouse_poll(&mouse)) {
    out->type = INPUT_EVENT_POINTER;
    out->pointer.dx = mouse.dx;
    out->pointer.dy = mouse.dy;
    out->pointer.x = mouse.x;
    out->pointer.y = mouse.y;
    out->pointer.buttons = mouse.buttons;
    out->pointer.changed = mouse.changed;
    out->pointer.pressed = mouse.pressed;
    out->pointer.released = mouse.released;
    out->pointer.source = mouse.source;
    return 1;
  }

  return 0;
}

char input_getchar(void) {
  char c;
  for (;;) {
    if (input_try_getchar(&c)) return c;
    __asm__ volatile("sti; hlt");
  }
}

void input_readline(char *buf, size_t buf_size) {
  if (buf_size == 0) return;

  size_t len = 0;
  size_t pos = 0;   /* cursor position within buf */
  int hist_pos = -1;
  char saved_line[HISTORY_BUF];
  saved_line[0] = '\0';
  buf[0] = '\0';

  for (;;) {
    char c = input_getchar();

    if (c == '\r') c = '\n';
    if (c == (char)0x7F) c = '\b';

    /* --- Enter --- */
    if (c == '\n') {
      console_putc('\n');
      history_push(buf);
      break;
    }

    /* --- Arrow keys --- */
    if (c == KEY_UP || c == KEY_DOWN) {
      int new_pos;
      if (c == KEY_UP) {
        if (hist_pos == -1) str_copy(saved_line, buf, HISTORY_BUF);
        new_pos = hist_pos + 1;
      } else {
        new_pos = hist_pos - 1;
      }
      const char *entry;
      if (new_pos == -1) {
        entry = saved_line;
      } else {
        entry = history_get(new_pos);
        if (!entry) continue;
      }
      /* erase current input */
      for (size_t i = 0; i < pos; i++) console_putc('\b');
      for (size_t i = 0; i < len; i++) console_putc(' ');
      for (size_t i = 0; i < len; i++) console_putc('\b');
      /* load new entry */
      str_copy(buf, entry, buf_size);
      len = str_len(buf);
      pos = len;
      hist_pos = new_pos;
      /* reprint */
      for (size_t i = 0; i < len; i++) console_putc(buf[i]);
      continue;
    }
    if (c == KEY_LEFT) {
      if (pos > 0) {
        pos--;
        console_putc('\b');
      }
      continue;
    }
    if (c == KEY_RIGHT) {
      if (pos < len) {
        console_putc(buf[pos]);
        pos++;
      }
      continue;
    }

    /* --- Backspace --- */
    if (c == '\b') {
      if (pos == 0) continue;
      /* move cursor left */
      console_putc('\b');
      /* shift buf[pos-1 .. len-1] left by 1 */
      for (size_t i = pos - 1; i < len - 1; i++) {
        buf[i] = buf[i + 1];
      }
      len--;
      pos--;
      buf[len] = '\0';
      /* reprint suffix + blank last char */
      for (size_t i = pos; i < len; i++) {
        console_putc(buf[i]);
      }
      console_putc(' ');
      /* backtrack cursor to pos */
      for (size_t i = pos; i <= len; i++) {
        console_putc('\b');
      }
      continue;
    }

    /* --- Ignore non-printable and extended keys --- */
    if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;

    /* --- Insert printable char at pos --- */
    if (len + 1 >= buf_size) continue;

    /* shift buf[pos .. len] right by 1 */
    for (size_t i = len; i > pos; i--) {
      buf[i] = buf[i - 1];
    }
    buf[pos] = c;
    len++;
    buf[len] = '\0';

    /* write char + reprint suffix */
    console_putc(c);
    for (size_t i = pos + 1; i < len; i++) {
      console_putc(buf[i]);
    }
    pos++;
    /* backtrack cursor to pos */
    for (size_t i = pos; i < len; i++) {
      console_putc('\b');
    }
  }
}
