#pragma once

#include "diagnostics/diagnostics.h"

typedef enum {
  BOOT_TARGET_NORMAL_GUI = 0,
  BOOT_TARGET_TUI_LEGACY = 1,
  BOOT_TARGET_SHELL_ONLY = 2,
  BOOT_TARGET_FS_LAB = 3,
} boot_target_t;

typedef enum {
  BOOT_BUGCHECK_LEGACY = 0,
  BOOT_BUGCHECK_MODERN = 1,
} boot_bugcheck_style_t;

typedef struct {
  boot_target_t target;
  boot_bugcheck_style_t bugcheck_style;
  diagnostic_test_mode_t test_mode;
} boot_selection_t;

void boot_loading_begin(void);
void boot_loading_step(const char *stage);
void boot_loading_finish(void);

void boot_launcher_run(boot_selection_t *selection);
