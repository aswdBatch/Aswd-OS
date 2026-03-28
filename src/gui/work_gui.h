#pragma once

typedef enum {
    WORK_MODE_HOME = 0,
    WORK_MODE_DOCS,
    WORK_MODE_SHEETS,
    WORK_MODE_SLIDES,
} work_mode_t;

void work_gui_launch(void);
void work_gui_open(work_mode_t mode, const char *path);
void work_run_tests(void);
