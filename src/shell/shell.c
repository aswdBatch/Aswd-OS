#include "shell/shell.h"

#include "common/colors.h"
#include "console/console.h"
#include "drivers/disk.h"
#include "drivers/fat32.h"
#include "fs/vfs.h"
#include "input/input.h"
#include "drivers/vga.h"
#include "lib/string.h"
#include "shell/commands.h"
#include "tui/tui.h"

static void shell_refresh_status(void) {
  char status[128];

  status[0] = '\0';
  str_copy(status, "cwd: ", sizeof(status));
  str_cat(status, vfs_available() ? vfs_cwd_path() : "/", sizeof(status));
  tui_status_bar(status);
}

static shell_mode_t g_shell_mode = SHELL_MODE_NORMAL;

static void shell_print_u32(uint32_t value) {
  char buf[16];
  u32_to_dec(value, buf, sizeof(buf));
  console_write(buf);
}

static void shell_print_hex8(uint8_t value) {
  static const char k_hex[] = "0123456789ABCDEF";
  char buf[3];
  buf[0] = k_hex[(value >> 4) & 0x0F];
  buf[1] = k_hex[value & 0x0F];
  buf[2] = '\0';
  console_write(buf);
}

static const char *shell_disk_backend_name(void) {
  switch (disk_backend()) {
    case DISK_BACKEND_ATA: return "ATA";
    case DISK_BACKEND_BIOS_TRAMPOLINE: return "BIOS trampoline";
    default: return "none";
  }
}

static void shell_print_storage_summary(void) {
  console_write("Storage backend: ");
  console_writeln(shell_disk_backend_name());

  console_write("Partition LBA: ");
  shell_print_u32(disk_partition_start());
  console_putc('\n');

  if (disk_backend() == DISK_BACKEND_BIOS_TRAMPOLINE) {
    console_write("Boot drive: 0x");
    shell_print_hex8(disk_bios_boot_drive());
    console_putc('\n');
  }

  if (vfs_available()) {
    fat32_info_t info;
    if (fat32_get_info(&info)) {
      console_write("FAT32 root cluster: ");
      shell_print_u32(info.root_cluster);
      console_putc('\n');
      console_write("FAT start LBA: ");
      shell_print_u32(info.fat_start_lba);
      console_write("  Data start LBA: ");
      shell_print_u32(info.data_start_lba);
      console_putc('\n');
    }
  }
}

shell_mode_t shell_get_mode(void) {
  return g_shell_mode;
}

int shell_is_raw_mode(void) {
  return g_shell_mode == SHELL_MODE_RAW;
}

void shell_run(shell_mode_t mode) {
  g_shell_mode = mode;
  char line[256];
  char *argv[16];

  console_set_mode(CONSOLE_MODE_SHELL);
  if (g_shell_mode == SHELL_MODE_RAW) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
    vga_set_scroll_region(0, 24);
    console_writeln("Raw shell mode.");
    shell_print_storage_summary();
    console_writeln("Type 'help' for commands.");
  } else {
    tui_shell_frame(vfs_available() ? vfs_cwd_path() : "/");
  }

  for (;;) {
    if (g_shell_mode != SHELL_MODE_RAW) {
      shell_refresh_status();
      if (vfs_available()) {
        console_write_colored(vfs_cwd_path(), VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        console_write_colored(" > ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
      } else {
        console_write_colored("aswd > ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
      }
    } else {
      console_write_colored("aswd> ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    }
    console_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    input_readline(line, sizeof(line));

    int argc = split_args(line, argv, 16);
    if (argc == 0) {
      continue;
    }

    if (commands_dispatch(argc, argv)) {
      break;
    }
  }
}
