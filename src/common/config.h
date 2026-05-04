#pragma once

#define ASWD_OS_NAME    "AswdOS"
#define ASWD_OS_VERSION "v0.9.1"

/**
 * Set to 1 (e.g. -DFAT_DEBUG_SERIAL=1 in CFLAGS) for noisy FAT flush/write tracing on serial.
 */
#ifndef FAT_DEBUG_SERIAL
#define FAT_DEBUG_SERIAL 0
#endif
#define ASWD_OS_BANNER  ASWD_OS_NAME " " ASWD_OS_VERSION
#define ASWD_OS_HELLO  "Hello :D"
