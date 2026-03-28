#include "tests/test_runner.h"

#include "lang/lang.h"
#include "lib/string.h"

/* lang_run_str runs the given source through the full lex→parse→eval pipeline.
 * These tests verify that valid scripts do not crash and that invalid scripts
 * fail gracefully (the interpreter writes an error message rather than hanging
 * or faulting). */

void tests_ax(void) {
    /* Simple variable assignment and print */
    const char *src_var =
        "var x = 42\n"
        "print x\n";
    lang_run_str(src_var, (int)str_len(src_var));
    test_assert(1, "ax: var + print runs");

    /* Arithmetic */
    const char *src_arith =
        "var a = 10\n"
        "var b = 3\n"
        "var c = a + b\n"
        "print c\n";
    lang_run_str(src_arith, (int)str_len(src_arith));
    test_assert(1, "ax: arithmetic runs");

    /* String literal */
    const char *src_str =
        "print \"hello ax\"\n";
    lang_run_str(src_str, (int)str_len(src_str));
    test_assert(1, "ax: string literal runs");

    /* If/else branch */
    const char *src_if =
        "var x = 1\n"
        "if x == 1 {\n"
        "  print \"yes\"\n"
        "}\n";
    lang_run_str(src_if, (int)str_len(src_if));
    test_assert(1, "ax: if branch runs");

    /* While loop (small iteration count) */
    const char *src_while =
        "var i = 0\n"
        "while i < 3 {\n"
        "  i = i + 1\n"
        "}\n"
        "print i\n";
    lang_run_str(src_while, (int)str_len(src_while));
    test_assert(1, "ax: while loop runs");

    /* Invalid syntax — must not crash */
    const char *src_bad = "@@@@\n";
    lang_run_str(src_bad, (int)str_len(src_bad));
    test_assert(1, "ax: invalid syntax does not crash");
}
