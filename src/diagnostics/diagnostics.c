#include "diagnostics/diagnostics.h"

#include <stdint.h>

#include "common/colors.h"
#include "console/console.h"
#include "cpu/bugcheck.h"
#include "cpu/timer.h"
#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "shell/commands.h"

static int run_smoke_test(void) {
  char *help_argv[] = { "help" };
  char *osinfo_argv[] = { "osinfo" };

  console_writeln("[test] command dispatch");
  if (commands_dispatch(1, help_argv)) {
    return 0;
  }
  if (commands_dispatch(1, osinfo_argv)) {
    return 0;
  }

  return 1;
}

static int run_temp_write_test(void) {
  char name[13];
  char path[32];
  const char *payload = "aswd diagnostics temp write";
  uint8_t buf[64];
  uint32_t seed = timer_get_ticks() % 1000000u;

  if (!vfs_available()) {
    console_writeln_colored("[test] filesystem unavailable", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return 0;
  }

  name[0] = 'B';
  name[1] = 'T';
  name[2] = (char)('0' + (seed / 100000u) % 10u);
  name[3] = (char)('0' + (seed / 10000u) % 10u);
  name[4] = (char)('0' + (seed / 1000u) % 10u);
  name[5] = (char)('0' + (seed / 100u) % 10u);
  name[6] = (char)('0' + (seed / 10u) % 10u);
  name[7] = (char)('0' + seed % 10u);
  name[8] = '.';
  name[9] = 'T';
  name[10] = 'M';
  name[11] = 'P';
  name[12] = '\0';
  path[0] = '\0';
  str_copy(path, "/ROOT/", sizeof(path));
  str_cat(path, name, sizeof(path));

  console_writeln("[test] temp write");
  console_writeln("[test]   create");
  if (vfs_write(path, (const uint8_t *)payload, (uint32_t)str_len(payload)) < 0) {
    console_writeln_colored("[test] write failed", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return 0;
  }

  console_writeln("[test]   readback");
  int read = vfs_cat(path, buf, (int)sizeof(buf) - 1);
  if (read < 0) {
    console_writeln_colored("[test] readback failed", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    (void)vfs_rm(path);
    return 0;
  }

  buf[read] = '\0';
  if (!str_eq((const char *)buf, payload)) {
    console_writeln_colored("[test] verification mismatch", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    (void)vfs_rm(path);
    return 0;
  }

  console_writeln("[test]   cleanup");
  if (vfs_rm(path) < 0) {
    console_writeln_colored("[test] cleanup failed", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    return 0;
  }

  console_writeln_colored("[test] temp write passed", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
  return 1;
}

static int run_keyboard_test(void) {
  int ps2 = keyboard_ps2_ready();
  if (ps2) {
    console_writeln_colored("[kbd] PS/2 keyboard: ready", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    console_writeln("[kbd] Input path: PS/2 IRQ1");
  } else {
    console_writeln_colored("[kbd] PS/2 keyboard: not detected", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    console_writeln("[kbd] Input path: BIOS trampoline (INT 16h) + BIOS buffer");
  }
  return 1;
}

int diagnostics_run_test(diagnostic_test_mode_t mode) {
  switch (mode) {
    case DIAGNOSTIC_TEST_NONE:
      return 1;
    case DIAGNOSTIC_TEST_SMOKE:
      return run_smoke_test();
    case DIAGNOSTIC_TEST_TEMP_WRITE:
      return run_smoke_test() && run_temp_write_test();
    case DIAGNOSTIC_TEST_KEYBOARD:
      return run_keyboard_test();
    case DIAGNOSTIC_TEST_FORCE_BUGCHECK:
      bugcheck("DIAGNOSTIC_BUGCHECK", "Triggered by diagnostics test");
  }

  return 0;
}
