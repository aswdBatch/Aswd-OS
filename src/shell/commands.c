#include "shell/commands.h"
#include "gui/axapp_gui.h"

#include <stddef.h>
#include <stdint.h>

#include "common/boot_log.h"
#include "common/changelog.h"
#include "common/colors.h"
#include "common/config.h"
#include "common/power.h"
#include "console/console.h"
#include "lang/lang.h"
#include "confirm/confirm.h"
#include "cpu/ports.h"
#include "cpu/timer.h"
#include "drivers/disk.h"
#include "drivers/fat32.h"
#include "drivers/vga.h"
#include "editor/editor.h"
#include "fs/vfs.h"
#include "input/input.h"
#include "lib/string.h"
#include "net/dns.h"
#include "net/http.h"
#include "net/icmp.h"
#include "net/net.h"
#include "shell/sysinfo.h"
#include "shell/shell.h"
#include "script/script.h"
#include "tui/tui.h"

typedef void (*cmd_fn_t)(int argc, char *argv[]);

typedef struct {
  const char *name;
  const char *help;
  cmd_fn_t fn;
} command_t;

static void cmd_help(int argc, char *argv[]);
static void cmd_osinfo(int argc, char *argv[]);
static void cmd_sysinfo(int argc, char *argv[]);
static void cmd_clear(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_run(int argc, char *argv[]);
static void cmd_confirm(int argc, char *argv[]);
static void cmd_pwd(int argc, char *argv[]);
static void cmd_ls(int argc, char *argv[]);
static void cmd_df(int argc, char *argv[]);
static void cmd_cd(int argc, char *argv[]);
static void cmd_cat(int argc, char *argv[]);
static void cmd_write(int argc, char *argv[]);
static void cmd_rm(int argc, char *argv[]);
static void cmd_cp(int argc, char *argv[]);
static void cmd_mv(int argc, char *argv[]);
static void cmd_mkdir(int argc, char *argv[]);
static void cmd_rmdir(int argc, char *argv[]);
static void cmd_edit(int argc, char *argv[]);
static void cmd_exit(int argc, char *argv[]);
static void cmd_disktest(int argc, char *argv[]);
static void cmd_diskinfo(int argc, char *argv[]);
static void cmd_bpb(int argc, char *argv[]);
static void cmd_fattest(int argc, char *argv[]);
static void cmd_vfstest(int argc, char *argv[]);
static void cmd_reboot(int argc, char *argv[]);
static void cmd_rebootaswd(int argc, char *argv[]);
static void cmd_date(int argc, char *argv[]);
static void cmd_ping(int argc, char *argv[]);
static void cmd_fetch(int argc, char *argv[]);
static void cmd_nslookup(int argc, char *argv[]);
static void cmd_ifconfig(int argc, char *argv[]);
static void cmd_ax(int argc, char *argv[]);
static void cmd_axapp(int argc, char *argv[]);
static void cmd_find(int argc, char *argv[]);
static void cmd_grep(int argc, char *argv[]);
static void cmd_history(int argc, char *argv[]);
static void cmd_ln(int argc, char *argv[]);
static void cmd_inspect(int argc, char *argv[]);
static void cmd_test(int argc, char *argv[]);
static void cmd_compile(int argc, char *argv[]);
static void cmd_logs(int argc, char *argv[]);

static const command_t g_cmds[] = {
    {"help", "List commands", cmd_help},
    {"osinfo", "Show OS version and changelog", cmd_osinfo},
    {"sysinfo", "Show CPU/RAM info", cmd_sysinfo},
    {"clear", "Reset the shell frame", cmd_clear},
    {"echo", "Echo text", cmd_echo},
    {"run", "Run script (built-in)", cmd_run},
    {"confirm", "Ask for confirmation", cmd_confirm},
    {"pwd", "Print current directory", cmd_pwd},
    {"ls", "List directory (ls -l time/size, ls -s human sizes)", cmd_ls},
    {"df", "Show disk usage", cmd_df},
    {"cd", "Change directory", cmd_cd},
    {"cat", "Print a file", cmd_cat},
    {"write", "Write text to a file", cmd_write},
    {"rm", "Remove file (send to /ROOT/RECYCLE.BIN); rm -f deletes permanently", cmd_rm},
    {"cp", "Copy file or cp -r directory", cmd_cp},
    {"mv", "Move or rename a file", cmd_mv},
    {"mkdir", "Create a directory", cmd_mkdir},
    {"rmdir", "Remove an empty directory", cmd_rmdir},
    {"edit", "Open the text editor", cmd_edit},
    {"exit", "Return to Program Manager", cmd_exit},
    {"disktest", "Read the boot sector", cmd_disktest},
    {"diskinfo", "Show disk backend diagnostics", cmd_diskinfo},
    {"bpb", "Show FAT32 boot sector fields", cmd_bpb},
    {"fattest", "List the current FAT directory", cmd_fattest},
    {"vfstest", "Exercise basic file ops", cmd_vfstest},
    {"reboot",    "Reboot the system",          cmd_reboot},
    {"rebootaswd","Reboot to Aswd OS (USB)",    cmd_rebootaswd},
    {"date",      "Show current date and time", cmd_date},
    {"ping",      "Ping an IP address",         cmd_ping},
    {"fetch",     "HTTP GET a URL",             cmd_fetch},
    {"nslookup",  "DNS lookup a hostname",      cmd_nslookup},
    {"ifconfig",  "Show network configuration", cmd_ifconfig},
    {"ax",        "Run an Ax (.ax) script",     cmd_ax},
    {"axapp",     "Open an AX app (.ax project)", cmd_axapp},
    {"find",      "Find entries in cwd matching substring", cmd_find},
    {"grep",      "Search lines in a file for substring", cmd_grep},
    {"history",   "Show shell input history", cmd_history},
    {"ln",        "Link files (not supported on FAT32)", cmd_ln},
    {"inspect",   "Show FAT volume summary", cmd_inspect},
    {"test",      "Run scripts under /ROOT/tests/", cmd_test},
    {"compile",   "Remote compile (not configured)", cmd_compile},
    {"logs",      "Show boot log lines", cmd_logs},
};

void commands_init(void) {}

static const command_t *find_cmd(const char *name) {
  for (size_t i = 0; i < sizeof(g_cmds) / sizeof(g_cmds[0]); i++) {
    if (str_eq(g_cmds[i].name, name)) {
      return &g_cmds[i];
    }
  }
  return 0;
}

static void print_uint32(uint32_t value) {
  char buf[16];
  u32_to_dec(value, buf, sizeof(buf));
  console_write(buf);
}

static void u64_to_dec_local(uint64_t value, char *out, size_t out_size) {
  char tmp[32];
  size_t idx = 0;
  size_t o = 0;

  if (out_size == 0) {
    return;
  }

  if (value == 0) {
    out[0] = '0';
    if (out_size > 1) {
      out[1] = '\0';
    }
    return;
  }

  while (value > 0 && idx < sizeof(tmp)) {
    tmp[idx++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  while (idx > 0 && o + 1 < out_size) {
    out[o++] = tmp[--idx];
  }
  out[o] = '\0';
}

static void print_uint64(uint64_t value) {
  char buf[32];
  u64_to_dec_local(value, buf, sizeof(buf));
  console_write(buf);
}

static void print_human_size(uint64_t bytes) {
  uint64_t scale = 1;
  const char *suffix = "B";

  if (bytes >= 1073741824ull) {
    scale = 1073741824ull;
    suffix = "G";
  } else if (bytes >= 1048576ull) {
    scale = 1048576ull;
    suffix = "M";
  } else if (bytes >= 1024ull) {
    scale = 1024ull;
    suffix = "K";
  }

  if (scale == 1) {
    print_uint64(bytes);
    console_write(suffix);
    return;
  }

  {
    uint64_t tenths = (bytes * 10ull + (scale / 2ull)) / scale;
    uint64_t whole = tenths / 10ull;
    uint64_t frac = tenths % 10ull;

    print_uint64(whole);
    if (whole < 100ull && frac != 0) {
      console_putc('.');
      print_uint64(frac);
    }
    console_write(suffix);
  }
}

static void print_hex8(uint8_t value) {
  static const char k_hex[] = "0123456789ABCDEF";
  char buf[3];
  buf[0] = k_hex[(value >> 4) & 0x0F];
  buf[1] = k_hex[value & 0x0F];
  buf[2] = '\0';
  console_write(buf);
}

static const char *disk_backend_name(void) {
  switch (disk_backend()) {
    case DISK_BACKEND_ATA: return "ATA";
    case DISK_BACKEND_BIOS_TRAMPOLINE: return "BIOS_TRAMPOLINE";
    default: return "NONE";
  }
}

static const char *disk_op_name(void) {
  switch (disk_last_op()) {
    case DISK_OP_READ: return "read";
    case DISK_OP_WRITE: return "write";
    default: return "none";
  }
}

static int buffer_equals(const uint8_t *buf, int len, const char *text) {
  int text_len = (int)str_len(text);
  if (len != text_len) {
    return 0;
  }
  return str_ncmp((const char *)buf, text, (size_t)text_len) == 0;
}

static void vfstest_fail(const char *step) {
  console_write_colored("vfstest failed at: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  console_writeln_colored(step, VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
}

static void print_protected_location(void) {
  console_writeln_colored("protected system location", VGA_COLOR_LIGHT_RED,
                          VGA_COLOR_BLACK);
}

static void print_changelog_entry(const changelog_entry_t *entry) {
  if (!entry) {
    return;
  }

  console_write("Latest update: ");
  console_write(entry->version);
  if (entry->date && entry->date[0]) {
    console_write(" (");
    console_write(entry->date);
    console_write(")");
  }
  console_putc('\n');

  if (entry->summary && entry->summary[0]) {
    console_writeln(entry->summary);
  }

  for (int i = 0; i < entry->note_count; i++) {
    console_write("  - ");
    console_writeln(entry->notes[i]);
  }
}

static int command_targets_writable_workspace(const char *path) {
  if (!path || !path[0]) {
    return 0;
  }
  if (path[0] != '/') {
    return vfs_cwd_is_writable();
  }
  if (str_eq(path, "/ROOT")) {
    return 1;
  }
  return str_ncmp(path, "/ROOT", 5) == 0 && path[5] == '/';
}

static void print_usage(const char *name, const char *usage) {
  console_write_colored("Usage: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  console_write(name);
  if (usage && usage[0]) {
    console_write(" ");
    console_writeln(usage);
  } else {
    console_putc('\n');
  }
}

static void shell_restore_frame(void) {
  console_set_mode(CONSOLE_MODE_SHELL);
  if (shell_is_raw_mode()) {
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_clear();
    vga_set_scroll_region(0, 24);
    return;
  }
  tui_shell_frame(vfs_available() ? vfs_cwd_path() : "/");
}

static int g_exit_requested = 0;

int commands_dispatch(int argc, char *argv[]) {
  g_exit_requested = 0;
  const command_t *cmd = find_cmd(argv[0]);
  if (!cmd) {
    console_writeln_colored("Unknown command. Type: help", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return 0;
  }
  cmd->fn(argc, argv);
  return g_exit_requested;
}

static void cmd_help(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln("Commands:");
  for (size_t i = 0; i < sizeof(g_cmds) / sizeof(g_cmds[0]); i++) {
    if (shell_is_raw_mode() && str_eq(g_cmds[i].name, "edit")) {
      continue;
    }
    console_write("  ");
    console_write(g_cmds[i].name);
    console_write(" - ");
    if (shell_is_raw_mode() && str_eq(g_cmds[i].name, "exit")) {
      console_writeln("Raw shell mode. Restart to reach Program Manager.");
    } else {
      console_writeln(g_cmds[i].help);
    }
  }
  console_writeln("Scripts:");
  console_writeln("  run demo");
}

static void cmd_osinfo(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln(ASWD_OS_BANNER);
  console_writeln(ASWD_OS_HELLO);
  console_write("Workspace: ");
  console_writeln("/ROOT only");
  console_write("Filesystem: ");
  console_writeln(vfs_available() ? "Mounted" : "Unavailable");
  console_putc('\n');
  print_changelog_entry(changelog_latest());
  console_putc('\n');
  console_writeln("Desktop app: open OS Info for the full release notes.");
}

static void cmd_sysinfo(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  sysinfo_print();
}

static void cmd_clear(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  shell_restore_frame();
}

static void cmd_echo(int argc, char *argv[]) {
  if (argc <= 1) {
    console_writeln("");
    return;
  }
  for (int i = 1; i < argc; i++) {
    console_write(argv[i]);
    if (i + 1 < argc) {
      console_write(" ");
    }
  }
  console_putc('\n');
}

static void cmd_run(int argc, char *argv[]) {
  int len;
  if (argc < 2) {
    console_writeln_colored("Usage: run <script>", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }
  len = (int)str_len(argv[1]);
  if (len >= 3 && argv[1][len-3] == '.' &&
      argv[1][len-2] == 'a' && argv[1][len-1] == 'x') {
    lang_run_file(argv[1]);
    return;
  }
  script_run(argv[1]);
}

static void cmd_ax(int argc, char *argv[]) {
  if (argc < 2) {
    console_writeln_colored("Usage: ax <file.ax>", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }
  lang_run_file(argv[1]);
}

static void cmd_axapp(int argc, char *argv[]) {
  if (argc < 2) {
    console_writeln("usage: axapp <file.ax>");
    return;
  }
  axapp_gui_launch_file(argv[1]);
}

static void cmd_confirm(int argc, char *argv[]) {
  char msg[128];
  msg[0] = '\0';

  if (argc <= 1) {
    str_copy(msg, "Confirm?", sizeof(msg));
  } else {
    for (int i = 1; i < argc; i++) {
      if (i > 1) {
        str_cat(msg, " ", sizeof(msg));
      }
      str_cat(msg, argv[i], sizeof(msg));
    }
  }

  confirm_prompt(msg);
}

static void cmd_pwd(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }
  console_writeln(vfs_cwd_path());
}

static void format_dos_stamp(uint16_t d, uint16_t t, char *buf, size_t buf_sz) {
  unsigned year = 1980u + (unsigned)((d >> 9) & 0x7Fu);
  unsigned mon  = (unsigned)((d >> 5) & 0xFu);
  unsigned day  = (unsigned)(d & 0x1Fu);
  unsigned hour = (unsigned)((t >> 11) & 0x1Fu);
  unsigned min  = (unsigned)((t >> 5) & 0x3Fu);
  char yb[8];

  if (buf_sz < 20) {
    if (buf_sz) buf[0] = '\0';
    return;
  }
  u32_to_dec(year, yb, sizeof(yb));
  str_copy(buf, yb, buf_sz);
  str_cat(buf, "-", buf_sz);
  if (mon < 10) str_cat(buf, "0", buf_sz);
  {
    char tb[6];
    u32_to_dec(mon, tb, sizeof(tb));
    str_cat(buf, tb, buf_sz);
  }
  str_cat(buf, "-", buf_sz);
  if (day < 10) str_cat(buf, "0", buf_sz);
  {
    char tb[6];
    u32_to_dec(day, tb, sizeof(tb));
    str_cat(buf, tb, buf_sz);
  }
  str_cat(buf, " ", buf_sz);
  if (hour < 10) str_cat(buf, "0", buf_sz);
  {
    char tb[6];
    u32_to_dec(hour, tb, sizeof(tb));
    str_cat(buf, tb, buf_sz);
  }
  str_cat(buf, ":", buf_sz);
  if (min < 10) str_cat(buf, "0", buf_sz);
  {
    char tb[6];
    u32_to_dec(min, tb, sizeof(tb));
    str_cat(buf, tb, buf_sz);
  }
}

static void cmd_ls(int argc, char *argv[]) {
  int show_sizes = 0;
  int show_long = 0;

  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  for (int i = 1; i < argc; i++) {
    if (str_eq(argv[i], "-s")) {
      show_sizes = 1;
    } else if (str_eq(argv[i], "-l")) {
      show_long = 1;
    } else {
      print_usage("ls", "[-l] [-s]");
      return;
    }
  }

  fat32_entry_t entries[64];
  int count = vfs_ls(entries, (int)(sizeof(entries) / sizeof(entries[0])));
  if (count < 0) {
    console_writeln_colored("ls failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  console_writeln(vfs_cwd_path());
  for (int i = 0; i < count; i++) {
    if (show_long) {
      char ts[24];
      format_dos_stamp(entries[i].mod_date, entries[i].mod_time, ts, sizeof(ts));
      console_write(ts);
      console_write("  ");
    }
    if (entries[i].is_dir) {
      console_write_colored("<DIR> ", VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
      console_writeln_colored(entries[i].name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    } else {
      if (!show_long) {
        console_write("      ");
      }
      if (show_sizes || show_long) {
        print_human_size(entries[i].size);
      } else {
        char sizebuf[16];
        u32_to_dec(entries[i].size, sizebuf, sizeof(sizebuf));
        console_write(sizebuf);
      }
      console_write(" ");
      console_writeln_colored(entries[i].name, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
  }
}

static void cmd_df(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  fat32_usage_t usage;
  if (!vfs_available() || !fat32_get_usage(&usage)) {
    console_writeln_colored("Filesystem metadata unavailable.", VGA_COLOR_LIGHT_RED,
                            VGA_COLOR_BLACK);
    return;
  }

  console_writeln("Filesystem  Size    Used    Free");
  console_write("FAT32       ");
  print_human_size(usage.total_bytes);
  console_write("  ");
  print_human_size(usage.used_bytes);
  console_write("  ");
  print_human_size(usage.free_bytes);
  console_putc('\n');

  console_write("total bytes: ");
  print_uint64(usage.total_bytes);
  console_putc('\n');
  console_write("used bytes:  ");
  print_uint64(usage.used_bytes);
  console_putc('\n');
  console_write("free bytes:  ");
  print_uint64(usage.free_bytes);
  console_putc('\n');

  console_write("clusters: ");
  print_uint32(usage.used_clusters);
  console_write(" used, ");
  print_uint32(usage.free_clusters);
  console_write(" free, ");
  print_uint32(usage.bytes_per_cluster);
  console_writeln(" bytes/cluster");
}

static void cmd_cd(int argc, char *argv[]) {
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  const char *target = (argc >= 2) ? argv[1] : "/";
  if (!vfs_cd(target)) {
    console_writeln_colored("cd failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }
  console_writeln(vfs_cwd_path());
}

static void cmd_cat(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage("cat", "<file>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  uint8_t buf[2048];
  int n = vfs_cat(argv[1], buf, (int)sizeof(buf) - 1);
  if (n < 0) {
    console_writeln_colored("cat failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  buf[n] = '\0';
  console_write((const char *)buf);
  if (n == 0 || buf[n - 1] != '\n') {
    console_putc('\n');
  }
}

static void cmd_write(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage("write", "<file> [text...]");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  char data[1024];
  data[0] = '\0';
  for (int i = 2; i < argc; i++) {
    if (i > 2) {
      str_cat(data, " ", sizeof(data));
    }
    str_cat(data, argv[i], sizeof(data));
  }

  int written = vfs_write(argv[1], (const uint8_t *)data, (uint32_t)str_len(data));
  if (written < 0) {
    if (!command_targets_writable_workspace(argv[1])) {
      print_protected_location();
    } else {
      console_writeln_colored("write failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    return;
  }
  console_write_colored("wrote ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
  print_uint32((uint32_t)written);
  console_writeln_colored(" bytes", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void cmd_rm(int argc, char *argv[]) {
  int force = 0;
  int fi = 1;

  while (fi < argc && str_eq(argv[fi], "-f")) {
    force = 1;
    fi++;
  }
  if (fi >= argc) {
    print_usage("rm", "[-f] <file>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  int rc = force ? vfs_rm_force(argv[fi]) : vfs_rm(argv[fi]);
  if (rc < 0) {
    if (!command_targets_writable_workspace(argv[fi])) {
      print_protected_location();
    } else {
      console_writeln_colored("rm failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    return;
  }
  if (rc == 0) {
    console_writeln_colored("file not found.", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    return;
  }
  console_writeln_colored(force ? "deleted" : "moved to recycle bin",
                         VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void cmd_cp(int argc, char *argv[]) {
  int recursive = 0;
  int ai = 1;

  while (ai < argc && str_eq(argv[ai], "-r")) {
    recursive = 1;
    ai++;
  }
  if (ai + 2 > argc) {
    print_usage("cp", "[-r] <src> <dst>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  {
    int rc = vfs_cp(argv[ai], argv[ai + 1], recursive);
    if (rc != 1) {
      if (!command_targets_writable_workspace(argv[ai + 1])) {
        print_protected_location();
      } else {
        console_writeln_colored("cp failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
      }
      return;
    }
  }
  console_writeln_colored("copied", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void cmd_mv(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage("mv", "<src> <dst>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  if (vfs_mv(argv[1], argv[2]) != 1) {
    if (!command_targets_writable_workspace(argv[2])) {
      print_protected_location();
    } else {
      console_writeln_colored("mv failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    return;
  }
  console_writeln_colored("moved", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static int str_contains_sub(const char *hay, const char *needle) {
  size_t i;
  size_t j;
  size_t nl;

  if (!needle || !needle[0]) {
    return 1;
  }
  nl = str_len(needle);
  for (i = 0; hay[i]; i++) {
    for (j = 0; j < nl && hay[i + j]; j++) {
      if (hay[i + j] != needle[j]) {
        break;
      }
    }
    if (j == nl) {
      return 1;
    }
  }
  return 0;
}

static void cmd_find(int argc, char *argv[]) {
  fat32_entry_t entries[64];
  int count;
  int i;

  if (argc < 2) {
    print_usage("find", "<substring>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  count = vfs_ls(entries, (int)(sizeof(entries) / sizeof(entries[0])));
  if (count < 0) {
    console_writeln_colored("find failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  for (i = 0; i < count; i++) {
    if (str_contains_sub(entries[i].name, argv[1])) {
      console_writeln(entries[i].name);
    }
  }
}

static void cmd_grep(int argc, char *argv[]) {
  uint8_t buf[4096];
  int n;
  char line[256];
  int li;
  int i;

  if (argc < 3) {
    print_usage("grep", "<text> <file>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  n = vfs_cat(argv[2], buf, (int)sizeof(buf) - 1);
  if (n < 0) {
    console_writeln_colored("grep: cannot read file.", VGA_COLOR_LIGHT_RED,
                            VGA_COLOR_BLACK);
    return;
  }
  buf[n] = '\0';

  li = 0;
  for (i = 0; i <= n; i++) {
    char c = (char)buf[i];
    if (c != '\n' && c != '\0') {
      if (li + 1 < (int)sizeof(line)) {
        line[li++] = c;
      }
      continue;
    }
    line[li] = '\0';
    li = 0;
    if (line[0] && str_contains_sub(line, argv[1])) {
      console_writeln(line);
    }
  }
}

static void cmd_history(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  input_shell_history_print();
}

static void cmd_ln(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln_colored("ln is not supported on FAT32 volumes.",
                        VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
}

static void cmd_inspect(int argc, char *argv[]) {
  fat32_info_t info;
  fat32_usage_t usage;

  (void)argc;
  (void)argv;

  if (!fat32_get_info(&info)) {
    console_writeln_colored("inspect: FAT unavailable.", VGA_COLOR_LIGHT_RED,
                            VGA_COLOR_BLACK);
    return;
  }

  console_writeln("FAT32 volume");
  console_write("partition LBA: ");
  print_uint32(info.partition_start_lba);
  console_putc('\n');
  console_write("bytes/sector: ");
  print_uint32(info.bytes_per_sector);
  console_putc('\n');
  console_write("sectors/cluster: ");
  print_uint32(info.sectors_per_cluster);
  console_putc('\n');

  if (fat32_get_usage(&usage)) {
    console_write("total bytes: ");
    print_uint64(usage.total_bytes);
    console_putc('\n');
    console_write("free bytes:  ");
    print_uint64(usage.free_bytes);
    console_putc('\n');
    console_write("used bytes:  ");
    print_uint64(usage.used_bytes);
    console_putc('\n');
    console_write("clusters (free/used): ");
    print_uint32(usage.free_clusters);
    console_write(" / ");
    print_uint32(usage.used_clusters);
    console_putc('\n');
  }
}

static void cmd_test(int argc, char *argv[]) {
  fat32_entry_t entries[24];
  int count;
  int i;
  char cwd_save[128];
  char path[160];
  char *run_argv[3];

  (void)argc;
  (void)argv;

  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  str_copy(cwd_save, vfs_cwd_path(), sizeof(cwd_save));
  if (!vfs_cd("/ROOT/tests")) {
    console_writeln_colored("No /ROOT/tests directory.", VGA_COLOR_YELLOW,
                            VGA_COLOR_BLACK);
    return;
  }

  count = vfs_ls(entries, (int)(sizeof(entries) / sizeof(entries[0])));
  if (count <= 0) {
    console_writeln_colored("No tests found.", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    (void)vfs_cd(cwd_save);
    return;
  }

  run_argv[0] = "run";
  for (i = 0; i < count; i++) {
    if (entries[i].is_dir) {
      continue;
    }
    path[0] = '\0';
    str_copy(path, "/ROOT/tests/", sizeof(path));
    str_cat(path, entries[i].name, sizeof(path));
    run_argv[1] = path;
    console_write("--- ");
    console_writeln(path);
    cmd_run(2, run_argv);
  }

  (void)vfs_cd(cwd_save);
}

static void cmd_compile(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln_colored(
      "compile: configure a remote builder URL to enable this command.",
      VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
}

static void cmd_logs(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  boot_log_dump();
}

static void cmd_mkdir(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage("mkdir", "<name>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  int rc = vfs_mkdir(argv[1]);
  if (rc < 0) {
    if (!command_targets_writable_workspace(argv[1])) {
      print_protected_location();
    } else {
      console_writeln_colored("mkdir failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    return;
  }
  if (rc == 0) {
    console_writeln_colored("directory already exists.", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    return;
  }
  console_writeln_colored("directory created", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void cmd_rmdir(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage("rmdir", "<name>");
    return;
  }
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  int rc = vfs_rmdir(argv[1]);
  if (rc < 0) {
    if (!command_targets_writable_workspace(argv[1])) {
      print_protected_location();
    } else {
      console_writeln_colored("rmdir failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
    return;
  }
  if (rc == 0) {
    console_writeln_colored("directory not found.", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    return;
  }
  console_writeln_colored("directory removed", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

static void cmd_edit(int argc, char *argv[]) {
  if (shell_is_raw_mode()) {
    console_writeln_colored("Editor is unavailable in raw shell mode.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }
  const char *name = (argc >= 2) ? argv[1] : "UNTITLED.TXT";
  editor_open(name);
  shell_restore_frame();
}

static void cmd_exit(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  if (shell_is_raw_mode()) {
    console_writeln("Raw shell mode. To go to program manager, restart (easier with 'rebootaswd')");
    return;
  }
  g_exit_requested = 1;
}

static void cmd_disktest(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (!disk_available()) {
    console_writeln_colored("Disk not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  uint8_t sector[512];
  if (disk_read_sectors(disk_partition_start(), 1, sector) != 0) {
    console_writeln_colored("Read failed.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  console_write("LBA ");
  print_uint32(disk_partition_start());
  console_write(" signature: ");
  if (sector[510] == 0x55 && sector[511] == 0xAA) {
    console_writeln("55AA OK");
  } else {
    console_writeln_colored("invalid", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
  }
}

static void cmd_diskinfo(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (!disk_available()) {
    console_writeln_colored("Disk not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  console_write("backend: ");
  console_writeln(disk_backend_name());

  console_write("partition lba: ");
  print_uint32(disk_partition_start());
  console_putc('\n');

  if (disk_backend() == DISK_BACKEND_BIOS_TRAMPOLINE) {
    console_write("boot drive: 0x");
    print_hex8(disk_bios_boot_drive());
    console_putc('\n');
  }

  console_write("last op: ");
  console_writeln(disk_op_name());

  console_write("last BIOS status: 0x");
  print_hex8(disk_last_bios_status());
  console_putc('\n');

  console_write("retry count: ");
  print_uint32(disk_last_retry_count());
  console_putc('\n');

  console_write("last error lba: ");
  print_uint32(disk_last_error_lba());
  console_putc('\n');
}

static void cmd_bpb(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  fat32_info_t info;
  if (!fat32_get_info(&info)) {
    console_writeln_colored("Filesystem metadata unavailable.", VGA_COLOR_LIGHT_RED,
                            VGA_COLOR_BLACK);
    return;
  }

  console_write("bytes/sector: ");
  print_uint32(info.bytes_per_sector);
  console_putc('\n');

  console_write("sectors/cluster: ");
  print_uint32(info.sectors_per_cluster);
  console_putc('\n');

  console_write("reserved sectors: ");
  print_uint32(info.reserved_sectors);
  console_putc('\n');

  console_write("fat count: ");
  print_uint32(info.fat_count);
  console_putc('\n');

  console_write("fat size sectors: ");
  print_uint32(info.fat_size_sectors);
  console_putc('\n');

  console_write("root cluster: ");
  print_uint32(info.root_cluster);
  console_putc('\n');

  console_write("partition start lba: ");
  print_uint32(info.partition_start_lba);
  console_putc('\n');

  console_write("fat start lba: ");
  print_uint32(info.fat_start_lba);
  console_putc('\n');

  console_write("data start lba: ");
  print_uint32(info.data_start_lba);
  console_putc('\n');
}

static void cmd_fattest(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  cmd_ls(0, 0);
}

static void cmd_vfstest(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  if (!vfs_available()) {
    console_writeln_colored("Filesystem not ready.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    return;
  }

  static const char *k_root_name = "RWTEST.TXT";
  static const char *k_root_data_a = "rwtest phase 1";
  static const char *k_root_data_b = "rwtest phase 2 overwrite";
  static const char *k_dir_name = "RWLAB";
  static const char *k_inner_name = "INNER.TXT";
  static const char *k_inner_data = "inner file data";
  uint8_t buf[128];
  fat32_entry_t entries[64];
  int n;

  if (!vfs_cd("/ROOT")) {
    vfstest_fail("cd /ROOT");
    return;
  }

  if (vfs_rm_force(k_root_name) < 0) {
    vfstest_fail("pre-clean rm RWTEST.TXT");
    return;
  }

  if (vfs_cd("/ROOT/RWLAB")) {
    if (vfs_rm_force(k_inner_name) < 0) {
      vfstest_fail("pre-clean rm /ROOT/RWLAB/INNER.TXT");
      return;
    }
    if (!vfs_cd("/ROOT")) {
      vfstest_fail("pre-clean cd /ROOT");
      return;
    }
  }

  {
    int rc = vfs_rmdir(k_dir_name);
    if (rc < 0) {
      vfstest_fail("pre-clean rmdir RWLAB");
      return;
    }
  }

  n = vfs_ls(entries, (int)(sizeof(entries) / sizeof(entries[0])));
  if (n < 0) {
    vfstest_fail("ls /");
    return;
  }

  {
    static const char *k_mv_dir = "MVLAB";
    static const char *k_mv_src = "SRC.TXT";
    static const char *k_mv_dst = "DST.TXT";
    static const char *k_mv_data = "mv blob";

    if (vfs_cd(k_mv_dir)) {
      (void)vfs_rm_force(k_mv_dst);
      (void)vfs_rm_force(k_mv_src);
      if (!vfs_cd("/ROOT")) {
        vfstest_fail("mv prep cd /ROOT");
        return;
      }
      (void)vfs_rmdir(k_mv_dir);
    }
    if (!vfs_cd("/ROOT")) {
      vfstest_fail("mv prep ensure /ROOT");
      return;
    }
    if (vfs_mkdir(k_mv_dir) != 1) {
      vfstest_fail("mkdir MVLAB");
      return;
    }
    if (!vfs_cd(k_mv_dir)) {
      vfstest_fail("cd MVLAB");
      return;
    }
    n = vfs_write(k_mv_src, (const uint8_t *)k_mv_data,
                  (uint32_t)str_len(k_mv_data));
    if (n != (int)str_len(k_mv_data)) {
      vfstest_fail("write SRC.TXT");
      return;
    }
    if (vfs_mv(k_mv_src, k_mv_dst) != 1) {
      vfstest_fail("mv SRC.TXT DST.TXT");
      return;
    }
    n = vfs_cat(k_mv_dst, buf, (int)sizeof(buf));
    if (!buffer_equals(buf, n, k_mv_data)) {
      vfstest_fail("read DST.TXT");
      return;
    }
    if (vfs_rm_force(k_mv_dst) != 1) {
      vfstest_fail("rm DST.TXT");
      return;
    }
    if (!vfs_cd("/ROOT")) {
      vfstest_fail("cd /ROOT after MVLAB");
      return;
    }
    if (vfs_rmdir(k_mv_dir) != 1) {
      vfstest_fail("rmdir MVLAB");
      return;
    }
  }

  n = vfs_write(k_root_name, (const uint8_t *)k_root_data_a,
                (uint32_t)str_len(k_root_data_a));
  if (n != (int)str_len(k_root_data_a)) {
    vfstest_fail("create RWTEST.TXT");
    return;
  }

  n = vfs_cat(k_root_name, buf, (int)sizeof(buf));
  if (!buffer_equals(buf, n, k_root_data_a)) {
    vfstest_fail("read RWTEST.TXT phase 1");
    return;
  }

  n = vfs_write(k_root_name, (const uint8_t *)k_root_data_b,
                (uint32_t)str_len(k_root_data_b));
  if (n != (int)str_len(k_root_data_b)) {
    vfstest_fail("overwrite RWTEST.TXT");
    return;
  }

  n = vfs_cat(k_root_name, buf, (int)sizeof(buf));
  if (!buffer_equals(buf, n, k_root_data_b)) {
    vfstest_fail("read RWTEST.TXT phase 2");
    return;
  }

  {
    int rc = vfs_mkdir(k_dir_name);
    if (rc != 1) {
      vfstest_fail("mkdir RWLAB");
      return;
    }
  }

  if (!vfs_cd("/ROOT/RWLAB")) {
    vfstest_fail("cd /ROOT/RWLAB");
    return;
  }

  n = vfs_write(k_inner_name, (const uint8_t *)k_inner_data,
                (uint32_t)str_len(k_inner_data));
  if (n != (int)str_len(k_inner_data)) {
    vfstest_fail("create INNER.TXT");
    return;
  }

  n = vfs_cat(k_inner_name, buf, (int)sizeof(buf));
  if (!buffer_equals(buf, n, k_inner_data)) {
    vfstest_fail("read INNER.TXT");
    return;
  }

  if (!vfs_cd("../RWLAB")) {
    vfstest_fail("cd ../RWLAB");
    return;
  }

  if (vfs_rm_force(k_inner_name) != 1) {
    vfstest_fail("rm INNER.TXT");
    return;
  }

  if (!vfs_cd("/ROOT")) {
    vfstest_fail("cd /ROOT after inner cleanup");
    return;
  }

  if (vfs_rmdir(k_dir_name) != 1) {
    vfstest_fail("rmdir RWLAB");
    return;
  }

  if (vfs_rm_force(k_root_name) != 1) {
    vfstest_fail("rm RWTEST.TXT");
    return;
  }

  console_writeln_colored("vfstest passed", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
}

/* ---- CMOS RTC read ---- */
static void read_cmos_datetime(int *h, int *m, int *s, int *day, int *mon, int *yr) {
    uint8_t hv, mv, sv, dv, mthv, yrv;
    outb(0x70, 0x04); hv  = inb(0x71);
    outb(0x70, 0x02); mv  = inb(0x71);
    outb(0x70, 0x00); sv  = inb(0x71);
    outb(0x70, 0x07); dv  = inb(0x71);
    outb(0x70, 0x08); mthv = inb(0x71);
    outb(0x70, 0x09); yrv  = inb(0x71);
    *h   = (hv   & 0xF) + ((hv   >> 4) & 0xF) * 10;
    *m   = (mv   & 0xF) + ((mv   >> 4) & 0xF) * 10;
    *s   = (sv   & 0xF) + ((sv   >> 4) & 0xF) * 10;
    *day = (dv   & 0xF) + ((dv   >> 4) & 0xF) * 10;
    *mon = (mthv & 0xF) + ((mthv >> 4) & 0xF) * 10;
    *yr  = 2000 + (yrv & 0xF) + ((yrv >> 4) & 0xF) * 10;
}

static void print_2digit(int v) {
    char buf[3];
    buf[0] = (char)('0' + v / 10);
    buf[1] = (char)('0' + v % 10);
    buf[2] = '\0';
    console_write(buf);
}

static void cmd_date(int argc, char *argv[]) {
    (void)argc; (void)argv;
    int h, m, s, day, mon, yr;
    read_cmos_datetime(&h, &m, &s, &day, &mon, &yr);
    char year[8];
    u32_to_dec((uint32_t)yr, year, sizeof(year));
    console_write(year); console_write("-");
    print_2digit(mon);   console_write("-");
    print_2digit(day);   console_write(" ");
    print_2digit(h);     console_write(":");
    print_2digit(m);     console_write(":");
    print_2digit(s);     console_putc('\n');
}

/* ---- Network commands ---- */

static void print_ip(const uint8_t *ip) {
    char tmp[6];
    int i;
    for (i = 0; i < 4; i++) {
        u32_to_dec(ip[i], tmp, sizeof(tmp));
        console_write(tmp);
        if (i < 3) console_write(".");
    }
}

static int network_has_ip(const net_info_t *ni) {
    return ni && (ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3]);
}

static void print_network_unavailable(const net_info_t *ni) {
    if (ni && ni->wifi_detected) {
        console_writeln_colored("Wi-Fi adapter detected, but it is not online yet.",
                                VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    } else {
        console_writeln_colored("No network interface.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
}

static void cmd_ifconfig(int argc, char *argv[]) {
    (void)argc; (void)argv;
    const net_info_t *ni = net_get_info();
    if (!ni->link_up && !ni->wifi_detected) {
        console_writeln_colored("No network interface.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return;
    }
    console_write("Transport: "); console_writeln(net_transport_name(ni->active_transport));
    console_write("State:     "); console_writeln(net_connection_state_name(ni->connection_state));
    if (ni->link_up) {
        console_write("NIC:     "); console_writeln(ni->nic_name);
        console_write("MAC:     ");
        {
            int i;
            static const char hex[] = "0123456789ABCDEF";
            char mac[3] = {0, 0, 0};
            for (i = 0; i < 6; i++) {
                mac[0] = hex[(ni->mac[i] >> 4) & 0xF];
                mac[1] = hex[ni->mac[i] & 0xF];
                console_write(mac);
                if (i < 5) console_write(":");
            }
            console_putc('\n');
        }
    }
    if (network_has_ip(ni)) {
        console_write("IP:      "); print_ip(ni->ip);      console_putc('\n');
        console_write("Mask:    "); print_ip(ni->netmask);  console_putc('\n');
        console_write("Gateway: "); print_ip(ni->gateway);  console_putc('\n');
        console_write("DNS:     "); print_ip(ni->dns);      console_putc('\n');
    } else if (ni->link_up) {
        console_writeln_colored("IP: not configured (DHCP pending)", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    }
    if (ni->wifi_detected) {
        console_write("Wi-Fi:   "); console_writeln(ni->wifi_name[0] ? ni->wifi_name : "detected");
        console_write("Wi-Fi backend: ");
        console_writeln(ni->wifi_backend_ready ? "ready" : "pending");
        if (ni->wifi_ssid[0]) {
            console_write("SSID:    "); console_writeln(ni->wifi_ssid);
        }
    }
}

static void cmd_ping(int argc, char *argv[]) {
    if (argc < 2) { print_usage("ping", "<ip_address>"); return; }
    const net_info_t *ni = net_get_info();
    if (!ni->link_up) {
        print_network_unavailable(ni);
        return;
    }

    /* Parse IP */
    uint8_t ip[4];
    {
        const char *p = argv[1];
        int i = 0; uint32_t cur = 0;
        for (; *p && i < 4; p++) {
            if (*p == '.') { ip[i++] = (uint8_t)cur; cur = 0; }
            else if (*p >= '0' && *p <= '9') cur = cur * 10 + (uint8_t)(*p - '0');
        }
        ip[i] = (uint8_t)cur;
    }

    console_write("PING "); print_ip(ip); console_writeln("...");

    int i;
    for (i = 0; i < 4; i++) {
        uint32_t t0 = timer_get_ticks();
        int seq = icmp_ping_send(ip);
        uint32_t deadline = t0 + 200u;   /* 2 second timeout */
        while (!icmp_ping_reply(seq) && timer_get_ticks() < deadline) {
            net_poll();
            __asm__ volatile("sti; hlt");
        }
        uint32_t t1 = timer_get_ticks();
        if (icmp_ping_reply(seq)) {
            char ms[8];
            u32_to_dec((t1 - t0) * 10u, ms, sizeof(ms));
            console_write("Reply from ");
            print_ip(ip);
            console_write(": time=");
            console_write(ms);
            console_writeln("ms");
        } else {
            console_writeln_colored("Request timed out.", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        }
        /* Wait ~1s between pings */
        uint32_t wait_end = timer_get_ticks() + 100u;
        while (timer_get_ticks() < wait_end) {
            net_poll();
            __asm__ volatile("sti; hlt");
        }
    }
}

static void cmd_nslookup(int argc, char *argv[]) {
    if (argc < 2) { print_usage("nslookup", "<hostname>"); return; }
    const net_info_t *ni = net_get_info();
    if (!ni->link_up) {
        print_network_unavailable(ni);
        return;
    }
    uint8_t ip[4];
    console_write("Looking up "); console_writeln(argv[1]);
    if (dns_resolve(argv[1], ip)) {
        console_write("Address: "); print_ip(ip); console_putc('\n');
    } else {
        console_writeln_colored("Could not resolve hostname.", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    }
}

static void cmd_fetch(int argc, char *argv[]) {
    if (argc < 2) { print_usage("fetch", "<http://url>"); return; }
    const net_info_t *ni = net_get_info();
    if (!ni->link_up) {
        print_network_unavailable(ni);
        return;
    }
    console_write("Fetching: "); console_writeln(argv[1]);

    static char body[4096];
    int status = 0;
    int n = http_get(argv[1], body, sizeof(body) - 1, &status);
    if (n < 0) {
        console_write("Fetch failed: ");
        console_writeln_colored(http_error_string(http_last_error()),
                                VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        return;
    }
    char code[8]; u32_to_dec((uint32_t)status, code, sizeof(code));
    console_write("HTTP "); console_writeln(code);
    body[n] = '\0';
    console_writeln(body);
}

static void cmd_reboot(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln("Rebooting...");
  power_reboot();
}

static void cmd_rebootaswd(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  console_writeln("Rebooting to Aswd OS...");
  power_reboot();
}
