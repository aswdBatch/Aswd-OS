#pragma once

typedef enum {
  DIAGNOSTIC_TEST_NONE = 0,
  DIAGNOSTIC_TEST_SMOKE = 1,
  DIAGNOSTIC_TEST_TEMP_WRITE = 2,
  DIAGNOSTIC_TEST_KEYBOARD = 3,
  DIAGNOSTIC_TEST_FORCE_BUGCHECK = 4,
} diagnostic_test_mode_t;

int diagnostics_run_test(diagnostic_test_mode_t mode);
