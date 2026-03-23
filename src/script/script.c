#include "script/script.h"

#include <stddef.h>

#include "common/colors.h"
#include "console/console.h"
#include "confirm/confirm.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "script/builtin_scripts.h"
#include "script/vars.h"
#include "shell/commands.h"

static const builtin_script_t *find_script(const char *name) {
  const builtin_script_t *scripts = builtin_scripts_get();
  int count = builtin_scripts_count();
  for (int i = 0; i < count; i++) {
    if (str_eq(scripts[i].name, name)) {
      return &scripts[i];
    }
  }
  return 0;
}

static void expand_vars(const char *in, char *out, size_t out_size) {
  size_t o = 0;
  for (size_t i = 0; in[i] && o + 1 < out_size; i++) {
    if (in[i] != '$') {
      out[o++] = in[i];
      continue;
    }

    i++;
    char name[16];
    size_t n = 0;
    while (in[i] && in[i] != ' ' && in[i] != '\t' && in[i] != '$' && n + 1 < sizeof(name)) {
      name[n++] = in[i++];
    }
    name[n] = '\0';
    i--;

    const char *value = script_vars_get(name);
    for (size_t k = 0; value[k] && o + 1 < out_size; k++) {
      out[o++] = value[k];
    }
  }
  out[o] = '\0';
}

static int handle_set(const char *line) {
  if (str_ncmp(line, "set ", 4) != 0) {
    return 0;
  }

  const char *p = line + 4;
  while (*p == ' ' || *p == '\t') {
    p++;
  }

  char name[16];
  char value[128];
  size_t ni = 0;
  size_t vi = 0;
  int seen_eq = 0;

  for (; *p; p++) {
    if (!seen_eq) {
      if (*p == '=') {
        seen_eq = 1;
        continue;
      }
      if (ni + 1 < sizeof(name)) {
        name[ni++] = *p;
      }
    } else {
      if (vi + 1 < sizeof(value)) {
        value[vi++] = *p;
      }
    }
  }
  name[ni] = '\0';
  value[vi] = '\0';

  if (!seen_eq || name[0] == '\0') {
    return 1;
  }

  script_vars_set(name, value);
  return 1;
}

static int handle_iflast(const char *line, int *skip_next) {
  if (str_ncmp(line, "iflast ", 7) != 0) {
    return 0;
  }

  const char *p = line + 7;
  while (*p == ' ' || *p == '\t') {
    p++;
  }

  if (str_eq(p, "ack")) {
    if (!confirm_last_was_ack()) {
      *skip_next = 1;
    }
    return 1;
  }

  return 1;
}

static uint8_t g_script_buf[4096];

static int run_text(const char *text) {
  console_mode_t old_mode = console_get_mode();
  console_set_mode(CONSOLE_MODE_SCRIPT);
  console_writeln("Running script...");

  script_vars_reset();

  int skip_next = 0;
  const char *p = text;

  while (*p) {
    char line[256];
    size_t li = 0;
    while (*p && *p != '\n' && li + 1 < sizeof(line)) {
      char c = *p++;
      if (c != '\r') {
        line[li++] = c;
      }
    }
    if (*p == '\n') {
      p++;
    }
    line[li] = '\0';

    if (skip_next) {
      skip_next = 0;
      continue;
    }

    char *s = line;
    while (*s == ' ' || *s == '\t') {
      s++;
    }
    if (*s == '\0' || *s == '#') {
      continue;
    }

    if (handle_set(s)) {
      continue;
    }
    if (handle_iflast(s, &skip_next)) {
      continue;
    }

    char expanded[256];
    expand_vars(s, expanded, sizeof(expanded));

    char argv_buf[256];
    str_copy(argv_buf, expanded, sizeof(argv_buf));

    char *argv[16];
    int argc = split_args(argv_buf, argv, 16);
    if (argc == 0) {
      continue;
    }

    commands_dispatch(argc, argv);
  }

  console_set_mode(old_mode);
  return 1;
}

int script_run(const char *name) {
  const builtin_script_t *script = find_script(name);
  if (script) {
    return run_text(script->text);
  }

  /* Fall back to loading from the filesystem */
  if (vfs_available()) {
    int n = vfs_cat(name, g_script_buf, (int)sizeof(g_script_buf) - 1);
    if (n <= 0) {
      /* Try appending .aswd extension */
      char fname[32];
      str_copy(fname, name, sizeof(fname));
      str_cat(fname, ".aswd", sizeof(fname));
      n = vfs_cat(fname, g_script_buf, (int)sizeof(g_script_buf) - 1);
    }
    if (n > 0) {
      g_script_buf[n] = '\0';
      return run_text((const char *)g_script_buf);
    }
  }

  console_writeln_colored("Script not found.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  return 0;
}

