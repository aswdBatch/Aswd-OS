#pragma once

typedef enum {
  SHELL_MODE_NORMAL = 0,
  SHELL_MODE_RAW = 1,
} shell_mode_t;

void shell_run(shell_mode_t mode);
shell_mode_t shell_get_mode(void);
int shell_is_raw_mode(void);
