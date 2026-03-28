#include <stdint.h>

#include "auth/auth_gui.h"
#include "boot/bootui.h"
#include "boot/multiboot.h"
#include "common/colors.h"
#include "common/config.h"
#include "console/console.h"
#include "cpu/bugcheck.h"
#include "cpu/idt.h"
#include "cpu/ports.h"
#include "cpu/timer.h"
#include "diagnostics/diagnostics.h"
#include "drivers/disk.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "explorer/explorer.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "shell/shell.h"
#include "net/net.h"
#include "tests/test_runner.h"
#include "usb/usb.h"
#include "users/users.h"

static void kernel_set_bugcheck_style(const boot_selection_t *selection) {
  bugcheck_set_style(selection->bugcheck_style == BOOT_BUGCHECK_LEGACY
      ? BUGCHECK_STYLE_LEGACY
      : BUGCHECK_STYLE_MODERN);
}

static void kernel_init_storage(int init_users) {
  boot_loading_step("Initializing storage");
  if (disk_init()) {
    boot_loading_step("Mounting filesystem");
    if (!vfs_init()) {
      boot_loading_step("Filesystem unavailable");
    } else if (init_users) {
      boot_loading_step("Preparing users");
    }

    if (init_users) {
      users_init();
    }
  } else {
    boot_loading_step("Storage unavailable");
    if (init_users) {
      users_init();
    }
  }
}

static void kernel_boot_fs_lab(const boot_selection_t *selection) {
  boot_loading_begin();
  kernel_init_storage(0);
  boot_loading_step("Starting timer");
  timer_init(100);
  boot_loading_step("Starting keyboard");
  keyboard_init();
  cpu_sti();
  boot_loading_step("Ready");
  boot_loading_finish();
  kernel_set_bugcheck_style(selection);
  shell_run(SHELL_MODE_RAW);
}

static void kernel_boot_normal(const boot_selection_t *selection) {
  boot_loading_begin();
  kernel_init_storage(1);
  boot_loading_step("Starting timer");
  timer_init(100);
  boot_loading_step("Starting keyboard");
  keyboard_init();
  cpu_sti();

  boot_loading_step("Scanning PCI");
  pci_init();
  boot_loading_step("Initializing USB");
  usb_init();
  boot_loading_step("Initializing network");
  net_init();

  boot_loading_step("Ready");
  boot_loading_finish();
  kernel_set_bugcheck_style(selection);

  auth_gui_run(selection->target);

  if (selection->test_mode != DIAGNOSTIC_TEST_NONE) {
    if (!diagnostics_run_test(selection->test_mode)) {
      console_writeln_colored("Boot diagnostics reported a failure.",
                              VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    } else {
      console_writeln_colored("Boot diagnostics passed.",
                              VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    }
  }

  if (selection->target == BOOT_TARGET_NORMAL_GUI) {
    mouse_init();
    for (;;) {
      gui_run();
      auth_gui_run(selection->target);
    }
  }

  if (selection->target == BOOT_TARGET_SHELL_ONLY) {
    shell_run(SHELL_MODE_RAW);
  } else {
    explorer_run();
  }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_addr) {
  idt_init();
  serial_init();
  multiboot_init(multiboot_magic, multiboot_addr);
  gfx_init();
  vga_init();
  vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
  console_init();

  if (multiboot_test_mode) {
    serial_write("[ASWD CI] Test mode detected.\r\n");
    kernel_init_storage(0);
    timer_init(100);
    keyboard_init();
    cpu_sti();
    tests_run_all();   /* does not return */
  }

  boot_selection_t selection;
  boot_launcher_run(&selection);

  if (selection.target == BOOT_TARGET_FS_LAB) {
    kernel_boot_fs_lab(&selection);
    return;
  }

  kernel_boot_normal(&selection);
}
