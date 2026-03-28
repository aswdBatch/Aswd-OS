#pragma once

/* Minimal test framework for AswdOS CI.
 * All output goes to the serial port (COM1).
 * On completion, the kernel exits QEMU via the ISA debug-exit port (0xf4).
 * QEMU exit code: 1 = all tests passed, 3 = one or more failures. */

/* Called by individual test suites to report a single assertion result.
 * Writes "[PASS] name" or "[FAIL] name" to serial. */
void test_assert(int condition, const char *name);

/* Runs all test suites, prints a summary, then exits QEMU.
 * Does not return. */
void tests_run_all(void);

/* Individual test suites — each defined in its own .c file. */
void tests_string(void);
void tests_shell(void);
void tests_fs(void);
void tests_ax(void);
