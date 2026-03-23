#include "gui/settings_gui.h"

#include <stdint.h>

#include "boot/multiboot.h"
#include "common/config.h"
#include "cpu/cpuid.h"
#include "cpu/timer.h"
#include "drivers/font.h"
#include "drivers/gfx.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "fs/vfs.h"
#include "auth/auth_store.h"
#include "gui/gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "usb/usb.h"
#include "users/users.h"
#include "net/net.h"

#define TAB_DISPLAY 0
#define TAB_SYSTEM  1
#define TAB_DEVICES 2
#define TAB_USERS   3
#define TAB_NETWORK 4
#define TAB_COUNT   5

#define COL_BG      gfx_rgb(250, 251, 254)
#define COL_TEXT    gfx_rgb(31, 41, 55)
#define COL_DIM     gfx_rgb(100, 116, 139)
#define COL_TAB     gfx_rgb(226, 232, 240)
#define COL_TAB_ON  gfx_rgb(37, 99, 235)
#define COL_TAB_TXT gfx_rgb(255, 255, 255)
#define COL_RULE    gfx_rgb(203, 213, 225)
#define COL_SWATCH_BORDER gfx_rgb(255, 255, 255)

static int g_win_id = -1;
static int g_tab = 0;
static int g_pending_create = 0;
static char g_create_name[9];
static int  g_create_name_len = 0;
static char g_create_msg[64];
static int  g_create_msg_err = 0;

/* gfx_rgb can't be used at file scope, so presets are built lazily. */
static uint32_t get_preset(int i) {
    switch (i) {
        case 0:  return gfx_rgb(12,  74, 110);  /* dark navy (default) */
        case 1:  return gfx_rgb(20,  83,  45);  /* dark green */
        case 2:  return gfx_rgb(68,  48, 117);  /* dark purple */
        case 3:  return gfx_rgb(100, 30,  30);  /* dark red */
        case 4:  return gfx_rgb(20,  90,  90);  /* dark teal */
        case 5:  return gfx_rgb(28,  28,  36);  /* near-black */
        case 6:  return gfx_rgb(120, 60,  20);  /* dark orange */
        case 7:  return gfx_rgb(10,  60,  80);  /* ocean */
        case 8:  return gfx_rgb(80,  20,  60);  /* dark magenta */
        case 9:  return gfx_rgb(50,  70,  20);  /* olive */
        case 10: return gfx_rgb(14,  14,  60);  /* midnight blue */
        case 11: return gfx_rgb(40,  40,  40);  /* charcoal */
        default: return gfx_rgb(12,  74, 110);
    }
}

#define PRESET_COUNT 12

static void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    gfx_draw_string(x, y, text, fg, bg);
}

static void fmt_uptime(char *out, size_t size) {
    char tmp[16];
    uint32_t secs = timer_uptime_secs();
    uint32_t h = secs / 3600u;
    uint32_t m = (secs % 3600u) / 60u;
    uint32_t s = secs % 60u;
    out[0] = '\0';
    u32_to_dec(h, tmp, sizeof(tmp)); str_cat(out, tmp, size); str_cat(out, "h ", size);
    u32_to_dec(m, tmp, sizeof(tmp)); str_cat(out, tmp, size); str_cat(out, "m ", size);
    u32_to_dec(s, tmp, sizeof(tmp)); str_cat(out, tmp, size); str_cat(out, "s", size);
}

static void draw_kv(int x, int *y, const char *key, const char *value) {
    draw_text(x, *y, key, COL_DIM, COL_BG);
    draw_text(x + 120, *y, value, COL_TEXT, COL_BG);
    *y += 18;
}

static const char *pointer_source_name(uint8_t source) {
    if (source == MOUSE_SOURCE_PS2) return "PS/2";
    if (source == MOUSE_SOURCE_USB) return "USB";
    return "None";
}

static void draw_display_tab(const gui_rect_t *r) {
    int y = r->y + 44;
    int x = r->x + 12;
    int i;
    uint32_t cur = gui_get_desktop_color();
    int swatch_size = 36;
    int swatch_gap  = 6;
    static const char *names[PRESET_COUNT] = {
        "Navy", "Green", "Purple", "Red", "Teal", "Black",
        "Orange", "Ocean", "Magenta", "Olive", "Midnight", "Charcoal"
    };

    draw_text(x, y, "Desktop color:", COL_DIM, COL_BG);
    y += 22;

    /* Two rows of 6 swatches */
    for (i = 0; i < PRESET_COUNT; i++) {
        int row = i / 6, col = i % 6;
        uint32_t preset = get_preset(i);
        int sx = x + col * (swatch_size + swatch_gap);
        int sy = y + row * (swatch_size + 22);
        int selected = (preset == cur);
        if (selected) {
            gfx_fill_rect(sx - 3, sy - 3, swatch_size + 6, swatch_size + 6,
                          gfx_rgb(255, 255, 255));
        } else {
            gfx_fill_rect(sx - 1, sy - 1, swatch_size + 2, swatch_size + 2,
                          gfx_rgb(160, 175, 195));
        }
        gfx_fill_rect(sx, sy, swatch_size, swatch_size, preset);
        draw_text(sx, sy + swatch_size + 4, names[i],
                  selected ? COL_TEXT : COL_DIM, COL_BG);
    }
    y += 2 * (swatch_size + 22) + 8;
    draw_text(x, y, "Click a color swatch to apply.", COL_DIM, COL_BG);
}

static void draw_system_tab(const gui_rect_t *r) {
    char brand[49];
    char mem[24];
    char uptime[32];
    int y = r->y + 40;

    cpuid_get_brand(brand);
    if (multiboot_has_mem_info()) {
        u32_to_dec((multiboot_mem_upper_kb() + 1024u) / 1024u, mem, sizeof(mem));
        str_cat(mem, " MB", sizeof(mem));
    } else {
        str_copy(mem, "unknown", sizeof(mem));
    }
    fmt_uptime(uptime, sizeof(uptime));

    draw_kv(r->x + 12, &y, "CPU", brand);
    draw_kv(r->x + 12, &y, "Memory", mem);
    draw_kv(r->x + 12, &y, "Uptime", uptime);
    draw_kv(r->x + 12, &y, "Filesystem", vfs_available() ? "Mounted" : "Unavailable");
}

static void draw_devices_tab(const gui_rect_t *r) {
    char line[48];
    int y = r->y + 40;
    int i;
    draw_kv(r->x + 12, &y, "Graphics", "Framebuffer desktop");
    draw_kv(r->x + 12, &y, "Keyboard", keyboard_ps2_ready() ? "PS/2 IRQ1 ready" : "BIOS fallback");
    draw_kv(r->x + 12, &y, "PS/2 Mouse", mouse_ps2_detected() ? "Detected" : "Not detected");
    draw_kv(r->x + 12, &y, "Pointer last", pointer_source_name(mouse_last_source()));

    line[0] = '\0';
    u32_to_dec(mouse_irq_count(), line, sizeof(line));
    draw_kv(r->x + 12, &y, "Pointer IRQs", line);

    line[0] = '\0';
    u32_to_dec(mouse_packet_error_count(), line, sizeof(line));
    draw_kv(r->x + 12, &y, "Pointer errors", line);

    line[0] = '\0';
    u32_to_dec((uint32_t)usb_controller_count(), line, sizeof(line));
    str_cat(line, " controller(s)", sizeof(line));
    draw_kv(r->x + 12, &y, "USB", line);

    for (i = 0; i < usb_controller_count() && i < 4; i++) {
        const usb_controller_t *ctrl = usb_controller_at(i);
        line[0] = '\0';
        if (!ctrl) continue;
        str_copy(line, usb_controller_kind_name(ctrl->kind), sizeof(line));
        str_cat(line, ctrl->ready ? " ready" : " found", sizeof(line));
        draw_text(r->x + 24, y, line, COL_TEXT, COL_BG);
        y += 16;
    }
}

static void draw_users_tab(const gui_rect_t *r) {
    int y = r->y + 44;
    int x = r->x + 12;
    int fw = r->w - 24;
    int i;
    int count = users_count();

    draw_text(x, y, "Accounts", COL_DIM, COL_BG);
    y += 20;

    for (i = 0; i < count; i++) {
        th_draw_list_row(x, y, fw, 24, users_name_at(i), 0);
        y += 26;
    }
    if (count == 0) {
        draw_text(x, y, "(no accounts)", COL_DIM, COL_BG);
        y += 20;
    }

    y += 6;
    th_draw_separator(x, y, fw);
    y += 10;

    if (users_current_is_admin()) {
        draw_text(x, y, "New account name:", COL_DIM, COL_BG);
        y += 18;
        th_draw_field(x, y, fw - 96, g_create_name, 1, 0);
        th_draw_button(x + fw - 88, y, 80, FONT_HEIGHT + 10, "Create", 0);
        y += FONT_HEIGHT + 18;
        if (g_create_msg[0]) {
            uint32_t fg = g_create_msg_err ? TH_STATUS_ERR : TH_TEXT_DIM;
            draw_text(x, y, g_create_msg, fg, COL_BG);
        }
    } else {
        draw_text(x, y, "Admin access required to add accounts.", COL_DIM, COL_BG);
    }
}

static void draw_network_tab(const gui_rect_t *r) {
    int y = r->y + 40;
    int x = r->x + 12;
    const net_info_t *ni = net_get_info();

    draw_kv(x, &y, "NIC", ni->nic_name[0] ? ni->nic_name : "Not detected");
    if (ni->nic_name[0]) {
        char mac[20];
        const uint8_t *m = ni->mac;
        char tmp[4];
        int mi;
        mac[0] = '\0';
        for (mi = 0; mi < 6; mi++) {
            static const char hex[] = "0123456789ABCDEF";
            tmp[0] = hex[(m[mi] >> 4) & 0xF];
            tmp[1] = hex[m[mi] & 0xF];
            tmp[2] = (mi < 5) ? ':' : '\0';
            tmp[3] = '\0';
            str_cat(mac, tmp, sizeof(mac));
        }
        draw_kv(x, &y, "MAC", mac);

        if (ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3]) {
            char ip[20];
            char tmp2[6];
            int ii;
            ip[0] = '\0';
            for (ii = 0; ii < 4; ii++) {
                u32_to_dec(ni->ip[ii], tmp2, sizeof(tmp2));
                str_cat(ip, tmp2, sizeof(ip));
                if (ii < 3) str_cat(ip, ".", sizeof(ip));
            }
            draw_kv(x, &y, "IP", ip);

            char gw[20];
            gw[0] = '\0';
            for (ii = 0; ii < 4; ii++) {
                u32_to_dec(ni->gateway[ii], tmp2, sizeof(tmp2));
                str_cat(gw, tmp2, sizeof(gw));
                if (ii < 3) str_cat(gw, ".", sizeof(gw));
            }
            draw_kv(x, &y, "Gateway", gw);
            draw_kv(x, &y, "DHCP", "Configured");
        } else {
            draw_kv(x, &y, "IP", ni->dhcp_pending ? "Requesting..." : "Not configured");
        }
    }
}

static void control_panel_on_paint(int win_id) {
    static const char *tabs[TAB_COUNT] = { "Display", "System", "Devices", "Users", "Network" };
    gui_rect_t r = gui_window_content(win_id);
    int gap = 6;
    int tab_w = (r.w - 24 - gap * (TAB_COUNT - 1)) / TAB_COUNT;
    int x = r.x + 12;
    int i;

    if (tab_w < 72) tab_w = 72;

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    for (i = 0; i < TAB_COUNT; i++) {
        uint32_t bg = (i == g_tab) ? COL_TAB_ON : COL_TAB;
        uint32_t fg = (i == g_tab) ? COL_TAB_TXT : COL_TEXT;
        gfx_fill_rect(x, r.y + 8, tab_w, 20, bg);
        draw_text(x + 10, r.y + 12, tabs[i], fg, bg);
        x += tab_w + gap;
    }
    gfx_fill_rect(r.x + 8, r.y + 34, r.w - 16, 1, COL_RULE);

    if (g_tab == TAB_DISPLAY) draw_display_tab(&r);
    else if (g_tab == TAB_SYSTEM) draw_system_tab(&r);
    else if (g_tab == TAB_DEVICES) draw_devices_tab(&r);
    else if (g_tab == TAB_USERS) draw_users_tab(&r);
    else draw_network_tab(&r);
}

static void control_panel_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }
    if (key == KEY_LEFT && g_tab > 0) { g_tab--; return; }
    if (key == KEY_RIGHT && g_tab + 1 < TAB_COUNT) { g_tab++; return; }

    if (g_tab == TAB_USERS && users_current_is_admin()) {
        if ((key == '\b' || key == (char)0x7F) && g_create_name_len > 0) {
            g_create_name[--g_create_name_len] = '\0';
            g_create_msg[0] = '\0';
            return;
        }
        if (key == '\r' || key == '\n') {
            if (g_create_name_len == 0) {
                str_copy(g_create_msg, "Enter a name first.", sizeof(g_create_msg));
                g_create_msg_err = 1;
                return;
            }
            if (users_create(g_create_name, 0)) {
                str_copy(g_create_msg, "User created.", sizeof(g_create_msg));
                g_create_msg_err = 0;
            } else {
                str_copy(g_create_msg, "Name invalid or already exists.", sizeof(g_create_msg));
                g_create_msg_err = 1;
            }
            g_create_name[0] = '\0';
            g_create_name_len = 0;
            return;
        }
        if ((unsigned char)key >= 32 && (unsigned char)key <= 126 && g_create_name_len < 8) {
            char ch = key;
            if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
            if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
                g_create_name[g_create_name_len++] = ch;
                g_create_name[g_create_name_len] = '\0';
                g_create_msg[0] = '\0'; g_create_msg_err = 0;
            }
        }
    }
}

static void control_panel_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    gui_rect_t r;
    (void)win_id;
    if (!(buttons & 1)) return;
    r = gui_window_content(g_win_id);

    /* Tab row (y < 34) */
    if (y < 34) {
        int gap = 6;
        int tab_w = (r.w - 24 - gap * (TAB_COUNT - 1)) / TAB_COUNT;
        int i;
        if (tab_w < 72) tab_w = 72;
        for (i = 0; i < TAB_COUNT; i++) {
            int tx = 12 + i * (tab_w + gap);
            if (x >= tx && x < tx + tab_w) { g_tab = i; return; }
        }
        return;
    }

    /* Display tab: color swatch clicks (2 rows of 6) */
    if (g_tab == TAB_DISPLAY) {
        int swatch_y    = 44 + 22;
        int swatch_size = 36;
        int swatch_gap  = 6;
        int i;
        for (i = 0; i < PRESET_COUNT; i++) {
            int row = i / 6, col = i % 6;
            int sx = 12 + col * (swatch_size + swatch_gap);
            int sy = swatch_y + row * (swatch_size + 22);
            if (x >= sx - 3 && x < sx + swatch_size + 3 &&
                y >= sy - 3 && y < sy + swatch_size + 3) {
                gui_set_desktop_color(get_preset(i));
                return;
            }
        }
    }

    if (g_tab == TAB_USERS && users_current_is_admin()) {
        /* Create button: matches position in draw_users_tab */
        int count = users_count();
        int btn_y = 44 + 20 + count * 26 + (count == 0 ? 20 : 0) + 6 + 10 + 18;
        int fw = r.w - 24;
        int btn_x = 12 + fw - 88;
        int fh = FONT_HEIGHT + 10;
        if (x >= btn_x && x < btn_x + 80 && y >= btn_y && y < btn_y + fh) {
            if (g_create_name_len == 0) {
                str_copy(g_create_msg, "Enter a name first.", sizeof(g_create_msg));
                g_create_msg_err = 1;
            } else if (users_create(g_create_name, 0)) {
                str_copy(g_create_msg, "User created.", sizeof(g_create_msg));
                g_create_msg_err = 0;
                g_create_name[0] = '\0'; g_create_name_len = 0;
            } else {
                str_copy(g_create_msg, "Name invalid or already exists.", sizeof(g_create_msg));
                g_create_msg_err = 1;
                g_create_name[0] = '\0'; g_create_name_len = 0;
            }
        }
    }
}

static void control_panel_on_close(int win_id) {
    (void)win_id;
    g_win_id = -1;
}

void settings_gui_launch(void) {
    gui_window_t *w;
    gui_rect_t rect;
    if (g_win_id >= 0 && gui_window_active(g_win_id)) {
        gui_window_focus(g_win_id);
        return;
    }
    gui_window_suggest_rect(580, 340, &rect);
    g_win_id = gui_window_create("Control Panel", rect.x, rect.y, rect.w, rect.h);
    if (g_win_id < 0) return;
    g_tab = 0;
    g_create_name[0] = '\0'; g_create_name_len = 0;
    g_create_msg[0] = '\0'; g_create_msg_err = 0;
    w = gui_get_window(g_win_id);
    w->icon_kind = GUI_ICON_SETTINGS;
    w->on_paint = control_panel_on_paint;
    w->on_key   = control_panel_on_key;
    w->on_mouse = control_panel_on_mouse;
    w->on_close = control_panel_on_close;
}

/* Alias for callers that used the old name */
void control_panel_launch(void) {
    settings_gui_launch();
}

void control_panel_open_users(void) {
    settings_gui_launch();
    g_tab = TAB_USERS;
    g_pending_create = 1;
}
