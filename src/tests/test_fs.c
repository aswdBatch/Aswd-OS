#include "tests/test_runner.h"

#include <stdint.h>

#include "fs/vfs.h"
#include "lib/string.h"

#define CI_FILE  "/ROOT/CI_TEST.TMP"
#define CI_DIR   "/ROOT/CI_DIR"
#define PAYLOAD  "aswd ci test payload"

void tests_fs(void) {
    uint8_t buf[64];
    int n;

    if (!vfs_available()) {
        /* Filesystem not present — skip FS tests rather than fail them */
        test_assert(1, "fs: vfs unavailable (skipped)");
        return;
    }

    /* Write a file */
    n = vfs_write(CI_FILE, (const uint8_t *)PAYLOAD, (uint32_t)str_len(PAYLOAD));
    test_assert(n >= 0, "fs: write file");

    /* Read it back */
    n = vfs_cat(CI_FILE, buf, (int)sizeof(buf) - 1);
    test_assert(n > 0, "fs: read file");
    if (n > 0) {
        buf[n] = '\0';
        test_assert(str_eq((const char *)buf, PAYLOAD), "fs: readback matches");
    } else {
        test_assert(0, "fs: readback matches");
    }

    /* Delete file */
    test_assert(vfs_rm(CI_FILE) >= 0, "fs: delete file");

    /* Verify it is gone */
    n = vfs_cat(CI_FILE, buf, (int)sizeof(buf) - 1);
    test_assert(n < 0, "fs: file gone after delete");

    /* Create and remove a directory */
    test_assert(vfs_mkdir(CI_DIR) >= 0,  "fs: mkdir");
    test_assert(vfs_rmdir(CI_DIR) >= 0,  "fs: rmdir");
}
