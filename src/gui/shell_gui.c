#include "gui/shell_gui.h"

#include "common/colors.h"
#include "console/console.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "gui/gui.h"
#include "gui/winconsole.h"
#include "lib/string.h"
#include "shell/commands.h"

static winconsole_t *g_wc;
static int g_win_id = -1;

#define SHELL_HISTORY_MAX 16

/* Line editing state */
static char g_line[256];
static int  g_line_len;
static int  g_line_pos;
static char g_history[SHELL_HISTORY_MAX][256];
static int  g_hist_count;
static int  g_hist_head;
static int  g_hist_pos;
static char g_saved_line[256];

static void print_prompt(void) {
    wc_set_color(g_wc, VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    wc_write(g_wc, "aswd> ");
    wc_set_color(g_wc, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void reset_line(void) {
    g_line[0] = '\0';
    g_line_len = 0;
    g_line_pos = 0;
    g_hist_pos = -1;
    g_saved_line[0] = '\0';
}

static void history_push(const char *line) {
    int last;
    if (!line || !line[0]) return;
    last = (g_hist_head - 1 + SHELL_HISTORY_MAX) % SHELL_HISTORY_MAX;
    if (g_hist_count > 0 && str_eq(g_history[last], line)) return;
    str_copy(g_history[g_hist_head], line, sizeof(g_history[g_hist_head]));
    g_hist_head = (g_hist_head + 1) % SHELL_HISTORY_MAX;
    if (g_hist_count < SHELL_HISTORY_MAX) g_hist_count++;
}

static const char *history_get(int offset) {
    int idx;
    if (offset < 0 || offset >= g_hist_count) return 0;
    idx = (g_hist_head - 1 - offset + SHELL_HISTORY_MAX * 2) % SHELL_HISTORY_MAX;
    return g_history[idx];
}

static void redraw_current_line(int clear_len) {
    while (g_line_pos > 0) {
        wc_putc(g_wc, '\b');
        g_line_pos--;
    }
    for (int i = 0; i < clear_len; i++) wc_putc(g_wc, ' ');
    for (int i = 0; i < clear_len; i++) wc_putc(g_wc, '\b');
    for (int i = 0; i < g_line_len; i++) wc_putc(g_wc, g_line[i]);
    g_line_pos = g_line_len;
}

static void replace_line(const char *text) {
    int clear_len = g_line_len;
    str_copy(g_line, text ? text : "", sizeof(g_line));
    g_line_len = (int)str_len(g_line);
    if (g_line_len > clear_len) clear_len = g_line_len;
    redraw_current_line(clear_len);
}

static void execute_line(void) {
    wc_putc(g_wc, '\n');

    if (g_line_len == 0) {
        print_prompt();
        return;
    }

    history_push(g_line);

    /* Redirect console output to our winconsole */
    char *argv[16];
    int argc = split_args(g_line, argv, 16);

    if (argc > 0) {
        winconsole_t *prev = console_get_target();
        console_set_target(g_wc);
        commands_dispatch(argc, argv);
        console_set_target(prev);
    }

    reset_line();
    print_prompt();
}

static void shell_gui_on_paint(int win_id) {
    gui_rect_t r = gui_window_content(win_id);
    if (g_wc) {
        gfx_fill_rect(r.x, r.y, r.w, r.h, gfx_rgb(0, 0, 0));
        wc_resize(g_wc, r.w, r.h);
        wc_paint(g_wc, r.x, r.y);
    }
}

static void shell_gui_on_key(int win_id, char key) {
    (void)win_id;
    if (!g_wc) return;

    /* Ctrl+Q closes */
    if (key == 0x11) {
        gui_window_close(g_win_id);
        return;
    }

    /* Enter */
    if (key == '\r' || key == '\n') {
        execute_line();
        return;
    }

    /* Backspace */
    if (key == '\b' || key == (char)0x7F) {
        if (g_line_pos > 0) {
            for (int i = g_line_pos - 1; i < g_line_len - 1; i++)
                g_line[i] = g_line[i + 1];
            g_line_len--;
            g_line_pos--;
            g_line[g_line_len] = '\0';
            wc_putc(g_wc, '\b');
            for (int i = g_line_pos; i < g_line_len; i++)
                wc_putc(g_wc, g_line[i]);
            wc_putc(g_wc, ' ');
            for (int i = g_line_pos; i <= g_line_len; i++)
                wc_putc(g_wc, '\b');
        }
        return;
    }

    /* Arrow keys */
    if (key == KEY_LEFT) {
        if (g_line_pos > 0) { g_line_pos--; wc_putc(g_wc, '\b'); }
        return;
    }
    if (key == KEY_RIGHT) {
        if (g_line_pos < g_line_len) { wc_putc(g_wc, g_line[g_line_pos]); g_line_pos++; }
        return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        int next = (key == KEY_UP) ? g_hist_pos + 1 : g_hist_pos - 1;
        const char *entry;
        if (g_hist_pos == -1 && key == KEY_UP) {
            str_copy(g_saved_line, g_line, sizeof(g_saved_line));
        }
        if (next == -1) {
            entry = g_saved_line;
        } else {
            entry = history_get(next);
            if (!entry) return;
        }
        g_hist_pos = next;
        replace_line(entry);
        return;
    }
    /* Ignore other special keys */
    if ((unsigned char)key < 32 || (unsigned char)key > 126) return;

    /* Insert printable char */
    if (g_line_len + 1 >= (int)sizeof(g_line)) return;

    for (int i = g_line_len; i > g_line_pos; i--)
        g_line[i] = g_line[i - 1];
    g_line[g_line_pos] = key;
    g_line_len++;
    g_line[g_line_len] = '\0';

    wc_putc(g_wc, key);
    for (int i = g_line_pos + 1; i < g_line_len; i++)
        wc_putc(g_wc, g_line[i]);
    g_line_pos++;
    for (int i = g_line_pos; i < g_line_len; i++)
        wc_putc(g_wc, '\b');
}

static void shell_gui_on_close(int win_id) {
    (void)win_id;
    if (g_wc) { wc_free(g_wc); g_wc = 0; }
    g_win_id = -1;
}

void shell_gui_launch(void) {
    gui_rect_t rect;
    int wid;

    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }

    gui_window_suggest_rect(720, 500, &rect);
    wid = gui_window_create("Terminal", rect.x, rect.y, rect.w, rect.h);
    if (wid < 0) return;

    g_wc = wc_alloc();
    if (!g_wc) { gui_window_close(wid); return; }

    g_win_id = wid;
    wc_init(g_wc, wid);
    g_hist_count = 0;
    g_hist_head = 0;
    g_hist_pos = -1;

    gui_window_t *w = gui_get_window(wid);
    w->icon_kind = GUI_ICON_TERMINAL;
    w->on_paint = shell_gui_on_paint;
    w->on_key   = shell_gui_on_key;
    w->on_close = shell_gui_on_close;

    reset_line();

    wc_set_color(g_wc, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    wc_write(g_wc, "AswdOS Terminal\n");
    wc_set_color(g_wc, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    wc_write(g_wc, "Type 'help' for commands. Ctrl+Q to close.\n\n");
    print_prompt();
}
