#include "script/vars.h"

#include <stddef.h>

#include "lib/string.h"

enum { VAR_MAX = 16, NAME_MAX = 16, VALUE_MAX = 128 };

static char g_names[VAR_MAX][NAME_MAX];
static char g_values[VAR_MAX][VALUE_MAX];

void script_vars_reset(void) {
  for (int i = 0; i < VAR_MAX; i++) {
    g_names[i][0] = '\0';
    g_values[i][0] = '\0';
  }
}

static int find_index(const char *name) {
  for (int i = 0; i < VAR_MAX; i++) {
    if (g_names[i][0] == '\0') {
      continue;
    }
    if (str_eq(g_names[i], name)) {
      return i;
    }
  }
  return -1;
}

static int find_empty(void) {
  for (int i = 0; i < VAR_MAX; i++) {
    if (g_names[i][0] == '\0') {
      return i;
    }
  }
  return -1;
}

void script_vars_set(const char *name, const char *value) {
  int idx = find_index(name);
  if (idx < 0) {
    idx = find_empty();
  }
  if (idx < 0) {
    return;
  }
  str_copy(g_names[idx], name, NAME_MAX);
  str_copy(g_values[idx], value, VALUE_MAX);
}

const char *script_vars_get(const char *name) {
  int idx = find_index(name);
  if (idx < 0) {
    return "";
  }
  return g_values[idx];
}

