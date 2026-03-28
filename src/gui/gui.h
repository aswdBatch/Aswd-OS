#pragma once

#include <stdint.h>

#define GUI_MAX_WINDOWS 8
#define GUI_TITLE_MAX   32

typedef struct {
    int title_bar_h;
    int taskbar_h;
    int resize_handle;
    int window_min_w;
    int window_min_h;
    int desktop_margin_x;
    int desktop_margin_y;
    int desktop_slot_w;
    int desktop_slot_h;
    int desktop_gap_x;
    int desktop_gap_y;
    int desktop_icon_size;
    int start_button_w;
    int start_menu_w;
    int start_header_h;
    int start_footer_h;
    int start_cell_w;
    int start_cell_h;
    int search_w;
    int search_h;
} gui_shell_metrics_t;

#define TITLE_BAR_HEIGHT (gui_shell_metrics()->title_bar_h)
#define TASKBAR_HEIGHT   (gui_shell_metrics()->taskbar_h)

typedef struct { int x, y, w, h; } gui_rect_t;

typedef enum {
    GUI_BG_THEME_MINT = 0,
    GUI_BG_THEME_GLASS,
    GUI_BG_THEME_STUDIO,
    GUI_BG_THEME_SUNSET,
    GUI_BG_THEME_OCEAN,
    GUI_BG_THEME_NEUTRAL,
    GUI_BG_THEME_COUNT
} gui_background_theme_t;

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
    GUI_ICON_WORK180,
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
    int        maximized;
    int        resizing;
    int        resize_start_mx, resize_start_my;
    int        resize_orig_w,   resize_orig_h;
    int        min_w, min_h;
    gui_rect_t restore_frame;
    int        close_cancelled;
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
void gui_window_set_min_size(int id, int min_w, int min_h);
void gui_repaint(void);
gui_rect_t gui_desktop_bounds(void);
void gui_window_suggest_rect(int pref_w, int pref_h, gui_rect_t *out);
const gui_shell_metrics_t *gui_shell_metrics(void);

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
void     gui_set_background_theme(gui_background_theme_t theme);
gui_background_theme_t gui_get_background_theme(void);
int      gui_background_theme_count(void);
const char *gui_background_theme_name(int index);
uint32_t gui_background_theme_preview_color(int index);
void     gui_draw_auth_backdrop(void);
