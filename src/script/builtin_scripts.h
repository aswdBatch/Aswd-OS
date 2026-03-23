#pragma once

typedef struct {
  const char *name;
  const char *text;
} builtin_script_t;

const builtin_script_t *builtin_scripts_get(void);
int builtin_scripts_count(void);

