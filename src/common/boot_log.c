#include "common/boot_log.h"

#include "boot/multiboot.h"
#include "common/colors.h"
#include "console/console.h"
#include "lib/string.h"

#define BOOT_LOG_LINES 24
#define BOOT_LOG_COL   72

static char g_boot_log[BOOT_LOG_LINES][BOOT_LOG_COL];
static int  g_boot_log_count = 0;

void boot_log_line(const char *msg) {
    if (!msg) {
        return;
    }
    if (multiboot_boot_quiet()) {
        return;
    }
    if (g_boot_log_count < BOOT_LOG_LINES) {
        str_copy(g_boot_log[g_boot_log_count], msg, BOOT_LOG_COL);
        g_boot_log_count++;
    } else {
        int i;
        for (i = 1; i < BOOT_LOG_LINES; i++) {
            str_copy(g_boot_log[i - 1], g_boot_log[i], BOOT_LOG_COL);
        }
        str_copy(g_boot_log[BOOT_LOG_LINES - 1], msg, BOOT_LOG_COL);
    }
}

void boot_log_dump(void) {
    int i;
    for (i = 0; i < g_boot_log_count; i++) {
        console_writeln_colored(g_boot_log[i], VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
}
