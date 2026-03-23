#include "script/builtin_scripts.h"

static const char demo_aswd[] =
    "# Aswd OS Script Demo\n"
    "\n"
    "echo === Demo ===\n"
    "osinfo\n"
    "\n"
    "confirm Proceed?\n"
    "iflast ack\n"
    "sysinfo\n"
    "\n"
    "set msg=Hello from script\n"
    "echo $msg\n";

static const builtin_script_t g_scripts[] = {
    {"demo", demo_aswd},
    {"demo.aswd", demo_aswd},
};

const builtin_script_t *builtin_scripts_get(void) {
  return &g_scripts[0];
}

int builtin_scripts_count(void) {
  return (int)(sizeof(g_scripts) / sizeof(g_scripts[0]));
}

