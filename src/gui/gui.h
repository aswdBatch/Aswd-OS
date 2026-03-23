#pragma once

#include <stdint.h>

#define GUI_MAX_WINDOWS 8
#define GUI_TITLE_MAX   32
#define TITLE_BAR_HEIGHT 22
#define TASKBAR_HEIGHT   28

typedef struct { int x, y, w, h; } gui_rect_t;

typedef enum {
    GUI_ICON_TERMINAL = 0,
    GUI_ICON_FILES,
    GUI_ICON_EDITOR,
    GUI_ICON_OSINFO,
    GUI_ICON_SETTINGS,
    GUI_ICON_TASKMGR,
    GUI_ICON_SNAKE,
    GUI_ICON_NOTES,
    GUI_ICON_STORE,
    GUI_ICON_CALC,
    GUI_ICON_BROWSER,
    GUI_ICON_AXDOCS,
    GUI_ICON_AXSTUDIO,
} gui_icon_kind_t;

typedef struct {
    const char *id;
    const char *label;
    const char *desktop_label;
    gui_icon_kind_t icon_kind;
    int show_on_desktop;
    int single_instance;
    int in_store;           /* 1 = available in App Store, not on desktop */
    int dev_only;           /* 1 = hidden from App Store unless admin */
    void (*launch)(void);
} gui_app_t;

typedef struct {
    int        active;
    char       title[GUI_TITLE_MAX];
    gui_rect_t frame;
    gui_rect_t content;
    int        focused;
    int        dragging;
    int        drag_off_x, drag_off_y;
    int        minimized;
    int        resizing;
    int        resize_start_mx, resize_start_my;
    int        resize_orig_w,   resize_orig_h;
    void      *state;
    int        icon_kind;   /* GUI_ICON_* or -1 for none */
    void (*on_paint)(int win_id);
    int  (*on_tick)(int win_id, uint32_t now);
    void (*on_key)(int win_id, char key);
    void (*on_mouse)(int win_id, int x, int y, uint8_t buttons);
    void (*on_close)(int win_id);
} gui_window_t;

void gui_init(void);
void gui_run(void);
int  gui_window_create(const char *title, int x, int y, int w, int h);
void gui_window_close(int id);
void gui_window_set_title(int id, const char *title);
void gui_window_focus(int id);
void gui_repaint(void);
gui_rect_t gui_desktop_bounds(void);
void gui_window_suggest_rect(int pref_w, int pref_h, gui_rect_t *out);

int gui_window_active(int id);
const char *gui_window_title(int id);
int gui_window_count(void);
int gui_window_focused(void);
int gui_window_id_at(int index);
gui_rect_t gui_window_content(int id);
gui_window_t *gui_get_window(int id);

int gui_app_count(void);
const gui_app_t *gui_app_at(int index);
void gui_launch_app(int index);

void     gui_set_desktop_color(uint32_t color);
uint32_t gui_get_desktop_color(void);
