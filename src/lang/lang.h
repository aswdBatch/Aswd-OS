#pragma once

/* Ax — simple interpreted language for AswdOS.
 * Run scripts with: ax <file.ax>  or  run <file.ax> */

void lang_run_file(const char *path);
void lang_run_str(const char *src, int len);
