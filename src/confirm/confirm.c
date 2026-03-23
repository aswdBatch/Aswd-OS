#include "confirm/confirm.h"

#include <stddef.h>

#include "common/colors.h"
#include "console/console.h"
#include "input/input.h"
#include "lib/ctype.h"
#include "lib/string.h"

static confirm_result_t g_last = CONFIRM_VETO;

static void normalize(char *s) {
  for (size_t i = 0; s[i]; i++) {
    s[i] = to_lower(s[i]);
  }
}

static int is_yes(const char *s) {
  return str_eq(s, "acknowledge") || str_eq(s, "ack") || str_eq(s, "yes") || str_eq(s, "y");
}

static int is_no(const char *s) {
  return str_eq(s, "veto") || str_eq(s, "v") || str_eq(s, "no") || str_eq(s, "n");
}

confirm_result_t confirm_prompt(const char *message) {
  char buf[64];

  console_writeln_colored(message, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
  console_write_colored("> ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
  console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

  input_readline(buf, sizeof(buf));
  normalize(buf);

  if (buf[0] == '\0') {
    g_last = CONFIRM_VETO;
    return g_last;
  }

  if (is_yes(buf)) {
    g_last = CONFIRM_ACK;
    return g_last;
  }

  if (is_no(buf)) {
    g_last = CONFIRM_VETO;
    return g_last;
  }

  console_writeln_colored("Invalid response; vetoed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  g_last = CONFIRM_VETO;
  return g_last;
}

confirm_result_t confirm_last_result(void) {
  return g_last;
}

int confirm_last_was_ack(void) {
  return g_last == CONFIRM_ACK;
}
