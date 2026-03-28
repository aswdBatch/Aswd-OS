#include "gui/files_gui.h"

#include <stdint.h>

#include "common/colors.h"
#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "fs/vfs.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "gui/work_gui.h"
#include "gui/winconsole.h"
#include "lib/string.h"

#define FILES_MAX_ENTRIES   64
#define FILES_HISTORY_MAX   12

#define SIDEBAR_W           (th_metrics()->sidebar_w)
#define TOOLBAR_H           (th_metrics()->toolbar_h)
#define ADDRESS_H           (th_metrics()->field_h + 8)
#define HEADER_H            (th_metrics()->list_row_h)
#define STATUS_H            (th_metrics()->status_h)
#define PANEL_PAD           (th_metrics()->card_pad)
#define BUTTON_W            62
#define BUTTON_H            (th_metrics()->button_h)
#define ROW_H               (th_metrics()->list_row_h)
#define DBLCLICK_TICKS      40u

#define COL_WIN_BG          gfx_rgb(246, 248, 252)
#define COL_SIDEBAR         gfx_rgb(236, 241, 247)
#define COL_MAIN_BG         gfx_rgb(255, 255, 255)
#define COL_TOOLBAR         gfx_rgb(242, 246, 252)
#define COL_ADDRESS         gfx_rgb(248, 250, 254)
#define COL_HEADER          gfx_rgb(232, 238, 247)
#define COL_STATUS          gfx_rgb(236, 241, 247)
#define COL_BORDER          gfx_rgb(188, 200, 218)
#define COL_TEXT            gfx_rgb(32, 44, 63)
#define COL_TEXT_DIM        gfx_rgb(99, 112, 132)
#define COL_ACCENT          gfx_rgb(38, 99, 235)
#define COL_ACCENT_SOFT     gfx_rgb(220, 232, 252)
#define COL_ROW_HOVER       gfx_rgb(243, 247, 253)
#define COL_ROW_SEL         gfx_rgb(219, 232, 252)
#define COL_BTN             gfx_rgb(252, 253, 255)
#define COL_BTN_DISABLED    gfx_rgb(238, 242, 248)
#define COL_WARN            gfx_rgb(180, 52, 72)
#define COL_OK              gfx_rgb(33, 128, 80)
#define COL_FOLDER          gfx_rgb(232, 173, 44)
#define COL_FILE            gfx_rgb(102, 147, 214)

enum {
    FILES_NAV_ROOT = 0,
    FILES_NAV_SYSTEM = 1,
    FILES_NAV_USERS = 2,
    FILES_NAV_COUNT = 3,
};

enum {
    FILES_BTN_BACK = 0,
    FILES_BTN_UP = 1,
    FILES_BTN_NEW = 2,
    FILES_BTN_REFRESH = 3,
    FILES_BTN_DELETE = 4,
    FILES_BTN_COUNT = 5,
};

static int g_win_id = -1;
static fat32_entry_t g_entries[FILES_MAX_ENTRIES];
static int g_entry_count = 0;
static int g_selected = 0;
static int g_scroll = 0;
static int g_newdir_seq = 1;
static char g_path[256];
static char g_notice[96];
static char g_back_history[FILES_HISTORY_MAX][256];
static int  g_back_count = 0;

static int      g_last_click_row = -1;
static uint32_t g_last_click_ticks = 0;

static int g_view_win_id = -1;
static winconsole_t *g_view_wc = 0;
static uint8_t g_view_buf[3840];

static void files_set_notice(const char *msg) {
    str_copy(g_notice, msg ? msg : "", sizeof(g_notice));
}

static int files_path_is_writable(const char *path) {
    if (!path) return 0;
    if (str_eq(path, "/ROOT")) return 1;
    return str_ncmp(path, "/ROOT", 5) == 0 && path[5] == '/';
}

static void files_join_path(const char *base, const char *name,
                            char *out, uint32_t out_size) {
    out[0] = '\0';
    if (str_eq(base, "/")) {
        str_copy(out, "/", out_size);
        str_cat(out, name, out_size);
        return;
    }
    str_copy(out, base, out_size);
    str_cat(out, "/", out_size);
    str_cat(out, name, out_size);
}

static int files_copy_segment(char *dst, int dst_size, const char *start, int len) {
    if (len <= 0 || len >= dst_size) {
        return 0;
    }

    for (int i = 0; i < len; i++) {
        dst[i] = start[i];
    }
    dst[len] = '\0';
    return 1;
}

static char files_ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - 'a' + 'A');
    }
    return c;
}

static int files_name_has_ext(const char *name, const char *ext) {
    int name_len = (int)str_len(name);
    int ext_len = (int)str_len(ext);

    if (name_len < ext_len) {
        return 0;
    }
    for (int i = 0; i < ext_len; i++) {
        if (files_ascii_upper(name[name_len - ext_len + i]) != files_ascii_upper(ext[i])) {
            return 0;
        }
    }
    return 1;
}

static int files_name_is_editable(const char *name) {
    return files_name_has_ext(name, ".TXT") ||
           files_name_has_ext(name, ".MD") ||
           files_name_has_ext(name, ".CFG") ||
           files_name_has_ext(name, ".INI") ||
           files_name_has_ext(name, ".LOG");
}

static void files_basename(const char *path, char *out, uint32_t out_size) {
    int len;
    int start;

    if (!path || !path[0]) {
        str_copy(out, "Files", out_size);
        return;
    }

    if (str_eq(path, "/")) {
        str_copy(out, "System", out_size);
        return;
    }

    len = (int)str_len(path);
    start = len - 1;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    str_copy(out, path + start, out_size);
}

static void files_update_title(void) {
    char base[32];
    char title[48];

    if (g_win_id < 0 || !gui_window_active(g_win_id)) {
        return;
    }

    files_basename(g_path, base, sizeof(base));
    title[0] = '\0';
    str_copy(title, "Files - ", sizeof(title));
    str_cat(title, base, sizeof(title));
    gui_window_set_title(g_win_id, title);
}

static void files_push_history(const char *path) {
    if (!path || !path[0]) {
        return;
    }
    if (g_back_count > 0 && str_eq(g_back_history[g_back_count - 1], path)) {
        return;
    }

    if (g_back_count >= FILES_HISTORY_MAX) {
        for (int i = 1; i < FILES_HISTORY_MAX; i++) {
            str_copy(g_back_history[i - 1], g_back_history[i],
                     sizeof(g_back_history[i - 1]));
        }
        g_back_count = FILES_HISTORY_MAX - 1;
    }

    str_copy(g_back_history[g_back_count++], path, sizeof(g_back_history[0]));
}

static int files_with_path_begin(char *saved_cwd, uint32_t saved_size) {
    if (!vfs_available()) {
        return 0;
    }
    str_copy(saved_cwd, vfs_cwd_path(), saved_size);
    return vfs_cd(g_path);
}

static void files_with_path_end(const char *saved_cwd) {
    if (saved_cwd && saved_cwd[0]) {
        (void)vfs_cd(saved_cwd);
    }
}

static int files_visible_rows(void) {
    gui_rect_t r;
    int rows;

    if (g_win_id < 0 || !gui_window_active(g_win_id)) {
        return 8;
    }

    r = gui_window_content(g_win_id);
    rows = (r.h - TOOLBAR_H - ADDRESS_H - HEADER_H - STATUS_H - PANEL_PAD * 2) / ROW_H;
    if (rows < 1) rows = 1;
    return rows;
}

static void files_clamp_scroll(void) {
    int rows = files_visible_rows();
    int max_scroll;

    if (g_entry_count <= 0) {
        g_selected = 0;
        g_scroll = 0;
        return;
    }

    if (g_selected < 0) g_selected = 0;
    if (g_selected >= g_entry_count) g_selected = g_entry_count - 1;

    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + rows) g_scroll = g_selected - rows + 1;

    max_scroll = g_entry_count - rows;
    if (max_scroll < 0) max_scroll = 0;
    if (g_scroll < 0) g_scroll = 0;
    if (g_scroll > max_scroll) g_scroll = max_scroll;
}

static int files_compare_entries(const fat32_entry_t *a, const fat32_entry_t *b) {
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    return str_cmp(a->name, b->name);
}

static void files_sort_entries(void) {
    for (int i = 0; i < g_entry_count; i++) {
        for (int j = i + 1; j < g_entry_count; j++) {
            if (files_compare_entries(&g_entries[i], &g_entries[j]) > 0) {
                fat32_entry_t tmp = g_entries[i];
                g_entries[i] = g_entries[j];
                g_entries[j] = tmp;
            }
        }
    }
}

static int files_jump_to(const char *path, int push_history) {
    char saved_cwd[256];
    char old_path[256];

    if (!vfs_available()) {
        files_set_notice("Filesystem unavailable.");
        return 0;
    }

    str_copy(old_path, g_path, sizeof(old_path));
    str_copy(saved_cwd, vfs_cwd_path(), sizeof(saved_cwd));

    if (!vfs_cd(path)) {
        files_set_notice("Cannot open folder.");
        return 0;
    }

    if (push_history && old_path[0] && !str_eq(old_path, vfs_cwd_path())) {
        files_push_history(old_path);
    }

    str_copy(g_path, vfs_cwd_path(), sizeof(g_path));
    (void)vfs_cd(saved_cwd);
    files_update_title();
    return 1;
}

static void files_refresh(void) {
    char saved_cwd[256];

    if (!vfs_available()) {
        g_entry_count = 0;
        g_selected = 0;
        g_scroll = 0;
        files_set_notice("Filesystem unavailable.");
        return;
    }

    if (!files_with_path_begin(saved_cwd, sizeof(saved_cwd))) {
        g_entry_count = 0;
        files_set_notice("Folder unavailable.");
        return;
    }

    g_entry_count = vfs_ls(g_entries, FILES_MAX_ENTRIES);
    if (g_entry_count < 0) {
        g_entry_count = 0;
        files_set_notice("Refresh failed.");
    } else {
        files_sort_entries();
    }

    str_copy(g_path, vfs_cwd_path(), sizeof(g_path));
    files_with_path_end(saved_cwd);
    files_update_title();
    files_clamp_scroll();
}

static void files_go_back(void) {
    if (g_back_count <= 0) {
        files_set_notice("No previous location.");
        return;
    }

    g_back_count--;
    if (files_jump_to(g_back_history[g_back_count], 0)) {
        files_set_notice("");
        files_refresh();
    }
}

static void files_go_up(void) {
    char saved_cwd[256];
    char parent[256];

    if (str_eq(g_path, "/")) {
        files_set_notice("Already at system root.");
        return;
    }

    str_copy(saved_cwd, vfs_cwd_path(), sizeof(saved_cwd));
    if (!vfs_cd(g_path) || !vfs_cd("..")) {
        (void)vfs_cd(saved_cwd);
        files_set_notice("Cannot go up.");
        return;
    }

    str_copy(parent, vfs_cwd_path(), sizeof(parent));
    (void)vfs_cd(saved_cwd);

    if (files_jump_to(parent, 1)) {
        files_set_notice("");
        files_refresh();
    }
}

static void files_make_new_dir(void) {
    char saved_cwd[256];
    char name[13];
    char num[8];

    if (!files_path_is_writable(g_path)) {
        files_set_notice("System folder: read-only.");
        return;
    }
    if (!files_with_path_begin(saved_cwd, sizeof(saved_cwd))) {
        files_set_notice("Folder unavailable.");
        return;
    }

    str_copy(name, "NEWDIR", sizeof(name));
    u32_to_dec((uint32_t)g_newdir_seq++, num, sizeof(num));
    str_cat(name, num, sizeof(name));

    if (vfs_mkdir(name) > 0) {
        files_set_notice("Folder created.");
    } else {
        files_set_notice("Create folder failed.");
    }

    files_with_path_end(saved_cwd);
    files_refresh();
}

static void view_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);

    if (g_view_wc) {
        gfx_fill_rect(r.x, r.y, r.w, r.h, gfx_rgb(0, 0, 0));
        wc_resize(g_view_wc, r.w, r.h);
        wc_paint(g_view_wc, r.x, r.y);
    }
}

static void view_on_key(int win_id, char key) {
    if (key == 0x11) gui_window_close(win_id);
}

static void view_on_close(int win_id) {
    (void)win_id;
    if (g_view_wc) {
        wc_free(g_view_wc);
        g_view_wc = 0;
    }
    g_view_win_id = -1;
}

static void files_open_viewer(const char *name) {
    gui_rect_t rect;
    gui_window_t *w;
    char saved_cwd[256];
    char title[40];
    char size_buf[16];
    int read;
    int is_binary = 0;

    if (!files_with_path_begin(saved_cwd, sizeof(saved_cwd))) {
        files_set_notice("Folder unavailable.");
        return;
    }

    read = vfs_cat(name, g_view_buf, (int)sizeof(g_view_buf));
    files_with_path_end(saved_cwd);
    if (read < 0) {
        files_set_notice("Cannot read file.");
        return;
    }

    if (g_view_win_id >= 0 && gui_window_active(g_view_win_id)) {
        if (g_view_wc) wc_clear(g_view_wc);
        gui_window_focus(g_view_win_id);
    } else {
        if (g_view_wc) {
            wc_free(g_view_wc);
            g_view_wc = 0;
        }

        gui_window_suggest_rect(560, 380, &rect);
        title[0] = '\0';
        str_copy(title, "View: ", sizeof(title));
        str_cat(title, name, sizeof(title));
        g_view_win_id = gui_window_create(title, rect.x, rect.y, rect.w, rect.h);
        if (g_view_win_id < 0) return;
        gui_window_set_min_size(g_view_win_id, 420, 280);

        g_view_wc = wc_alloc();
        if (!g_view_wc) {
            gui_window_close(g_view_win_id);
            g_view_win_id = -1;
            return;
        }

        wc_init(g_view_wc, g_view_win_id);
        w = gui_get_window(g_view_win_id);
        w->on_paint = view_on_paint;
        w->on_key = view_on_key;
        w->on_close = view_on_close;
    }

    for (int i = 0; i < read; i++) {
        unsigned char b = (unsigned char)g_view_buf[i];
        if (b < 9 && b != '\t' && b != '\n' && b != '\r') {
            is_binary = 1;
            break;
        }
    }

    wc_set_color(g_view_wc, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    if (is_binary) {
        wc_write(g_view_wc, "Binary file (");
        u32_to_dec((uint32_t)read, size_buf, sizeof(size_buf));
        wc_write(g_view_wc, size_buf);
        wc_write(g_view_wc, " bytes).\n");
    } else {
        for (int i = 0; i < read; i++) {
            char c = (char)g_view_buf[i];
            if (c == '\r') continue;
            wc_putc(g_view_wc, c);
        }
    }
}

static void files_open_selected(void) {
    char next_path[256];

    if (g_selected < 0 || g_selected >= g_entry_count) {
        return;
    }

    if (g_entries[g_selected].is_dir) {
        files_join_path(g_path, g_entries[g_selected].name, next_path, sizeof(next_path));
        if (files_jump_to(next_path, 1)) {
            files_set_notice("");
            files_refresh();
        }
        return;
    }

    files_join_path(g_path, g_entries[g_selected].name, next_path, sizeof(next_path));
    if (files_name_is_editable(g_entries[g_selected].name)) {
        work_gui_open(WORK_MODE_DOCS, next_path);
        return;
    }

    files_open_viewer(g_entries[g_selected].name);
}

static void files_delete_selected(void) {
    char saved_cwd[256];
    int rc;

    if (g_selected < 0 || g_selected >= g_entry_count) {
        return;
    }
    if (!files_path_is_writable(g_path)) {
        files_set_notice("System folder: read-only.");
        return;
    }
    if (!files_with_path_begin(saved_cwd, sizeof(saved_cwd))) {
        files_set_notice("Folder unavailable.");
        return;
    }

    if (g_entries[g_selected].is_dir) {
        rc = vfs_rmdir(g_entries[g_selected].name);
    } else {
        rc = vfs_rm(g_entries[g_selected].name);
    }

    files_with_path_end(saved_cwd);

    if (rc > 0) {
        files_set_notice("Entry deleted.");
        files_refresh();
        return;
    }

    files_set_notice(g_entries[g_selected].is_dir
        ? "Delete failed (directory must be empty)."
        : "Delete failed.");
}

static void files_draw_button(int x, int y, int w, const char *label, int enabled) {
    uint32_t bg = enabled ? COL_BTN : COL_BTN_DISABLED;
    uint32_t fg = enabled ? COL_TEXT : COL_TEXT_DIM;
    if (enabled) {
        th_draw_button(x, y, w, BUTTON_H, label, 0);
    } else {
        gfx_fill_rect(x, y, w, BUTTON_H, COL_BORDER);
        gfx_fill_rect(x + 1, y + 1, w - 2, BUTTON_H - 2, bg);
        th_draw_text_center(x, y + (BUTTON_H - th_metrics()->font_body) / 2, w,
                            label, fg, bg, th_metrics()->font_body);
    }
}

static void files_button_rect(int button, int *x, int *y, int *w, int *h) {
    *x = SIDEBAR_W + PANEL_PAD + button * (BUTTON_W + 8);
    *y = 6;
    *w = BUTTON_W;
    *h = BUTTON_H;
}

static int files_button_enabled(int button) {
    switch (button) {
        case FILES_BTN_BACK: return g_back_count > 0;
        case FILES_BTN_UP: return !str_eq(g_path, "/");
        case FILES_BTN_NEW: return files_path_is_writable(g_path);
        case FILES_BTN_REFRESH: return 1;
        case FILES_BTN_DELETE:
            return files_path_is_writable(g_path) && g_selected >= 0 && g_selected < g_entry_count;
    }
    return 0;
}

static const char *files_nav_label(int item) {
    if (item == FILES_NAV_ROOT) return "ROOT";
    if (item == FILES_NAV_SYSTEM) return "System";
    return "USERS";
}

static const char *files_nav_path(int item) {
    if (item == FILES_NAV_ROOT) return "/ROOT";
    if (item == FILES_NAV_SYSTEM) return "/";
    return "/USERS";
}

static void files_nav_rect(int item, int *x, int *y, int *w, int *h) {
    *x = 10;
    *y = 44 + item * 34;
    *w = SIDEBAR_W - 20;
    *h = 26;
}

static int files_nav_selected(int item) {
    if (item == FILES_NAV_ROOT) {
        return files_path_is_writable(g_path);
    }
    if (item == FILES_NAV_USERS) {
        if (str_eq(g_path, "/USERS")) return 1;
        return str_ncmp(g_path, "/USERS", 6) == 0 && g_path[6] == '/';
    }
    return str_eq(g_path, "/");
}

static void files_format_breadcrumb(char *out, uint32_t out_size) {
    int len;
    int start = 1;

    out[0] = '\0';
    str_copy(out, "This PC", out_size);

    if (str_eq(g_path, "/")) {
        str_cat(out, " > System", out_size);
        return;
    }

    len = (int)str_len(g_path);
    while (start < len) {
        int end = start;
        char seg[16];

        while (end < len && g_path[end] != '/') {
            end++;
        }
        if (files_copy_segment(seg, sizeof(seg), g_path + start, end - start)) {
            str_cat(out, " > ", out_size);
            str_cat(out, seg, out_size);
        }
        start = end + 1;
    }
}

static const char *files_entry_type(const fat32_entry_t *entry) {
    return entry->is_dir ? "Folder" : "File";
}

static void files_entry_size(const fat32_entry_t *entry, char *out, uint32_t out_size) {
    if (entry->is_dir) {
        str_copy(out, "-", out_size);
        return;
    }
    u32_to_dec(entry->size, out, out_size);
    str_cat(out, " B", out_size);
}

static int files_row_index_from_y(int y) {
    int list_top = TOOLBAR_H + ADDRESS_H + HEADER_H + PANEL_PAD;
    int row = (y - list_top) / ROW_H;

    if (y < list_top) return -1;
    if (row < 0) return -1;
    if (g_scroll + row >= g_entry_count) return -1;
    return g_scroll + row;
}

static void files_on_paint(int win_id) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(win_id);
    int main_x = r.x + SIDEBAR_W;
    int main_w = r.w - SIDEBAR_W;
    int name_x = main_x + PANEL_PAD + 34;
    int type_x = r.x + r.w - 180;
    int size_x = r.x + r.w - 86;
    int list_y = r.y + TOOLBAR_H + ADDRESS_H + HEADER_H + PANEL_PAD;
    int rows = files_visible_rows();
    char breadcrumb[128];
    char status[160];

    files_clamp_scroll();

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_WIN_BG);
    th_draw_sidebar(r.x, r.y, SIDEBAR_W, r.h, "Folders");
    gfx_fill_rect(main_x, r.y, main_w, r.h, COL_MAIN_BG);
    th_draw_toolbar(main_x, r.y, main_w, "Files");
    gfx_fill_rect(main_x, r.y + TOOLBAR_H, main_w, ADDRESS_H, COL_ADDRESS);
    th_draw_table_header(main_x, r.y + TOOLBAR_H + ADDRESS_H, main_w, HEADER_H);

    gfx_fill_rect(r.x + SIDEBAR_W - 1, r.y, 1, r.h, COL_BORDER);
    gfx_fill_rect(main_x, r.y + TOOLBAR_H - 1, main_w, 1, COL_BORDER);
    gfx_fill_rect(main_x, r.y + TOOLBAR_H + ADDRESS_H - 1, main_w, 1, COL_BORDER);
    gfx_fill_rect(main_x, r.y + TOOLBAR_H + ADDRESS_H + HEADER_H - 1, main_w, 1, COL_BORDER);
    gfx_fill_rect(r.x, r.y + r.h - STATUS_H, r.w, 1, COL_BORDER);

    for (int item = 0; item < FILES_NAV_COUNT; item++) {
        int sx;
        int sy;
        int sw;
        int sh;
        uint32_t bg;
        uint32_t fg;

        files_nav_rect(item, &sx, &sy, &sw, &sh);
        sx += r.x;
        sy += r.y;
        bg = files_nav_selected(item) ? COL_ACCENT_SOFT : COL_SIDEBAR;
        fg = files_nav_selected(item) ? COL_ACCENT : COL_TEXT;

        gfx_fill_rect(sx, sy, sw, sh, bg);
        gfx_fill_rect(sx + 8, sy + 7, 10, 8,
                      item == FILES_NAV_SYSTEM ? COL_FILE : COL_FOLDER);
        th_draw_text(sx + 24, sy + (sh - tm->font_body) / 2,
                     files_nav_label(item), fg, bg, tm->font_body);
    }

    for (int button = 0; button < FILES_BTN_COUNT; button++) {
        int bx;
        int by;
        int bw;
        int bh;
        const char *label = "Back";

        if (button == FILES_BTN_UP) label = "Up";
        else if (button == FILES_BTN_NEW) label = "New";
        else if (button == FILES_BTN_REFRESH) label = "Refresh";
        else if (button == FILES_BTN_DELETE) label = "Delete";

        files_button_rect(button, &bx, &by, &bw, &bh);
        files_draw_button(r.x + bx, r.y + by, bw, label, files_button_enabled(button));
    }

    files_format_breadcrumb(breadcrumb, sizeof(breadcrumb));
    gfx_fill_rect(main_x + PANEL_PAD, r.y + TOOLBAR_H + 5, main_w - PANEL_PAD * 2,
                  ADDRESS_H - 10, COL_BORDER);
    gfx_fill_rect(main_x + PANEL_PAD + 1, r.y + TOOLBAR_H + 6,
                  main_w - PANEL_PAD * 2 - 2, ADDRESS_H - 12, COL_MAIN_BG);
    th_draw_text(main_x + PANEL_PAD + 10, r.y + TOOLBAR_H + 9,
                 breadcrumb, COL_TEXT, COL_MAIN_BG, tm->font_body);

    th_draw_text(name_x, r.y + TOOLBAR_H + ADDRESS_H + 5, "Name", COL_TEXT_DIM, COL_HEADER, tm->font_body);
    th_draw_text(type_x, r.y + TOOLBAR_H + ADDRESS_H + 5, "Type", COL_TEXT_DIM, COL_HEADER, tm->font_body);
    th_draw_text(size_x, r.y + TOOLBAR_H + ADDRESS_H + 5, "Size", COL_TEXT_DIM, COL_HEADER, tm->font_body);

    for (int i = 0; i < rows && g_scroll + i < g_entry_count; i++) {
        int idx = g_scroll + i;
        int row_y = list_y + i * ROW_H;
        fat32_entry_t *entry = &g_entries[idx];
        uint32_t bg = (idx == g_selected) ? COL_ROW_SEL : COL_MAIN_BG;
        char size_buf[16];

        if (idx != g_selected) {
            bg = (i & 1) ? COL_ROW_HOVER : COL_MAIN_BG;
        }

        th_draw_list_row(main_x + PANEL_PAD, row_y, main_w - PANEL_PAD * 2, ROW_H, "", idx == g_selected);
        gfx_fill_rect(main_x + PANEL_PAD + 1, row_y + 1, main_w - PANEL_PAD * 2 - 2, ROW_H - 2, bg);
        gfx_fill_rect(name_x - 18, row_y + 6, 12, 10,
                      entry->is_dir ? COL_FOLDER : COL_FILE);
        th_draw_text(name_x, row_y + 4, entry->name, COL_TEXT, bg, tm->font_body);
        th_draw_text(type_x, row_y + 4, files_entry_type(entry), COL_TEXT_DIM, bg, tm->font_body);
        files_entry_size(entry, size_buf, sizeof(size_buf));
        th_draw_text(size_x, row_y + 4, size_buf, COL_TEXT_DIM, bg, tm->font_body);
    }

    status[0] = '\0';
    if (files_path_is_writable(g_path)) {
        str_copy(status, "Writable workspace", sizeof(status));
    } else {
        str_copy(status, "System folder: read-only", sizeof(status));
    }
    str_cat(status, " | ", sizeof(status));
    {
        char count_buf[16];
        u32_to_dec((uint32_t)g_entry_count, count_buf, sizeof(count_buf));
        str_cat(status, count_buf, sizeof(status));
        str_cat(status, " items", sizeof(status));
    }
    if (g_notice[0]) {
        str_cat(status, " | ", sizeof(status));
        str_cat(status, g_notice, sizeof(status));
    }

    th_draw_statusbar(r.x, r.y + r.h - STATUS_H, r.w, STATUS_H, "");
    th_draw_text(r.x + 10, r.y + r.h - STATUS_H + (STATUS_H - tm->font_small) / 2,
                 status,
                 files_path_is_writable(g_path) ? COL_OK : COL_WARN,
                 TH_BG_STATUS,
                 tm->font_small);
}

static void files_on_key(int win_id, char key) {
    (void)win_id;

    if (key == 0x11) {
        gui_window_close(g_win_id);
        return;
    }
    if (key == KEY_UP && g_selected > 0) {
        g_selected--;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_DOWN && g_selected + 1 < g_entry_count) {
        g_selected++;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_PAGEUP) {
        g_selected -= files_visible_rows();
        if (g_selected < 0) g_selected = 0;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_PAGEDOWN) {
        g_selected += files_visible_rows();
        if (g_selected >= g_entry_count) g_selected = g_entry_count - 1;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_HOME) {
        g_selected = 0;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_END && g_entry_count > 0) {
        g_selected = g_entry_count - 1;
        files_clamp_scroll();
        return;
    }
    if (key == KEY_LEFT || key == 'b' || key == 'B') {
        files_go_back();
        return;
    }
    if (key == '\b' || key == 'u' || key == 'U') {
        files_go_up();
        return;
    }
    if (key == KEY_DELETE) {
        files_delete_selected();
        return;
    }
    if (key == '\r' || key == '\n' || key == KEY_RIGHT) {
        files_open_selected();
        return;
    }
    if (key == 'r' || key == 'R') {
        files_set_notice("");
        files_refresh();
        return;
    }
    if (key == 'n' || key == 'N') {
        files_make_new_dir();
    }
}

static void files_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    int row;

    (void)win_id;
    if (!(buttons & 1)) return;

    for (int item = 0; item < FILES_NAV_COUNT; item++) {
        int sx;
        int sy;
        int sw;
        int sh;

        files_nav_rect(item, &sx, &sy, &sw, &sh);
        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
            if (files_jump_to(files_nav_path(item), 1)) {
                files_set_notice("");
                files_refresh();
            }
            return;
        }
    }

    for (int button = 0; button < FILES_BTN_COUNT; button++) {
        int bx;
        int by;
        int bw;
        int bh;

        files_button_rect(button, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            if (!files_button_enabled(button)) {
                return;
            }
            if (button == FILES_BTN_BACK) files_go_back();
            else if (button == FILES_BTN_UP) files_go_up();
            else if (button == FILES_BTN_NEW) files_make_new_dir();
            else if (button == FILES_BTN_REFRESH) { files_set_notice(""); files_refresh(); }
            else if (button == FILES_BTN_DELETE) files_delete_selected();
            return;
        }
    }

    row = files_row_index_from_y(y);
    if (row >= 0) {
        uint32_t now = timer_get_ticks();

        if (row == g_last_click_row &&
            (uint32_t)(now - g_last_click_ticks) <= DBLCLICK_TICKS) {
            g_selected = row;
            g_last_click_row = -1;
            files_open_selected();
        } else {
            g_selected = row;
            g_last_click_row = row;
            g_last_click_ticks = now;
            files_clamp_scroll();
        }
    }
}

static void files_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void files_gui_launch(void) {
    gui_window_t *w;
    gui_rect_t rect;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(760, 470, &rect);
    g_win_id = gui_window_create("Files", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;
    gui_window_set_min_size(g_win_id, 560, 340);

    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_FILES;
    w->on_paint = files_on_paint;
    w->on_key = files_on_key;
    w->on_mouse = files_on_mouse;
    w->on_close = files_on_close;

    g_entry_count = 0;
    g_selected = 0;
    g_scroll = 0;
    g_back_count = 0;
    g_last_click_row = -1;
    files_set_notice("Double-click open. Backspace goes up.");
    str_copy(g_path, "/ROOT", sizeof(g_path));

    if (!files_jump_to("/ROOT", 0)) {
        files_set_notice("Cannot open /ROOT.");
    }
    files_refresh();
}
