#pragma once

#include "boot/bootui.h"

/* Run the auth gate before the selected boot target is launched.
 *
 * Behaviour:
 *   - No valid admin on disk  → show fullscreen Setup screen (always,
 *     regardless of requested_target); on success continue.
 *   - Valid admin + BOOT_TARGET_NORMAL_GUI → show fullscreen Login screen;
 *     blocks until credentials are accepted.
 *   - Valid admin + TUI / Shell Only → returns immediately (legacy bypass).
 *
 * Prerequisites: gfx_init(), keyboard_init(), disk_init(), cpu_sti()
 * must all have been called before this function.
 * If graphics mode is unavailable or disk is unavailable, returns
 * immediately without prompting.
 */
void auth_gui_run(boot_target_t requested_target);
