#include "tests/test_runner.h"

#include "shell/commands.h"

/* commands_dispatch returns 1 to signal "exit shell"; 0 for normal commands. */

void tests_shell(void) {
    char *help_argv[]   = { "help" };
    char *echo_argv[]   = { "echo", "hello", "world" };
    char *osinfo_argv[] = { "osinfo" };
    char *pwd_argv[]    = { "pwd" };

    test_assert(commands_dispatch(1, help_argv)   == 0, "shell: help runs");
    test_assert(commands_dispatch(3, echo_argv)   == 0, "shell: echo runs");
    test_assert(commands_dispatch(1, osinfo_argv) == 0, "shell: osinfo runs");
    test_assert(commands_dispatch(1, pwd_argv)    == 0, "shell: pwd runs");
}
