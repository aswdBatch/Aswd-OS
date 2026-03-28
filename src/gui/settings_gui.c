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
#include "gui/permission_gui.h"
#include "gui/theme.h"
#include "lib/string.h"
#include "usb/usb.h"
#include "users/users.h"
#include "net/net.h"
#include "net/wifi.h"
#include "net/site_allow.h"

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
static char g_wifi_ssid_input[WIFI_SSID_MAX + 1];
static int  g_wifi_ssid_len = 0;
static char g_wifi_pass_input[WIFI_PASSPHRASE_MAX + 1];
static int  g_wifi_pass_len = 0;
static char g_wifi_msg[96];
static int  g_wifi_msg_err = 0;
static int  g_wifi_scan_sel = 0;
static int  g_wifi_saved_sel = 0;
static int  g_wifi_security_sel = WIFI_SECURITY_WPA2_PSK;
static char g_site_input[SITE_ALLOW_HOST_MAX];
static int  g_site_input_len = 0;
static char g_site_msg[80];
static int  g_site_msg_err = 0;
static int  g_site_sel = 0;

typedef enum {
    NETWORK_FOCUS_WIFI_SCAN = 0,
    NETWORK_FOCUS_WIFI_SAVED,
    NETWORK_FOCUS_WIFI_SSID,
    NETWORK_FOCUS_WIFI_PASS,
    NETWORK_FOCUS_SITE,
} settings_network_focus_t;

static settings_network_focus_t g_network_focus = NETWORK_FOCUS_WIFI_SSID;

static int display_theme_card_rect(const gui_rect_t *r, int index, gui_rect_t *out);
static gui_rect_t settings_body_rect(void);
static gui_rect_t settings_inner_rect(const gui_rect_t *body);

typedef struct {
    gui_rect_t wifi_card_rect;
    gui_rect_t wifi_scan_rect;
    gui_rect_t wifi_saved_rect;
    gui_rect_t wifi_ssid_rect;
    gui_rect_t wifi_pass_rect;
    gui_rect_t wifi_security_btn;
    gui_rect_t wifi_scan_btn;
    gui_rect_t wifi_connect_btn;
    gui_rect_t wifi_save_btn;
    gui_rect_t wifi_forget_btn;
    gui_rect_t wifi_disconnect_btn;
    gui_rect_t wifi_msg_rect;
    gui_rect_t site_card_rect;
    gui_rect_t site_list_rect;
    gui_rect_t site_input_rect;
    gui_rect_t add_btn;
    gui_rect_t remove_btn;
    gui_rect_t footer_rect;
    int wifi_rows_to_draw;
    int wifi_scan_start;
    int wifi_saved_start;
    int site_rows_to_draw;
    int site_list_start;
} settings_network_layout_t;

static void network_tab_compute_layout(const gui_rect_t *body, int count,
                                       settings_network_layout_t *layout);
static void format_ipv4(char *out, size_t size, const uint8_t *ip);
static const char *network_focus_hint(void);
static void ensure_network_wifi_ready(void);
static void sync_wifi_inputs_from_saved(int index);
static void sync_wifi_inputs_from_scan(int index);
static void set_wifi_message_from_result(int rc, const char *success_msg);
static void wifi_action_scan(void);
static void wifi_action_save(void);
static void wifi_action_connect(void);
static void wifi_action_disconnect(void);
static void wifi_action_forget(void);

static void draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    th_draw_text(x, y, text, fg, bg, th_metrics()->font_body);
}

static void draw_text_small(int x, int y, const char *text, uint32_t fg, uint32_t bg) {
    th_draw_text(x, y, text, fg, bg, th_metrics()->font_small);
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
    const th_metrics_t *tm = th_metrics();

    draw_text_small(x, *y + 2, key, COL_DIM, COL_BG);
    draw_text(x + 132, *y, value, COL_TEXT, COL_BG);
    *y += gfx_font_line_height(FONT_ROLE_UI, tm->font_body) + tm->gap_sm;
}

static const char *pointer_source_name(uint8_t source) {
    if (source == MOUSE_SOURCE_PS2) return "PS/2";
    if (source == MOUSE_SOURCE_USB) return "USB";
    return "None";
}

static void format_ipv4(char *out, size_t size, const uint8_t *ip) {
    char tmp[6];
    out[0] = '\0';
    for (int i = 0; i < 4; i++) {
        u32_to_dec(ip[i], tmp, sizeof(tmp));
        str_cat(out, tmp, size);
        if (i < 3) str_cat(out, ".", size);
    }
}

static void usb_controller_summary(char *out, size_t size, const usb_controller_t *ctrl) {
    out[0] = '\0';
    if (!ctrl) return;
    str_copy(out, usb_controller_kind_name(ctrl->kind), size);
    str_cat(out, ctrl->ready ? " ready" : " detected", size);
    if (ctrl->supports_control || ctrl->supports_interrupt || ctrl->supports_bulk) {
        str_cat(out, " (", size);
        if (ctrl->supports_control) str_cat(out, "ctl", size);
        if (ctrl->supports_interrupt) {
            if (ctrl->supports_control) str_cat(out, "/", size);
            str_cat(out, "int", size);
        }
        if (ctrl->supports_bulk) {
            if (ctrl->supports_control || ctrl->supports_interrupt) str_cat(out, "/", size);
            str_cat(out, "bulk", size);
        }
        str_cat(out, ")", size);
    }
}

static const char *network_focus_hint(void) {
    switch (g_network_focus) {
        case NETWORK_FOCUS_WIFI_SCAN:
            return "Scan list focused. Up/Down selects. Enter tries Connect.";
        case NETWORK_FOCUS_WIFI_SAVED:
            return "Saved list focused. Up/Down selects. Enter connects. Delete forgets.";
        case NETWORK_FOCUS_WIFI_PASS:
            return "Passphrase field focused. Left/Right cycle security. Enter saves profile.";
        case NETWORK_FOCUS_SITE:
            return "Allowed-site field focused. Enter adds the current host.";
        default:
            return "SSID field focused. Left/Right cycle security. Enter saves profile.";
    }
}

static void ensure_network_wifi_ready(void) {
    if (!wifi_is_initialized()) {
        wifi_init();
    }
}

static void sync_wifi_inputs_from_saved(int index) {
    const wifi_saved_network_t *saved = wifi_saved_at(index);
    if (!saved) return;
    str_copy(g_wifi_ssid_input, saved->ssid, sizeof(g_wifi_ssid_input));
    str_copy(g_wifi_pass_input, saved->passphrase, sizeof(g_wifi_pass_input));
    g_wifi_ssid_len = (int)str_len(g_wifi_ssid_input);
    g_wifi_pass_len = (int)str_len(g_wifi_pass_input);
    g_wifi_security_sel = saved->security;
}

static void sync_wifi_inputs_from_scan(int index) {
    const wifi_network_t *scan = wifi_scan_at(index);
    if (!scan) return;
    str_copy(g_wifi_ssid_input, scan->ssid, sizeof(g_wifi_ssid_input));
    g_wifi_ssid_len = (int)str_len(g_wifi_ssid_input);
    g_wifi_security_sel = scan->security;
}

static void set_wifi_message_from_result(int rc, const char *success_msg) {
    if (rc == WIFI_OK) {
        str_copy(g_wifi_msg, success_msg ? success_msg : "Done.", sizeof(g_wifi_msg));
        g_wifi_msg_err = 0;
        return;
    }
    if ((rc == WIFI_ERR_BACKEND || rc == WIFI_ERR_SCAN || rc == WIFI_ERR_UNSUPPORTED) &&
        wifi_connection_note()[0]) {
        str_copy(g_wifi_msg, wifi_connection_note(), sizeof(g_wifi_msg));
    } else {
        str_copy(g_wifi_msg, wifi_result_string(rc), sizeof(g_wifi_msg));
    }
    g_wifi_msg_err = 1;
}

static void wifi_action_scan(void) {
    set_wifi_message_from_result(wifi_scan_request(), "Scan requested.");
}

static void wifi_action_save(void) {
    set_wifi_message_from_result(
        wifi_save_network(g_wifi_ssid_input, (wifi_security_t)g_wifi_security_sel, g_wifi_pass_input),
        "Saved Wi-Fi profile.");
}

static void wifi_action_connect(void) {
    int rc;
    if (g_network_focus == NETWORK_FOCUS_WIFI_SAVED && wifi_saved_count() > 0) {
        rc = wifi_connect_saved(g_wifi_saved_sel);
    } else if (g_network_focus == NETWORK_FOCUS_WIFI_SCAN && wifi_scan_count() > 0) {
        rc = wifi_connect_scan(g_wifi_scan_sel, g_wifi_pass_input);
    } else {
        rc = wifi_connect_manual(g_wifi_ssid_input, (wifi_security_t)g_wifi_security_sel, g_wifi_pass_input);
    }
    set_wifi_message_from_result(rc, "Connect requested.");
}

static void wifi_action_disconnect(void) {
    wifi_disconnect();
    str_copy(g_wifi_msg, "Wi-Fi session cleared.", sizeof(g_wifi_msg));
    g_wifi_msg_err = 0;
}

static void wifi_action_forget(void) {
    set_wifi_message_from_result(wifi_forget_saved(g_wifi_saved_sel), "Saved Wi-Fi removed.");
    if (g_wifi_saved_sel >= wifi_saved_count() && g_wifi_saved_sel > 0) g_wifi_saved_sel--;
}

static void draw_display_tab(const gui_rect_t *r) {
    const th_metrics_t *tm = th_metrics();
    const gfx_display_profile_t *dp = gfx_display_profile();
    gui_rect_t inner = settings_inner_rect(r);
    int y = inner.y;
    int x = inner.x;
    int last_card_y = inner.y;

    draw_text_small(x, y, "Themes", TH_ACCENT, COL_BG);
    y += tm->font_small + tm->gap_sm;

    for (int i = 0; i < gui_background_theme_count(); i++) {
        gui_rect_t card;
        int selected = (i == (int)gui_get_background_theme());

        if (!display_theme_card_rect(r, i, &card)) continue;
        if (card.y > last_card_y) last_card_y = card.y;
        th_draw_card(card.x, card.y, card.w, card.h, 0,
                     selected ? gfx_rgb(240, 246, 255) : gfx_rgb(251, 252, 255), selected);
        gfx_fill_rect_gradient_h(card.x + 1, card.y + 1, card.w - 2, 10,
                                 gui_background_theme_preview_color(i),
                                 gfx_rgb(24, 35, 50));
        draw_text(card.x + 10, card.y + 18, gui_background_theme_name(i), COL_TEXT, TH_BG_CARD);
        draw_text_small(card.x + 10, card.y + 42,
                  selected ? "Active desktop theme" : "Click to apply",
                  selected ? TH_ACCENT : COL_DIM, TH_BG_CARD);
        if (selected) {
            th_draw_badge(card.x + card.w - 58, card.y + 14, "Active", TH_ACCENT, TH_TEXT_INVERT);
        }
    }

    y = last_card_y + 88;
    if (y + 120 > inner.y + inner.h) {
        y = inner.y + inner.h - 120;
    }
    if (y < inner.y + 94) y = inner.y + 94;
    th_draw_separator(x, y - tm->gap_sm, inner.w);
    draw_text_small(x, y, "Display profile", TH_ACCENT, COL_BG);
    y += tm->font_small + tm->gap_sm;
    {
        char line[48];
        char num[16];
        line[0] = '\0';
        u32_to_dec(dp->framebuffer_w, num, sizeof(num));
        str_copy(line, num, sizeof(line));
        str_cat(line, " x ", sizeof(line));
        u32_to_dec(dp->framebuffer_h, num, sizeof(num));
        str_cat(line, num, sizeof(line));
        draw_kv(x, &y, "Framebuffer", line);
    }
    draw_kv(x, &y, "Aspect",
            dp->aspect == GFX_ASPECT_4_3 ? "4:3"
            : (dp->aspect == GFX_ASPECT_16_10 ? "16:10" : "16:9"));
    draw_kv(x, &y, "Density",
            dp->density == GFX_DENSITY_COMPACT ? "Compact"
            : (dp->density == GFX_DENSITY_NORMAL ? "Normal" : "Comfortable"));
}

static int display_theme_card_rect(const gui_rect_t *r, int index, gui_rect_t *out) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t inner = settings_inner_rect(r);
    int cols = 1;
    int card_w;
    int col = index % cols;
    int row = index / cols;
    int gap = tm->gap_md;

    th_measure_grid(inner.w, 180, gap, 3, &cols, &card_w);
    col = index % cols;
    row = index / cols;

    if (!out) return 0;
    out->x = inner.x + col * (card_w + gap);
    out->y = inner.y + 18 + row * 88;
    out->w = card_w;
    out->h = 74;
    return 1;
}

static gui_rect_t settings_body_rect(void) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(g_win_id);
    gui_rect_t body;
    int header_h = th_page_header_height();
    int body_y = r.y + 8 + header_h + tm->tab_h + tm->gap_md;

    body.x = r.x + 10;
    body.y = body_y;
    body.w = r.w - 20;
    body.h = r.h - (body.y - r.y) - 10;
    if (body.h < 80) body.h = 80;
    return body;
}

static gui_rect_t settings_inner_rect(const gui_rect_t *body) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t inner = *body;

    inner.x += tm->card_pad + 4;
    inner.y += tm->card_pad + 4;
    inner.w -= (tm->card_pad + 4) * 2;
    inner.h -= (tm->card_pad + 4) * 2;
    if (inner.w < 32) inner.w = 32;
    if (inner.h < 32) inner.h = 32;
    return inner;
}

static void draw_system_tab(const gui_rect_t *r) {
    gui_rect_t inner = settings_inner_rect(r);
    char brand[49];
    char mem[24];
    char uptime[32];
    int y = inner.y;

    cpuid_get_brand(brand);
    if (multiboot_has_mem_info()) {
        u32_to_dec((multiboot_mem_upper_kb() + 1024u) / 1024u, mem, sizeof(mem));
        str_cat(mem, " MB", sizeof(mem));
    } else {
        str_copy(mem, "unknown", sizeof(mem));
    }
    fmt_uptime(uptime, sizeof(uptime));

    draw_text_small(inner.x, y, "System", TH_ACCENT, COL_BG);
    y += th_metrics()->font_small + th_metrics()->gap_sm;
    draw_kv(inner.x, &y, "CPU", brand);
    draw_kv(inner.x, &y, "Memory", mem);
    draw_kv(inner.x, &y, "Uptime", uptime);
    draw_kv(inner.x, &y, "Filesystem", vfs_available() ? "Mounted" : "Unavailable");
}

static void draw_devices_tab(const gui_rect_t *r) {
    gui_rect_t inner = settings_inner_rect(r);
    char line[48];
    int y = inner.y;
    int i;
    draw_text_small(inner.x, y, "Devices", TH_ACCENT, COL_BG);
    y += th_metrics()->font_small + th_metrics()->gap_sm;
    draw_kv(inner.x, &y, "Graphics", "Framebuffer desktop");
    draw_kv(inner.x, &y, "Keyboard", keyboard_ps2_ready() ? "PS/2 IRQ1 ready" : "BIOS fallback");
    draw_kv(inner.x, &y, "PS/2 Mouse", mouse_ps2_detected() ? "Detected" : "Not detected");
    draw_kv(inner.x, &y, "Pointer last", pointer_source_name(mouse_last_source()));

    line[0] = '\0';
    u32_to_dec(mouse_irq_count(), line, sizeof(line));
    draw_kv(inner.x, &y, "Pointer IRQs", line);

    line[0] = '\0';
    u32_to_dec(mouse_packet_error_count(), line, sizeof(line));
    draw_kv(inner.x, &y, "Pointer errors", line);

    line[0] = '\0';
    u32_to_dec((uint32_t)usb_controller_count(), line, sizeof(line));
    str_cat(line, " controller(s)", sizeof(line));
    draw_kv(inner.x, &y, "USB", line);
    line[0] = '\0';
    u32_to_dec((uint32_t)usb_device_count(), line, sizeof(line));
    str_cat(line, " device(s)", sizeof(line));
    draw_kv(inner.x, &y, "USB enum", line);

    for (i = 0; i < usb_controller_count() && i < 4; i++) {
        const usb_controller_t *ctrl = usb_controller_at(i);
        if (!ctrl) continue;
        usb_controller_summary(line, sizeof(line), ctrl);
        draw_text(inner.x + 16, y, line, COL_TEXT, COL_BG);
        y += gfx_font_line_height(FONT_ROLE_UI, th_metrics()->font_body);
    }
}

static void draw_users_tab(const gui_rect_t *r) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t inner = settings_inner_rect(r);
    int y = inner.y;
    int x = inner.x;
    int fw = inner.w;
    int i;
    int count = users_count();

    draw_text_small(x, y, "Accounts", TH_ACCENT, COL_BG);
    y += tm->font_small + tm->gap_sm;

    for (i = 0; i < count; i++) {
        th_draw_list_row(x, y, fw, tm->list_row_h, users_name_at(i), 0);
        y += tm->list_row_h + 4;
    }
    if (count == 0) {
        draw_text_small(x, y, "(no accounts)", COL_DIM, COL_BG);
        y += tm->font_small + tm->gap_md;
    }

    y += tm->gap_sm;
    th_draw_separator(x, y, fw);
    y += tm->gap_sm;

    draw_text_small(x, y, "New account name", COL_DIM, COL_BG);
    y += tm->font_small + tm->gap_sm;
    th_draw_field(x, y, fw - 96, g_create_name, 1, 0);
    th_draw_button(x + fw - 88, y, 80, tm->button_h, "Create", 0);
    y += tm->button_h + tm->gap_md;
    if (g_create_msg[0]) {
        uint32_t fg = g_create_msg_err ? TH_STATUS_ERR : TH_TEXT_DIM;
        draw_text_small(x, y, g_create_msg, fg, COL_BG);
    }
}

static void draw_network_tab(const gui_rect_t *r) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t inner = settings_inner_rect(r);
    int y = inner.y;
    int x = inner.x;
    int fw = inner.w;
    const net_info_t *ni = net_get_info();
    const wifi_adapter_info_t *wa = wifi_adapter_info();
    int count = site_allow_count();
    int scan_count = wifi_scan_count();
    int saved_count = wifi_saved_count();
    settings_network_layout_t layout;
    char left[64];
    char center[64];
    char right[64];
    char line[96];
    char state_badge[32];

    ensure_network_wifi_ready();
    scan_count = wifi_scan_count();
    saved_count = wifi_saved_count();

    draw_text_small(x, y, "Network", TH_ACCENT, COL_BG);
    y += tm->font_small + tm->gap_sm;

    str_copy(left, ni->nic_name[0] ? ni->nic_name : "No wired NIC", sizeof(left));
    if (ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3]) {
        format_ipv4(line, sizeof(line), ni->ip);
        str_cat(left, " • ", sizeof(left));
        str_cat(left, line, sizeof(left));
    }
    if (ni->link_up && !(ni->ip[0] || ni->ip[1] || ni->ip[2] || ni->ip[3])) {
        str_cat(left, ni->dhcp_pending ? " • DHCP" : " • idle", sizeof(left));
    }
    str_copy(center, wa->present ? wa->name : "Wi-Fi not detected", sizeof(center));
    str_copy(right, net_transport_name(ni->active_transport), sizeof(right));
    str_cat(right, " ", sizeof(right));
    str_cat(right, net_connection_state_name(ni->connection_state), sizeof(right));
    th_draw_info_strip(x, y, fw, left, center, right);

    network_tab_compute_layout(r, count, &layout);
    th_draw_card(layout.wifi_card_rect.x, layout.wifi_card_rect.y,
                 layout.wifi_card_rect.w, layout.wifi_card_rect.h, 0, TH_BG_CARD, 0);
    {
        int pad = tm->card_pad;
        int card_x = layout.wifi_card_rect.x + pad;
        int card_y = layout.wifi_card_rect.y + pad;
        int label_y = layout.wifi_scan_rect.y - tm->font_small - 2;
        int note_y = card_y + tm->font_small + tm->gap_sm;

        str_copy(state_badge,
                 !wa->present ? "No adapter"
                 : (!wa->supported ? "Unsupported"
                 : (wa->backend_ready ? "Driver ready" : (wa->transport_limited ? "USB limited" : "Driver pending"))),
                 sizeof(state_badge));
        th_draw_text(card_x, card_y, "Wi-Fi manager", TH_ACCENT, TH_BG_CARD, tm->font_small);
        th_draw_badge(layout.wifi_card_rect.x + layout.wifi_card_rect.w - 112, card_y - 2,
                      state_badge, TH_ACCENT, TH_TEXT_INVERT);
        th_draw_text(card_x, note_y,
                     wa->present ? wa->name : "No supported Wi-Fi family is attached right now.",
                     TH_TEXT, TH_BG_CARD, tm->font_body);
        line[0] = '\0';
        if (wa->present) {
            str_copy(line, wifi_family_name(wa->family), sizeof(line));
            str_cat(line, " • ", sizeof(line));
            str_cat(line, wifi_connection_note(), sizeof(line));
        } else {
            str_copy(line, "Saved profiles can still be staged here for later hardware testing.", sizeof(line));
        }
        th_draw_text(card_x, note_y + tm->font_body + 2, line, TH_TEXT_DIM, TH_BG_CARD, tm->font_small);

        th_draw_text(layout.wifi_scan_rect.x, label_y, "Scanned networks", TH_TEXT_DIM, TH_BG_CARD, tm->font_small);
        th_draw_text(layout.wifi_saved_rect.x, label_y, "Saved profiles", TH_TEXT_DIM, TH_BG_CARD, tm->font_small);

        if (scan_count == 0) {
            const char *empty_scan = !wa->present ? "(no Wi-Fi adapter)"
                : (!wa->backend_ready ? "(live scan backend unavailable)" : "(no networks found)");
            th_draw_list_row(layout.wifi_scan_rect.x, layout.wifi_scan_rect.y, layout.wifi_scan_rect.w,
                             tm->list_row_h, empty_scan, 0);
        } else {
            if (g_wifi_scan_sel >= scan_count) g_wifi_scan_sel = scan_count - 1;
            if (g_wifi_scan_sel < 0) g_wifi_scan_sel = 0;
            for (int i = 0; i < layout.wifi_rows_to_draw; i++) {
                int idx = layout.wifi_scan_start + i;
                char row[48];
                int row_y = layout.wifi_scan_rect.y + i * (tm->list_row_h + 4);
                const wifi_network_t *scan = wifi_scan_at(idx);
                if (!scan) continue;
                row[0] = '\0';
                str_copy(row, scan->ssid, sizeof(row));
                str_cat(row, " • ", sizeof(row));
                str_cat(row, wifi_security_name(scan->security), sizeof(row));
                th_draw_list_row(layout.wifi_scan_rect.x, row_y, layout.wifi_scan_rect.w,
                                 tm->list_row_h, row, idx == g_wifi_scan_sel);
            }
        }

        if (saved_count == 0) {
            th_draw_list_row(layout.wifi_saved_rect.x, layout.wifi_saved_rect.y, layout.wifi_saved_rect.w,
                             tm->list_row_h, "(no saved profiles)", 0);
        } else {
            if (g_wifi_saved_sel >= saved_count) g_wifi_saved_sel = saved_count - 1;
            if (g_wifi_saved_sel < 0) g_wifi_saved_sel = 0;
            for (int i = 0; i < layout.wifi_rows_to_draw; i++) {
                int idx = layout.wifi_saved_start + i;
                char row[48];
                int row_y = layout.wifi_saved_rect.y + i * (tm->list_row_h + 4);
                const wifi_saved_network_t *saved = wifi_saved_at(idx);
                if (!saved) continue;
                row[0] = '\0';
                str_copy(row, saved->ssid, sizeof(row));
                str_cat(row, " • ", sizeof(row));
                str_cat(row, wifi_security_name(saved->security), sizeof(row));
                th_draw_list_row(layout.wifi_saved_rect.x, row_y, layout.wifi_saved_rect.w,
                                 tm->list_row_h, row, idx == g_wifi_saved_sel);
            }
        }

        th_draw_field(layout.wifi_ssid_rect.x, layout.wifi_ssid_rect.y,
                      layout.wifi_ssid_rect.w, g_wifi_ssid_input,
                      g_network_focus == NETWORK_FOCUS_WIFI_SSID, 0);
        th_draw_field(layout.wifi_pass_rect.x, layout.wifi_pass_rect.y,
                      layout.wifi_pass_rect.w, g_wifi_pass_input,
                      g_network_focus == NETWORK_FOCUS_WIFI_PASS, 1);
        th_draw_button(layout.wifi_security_btn.x, layout.wifi_security_btn.y,
                       layout.wifi_security_btn.w, tm->button_h,
                       wifi_security_name((wifi_security_t)g_wifi_security_sel),
                       g_network_focus == NETWORK_FOCUS_WIFI_SSID || g_network_focus == NETWORK_FOCUS_WIFI_PASS);
        th_draw_button(layout.wifi_scan_btn.x, layout.wifi_scan_btn.y,
                       layout.wifi_scan_btn.w, tm->button_h, "Scan", 0);
        th_draw_button(layout.wifi_connect_btn.x, layout.wifi_connect_btn.y,
                       layout.wifi_connect_btn.w, tm->button_h, "Connect", 0);
        th_draw_button(layout.wifi_save_btn.x, layout.wifi_save_btn.y,
                       layout.wifi_save_btn.w, tm->button_h, "Save", 0);
        th_draw_button(layout.wifi_forget_btn.x, layout.wifi_forget_btn.y,
                       layout.wifi_forget_btn.w, tm->button_h, "Forget", 0);
        th_draw_button(layout.wifi_disconnect_btn.x, layout.wifi_disconnect_btn.y,
                       layout.wifi_disconnect_btn.w, tm->button_h, "Disconnect", 0);
        th_draw_text(layout.wifi_msg_rect.x, layout.wifi_msg_rect.y,
                     g_wifi_msg[0] ? g_wifi_msg : network_focus_hint(),
                     g_wifi_msg[0] ? (g_wifi_msg_err ? TH_STATUS_ERR : TH_TEXT_DIM) : TH_TEXT_DIM,
                     TH_BG_CARD, tm->font_small);
    }

    th_draw_card(layout.site_card_rect.x, layout.site_card_rect.y,
                 layout.site_card_rect.w, layout.site_card_rect.h, 0, TH_BG_CARD, 0);
    {
        int header_y = layout.site_card_rect.y + tm->card_pad;
        th_draw_text(layout.site_card_rect.x + tm->card_pad, header_y,
                     "Allowed sites", TH_ACCENT, TH_BG_CARD, tm->font_small);
        if (count > 0) {
            char badge[8];
            u32_to_dec((uint32_t)count, badge, sizeof(badge));
            th_draw_badge(layout.site_card_rect.x + layout.site_card_rect.w - 40, header_y - 2,
                          badge, TH_ACCENT, TH_TEXT_INVERT);
        }
        th_draw_text(layout.site_card_rect.x + tm->card_pad, header_y + tm->font_small + 2,
                     "Empty list means Browser and fetch can reach any host this session.",
                     TH_TEXT_DIM, TH_BG_CARD, tm->font_small);
    }

    if (count == 0) {
        th_draw_list_row(layout.site_list_rect.x, layout.site_list_rect.y, layout.site_list_rect.w,
                         tm->list_row_h, "(no allowed sites yet)", 0);
    } else {
        if (g_site_sel >= count) g_site_sel = count - 1;
        if (g_site_sel < 0) g_site_sel = 0;
        for (int i = 0; i < layout.site_rows_to_draw; i++) {
            int idx = layout.site_list_start + i;
            const char *host = site_allow_host_at(idx);
            int row_y = layout.site_list_rect.y + i * (tm->list_row_h + 4);
            th_draw_list_row(layout.site_list_rect.x, row_y, layout.site_list_rect.w,
                             tm->list_row_h, host ? host : "", idx == g_site_sel);
        }
    }

    th_draw_field(layout.site_input_rect.x, layout.site_input_rect.y, layout.site_input_rect.w,
                  g_site_input, g_network_focus == NETWORK_FOCUS_SITE, 0);
    th_draw_button(layout.add_btn.x, layout.add_btn.y, layout.add_btn.w, tm->button_h, "Add", 0);
    th_draw_button(layout.remove_btn.x, layout.remove_btn.y, layout.remove_btn.w, tm->button_h, "Remove", count > 0);
    y = layout.footer_rect.y;

    if (g_site_msg[0]) {
        uint32_t fg = g_site_msg_err ? TH_STATUS_ERR : TH_TEXT_DIM;
        draw_text_small(x, y, g_site_msg, fg, COL_BG);
        y += tm->font_small + 2;
    }

    if (site_allow_persistent_available()) {
        draw_text_small(x, y, "Saved in /TLSALLOW.CFG so the list survives reboots.", COL_DIM, COL_BG);
    } else {
        draw_text_small(x, y, "Filesystem unavailable: allowed sites only last for this session.", COL_DIM, COL_BG);
    }
}

static void network_tab_compute_layout(const gui_rect_t *body, int count,
                                       settings_network_layout_t *layout) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t inner = settings_inner_rect(body);
    int top = inner.y + tm->font_small + tm->gap_sm + th_info_strip_height() + tm->gap_md;
    int row_gap = 4;
    int site_min_h = tm->list_row_h + tm->field_h + tm->button_h + tm->font_small * 2 + tm->gap_lg + 10;
    int wifi_h;
    int site_h;
    int list_rows;
    int list_h;
    int pad;
    int col_gap;
    int col_w;
    int left_btn_w;
    int site_inner_x;
    int site_inner_w;
    int site_available_rows_h;

    if (!layout) return;
    mem_set(layout, 0, sizeof(*layout));

    wifi_h = inner.h - (top - inner.y) - site_min_h - tm->gap_lg;
    if (wifi_h < 132) wifi_h = 132;
    if (wifi_h > 192) wifi_h = 192;
    if (top + wifi_h + tm->gap_lg + site_min_h > inner.y + inner.h) {
        wifi_h = inner.y + inner.h - top - tm->gap_lg - site_min_h;
    }
    if (wifi_h < 120) wifi_h = 120;
    site_h = inner.y + inner.h - (top + wifi_h + tm->gap_lg);
    if (site_h < site_min_h) site_h = site_min_h;

    layout->wifi_card_rect.x = inner.x;
    layout->wifi_card_rect.y = top;
    layout->wifi_card_rect.w = inner.w;
    layout->wifi_card_rect.h = wifi_h;
    layout->site_card_rect.x = inner.x;
    layout->site_card_rect.y = top + wifi_h + tm->gap_lg;
    layout->site_card_rect.w = inner.w;
    layout->site_card_rect.h = site_h;

    pad = tm->card_pad;
    col_gap = tm->gap_md;
    col_w = (layout->wifi_card_rect.w - pad * 2 - col_gap) / 2;
    if (col_w < 120) col_w = 120;
    list_rows = (wifi_h >= 172) ? 2 : 1;
    list_h = list_rows * (tm->list_row_h + row_gap) - row_gap;

    layout->wifi_rows_to_draw = list_rows;
    layout->wifi_scan_rect.x = layout->wifi_card_rect.x + pad;
    layout->wifi_scan_rect.y = layout->wifi_card_rect.y + pad + tm->font_small + tm->font_body + tm->gap_sm + 10;
    layout->wifi_scan_rect.w = col_w;
    layout->wifi_scan_rect.h = list_h;
    layout->wifi_saved_rect.x = layout->wifi_scan_rect.x + col_w + col_gap;
    layout->wifi_saved_rect.y = layout->wifi_scan_rect.y;
    layout->wifi_saved_rect.w = col_w;
    layout->wifi_saved_rect.h = list_h;
    layout->wifi_ssid_rect.x = layout->wifi_scan_rect.x;
    layout->wifi_ssid_rect.y = layout->wifi_scan_rect.y + list_h + tm->gap_sm;
    layout->wifi_ssid_rect.w = col_w;
    layout->wifi_ssid_rect.h = tm->field_h;
    layout->wifi_pass_rect.x = layout->wifi_saved_rect.x;
    layout->wifi_pass_rect.y = layout->wifi_ssid_rect.y;
    layout->wifi_pass_rect.w = col_w;
    layout->wifi_pass_rect.h = tm->field_h;

    left_btn_w = (col_w - tm->gap_sm * 2) / 3;
    if (left_btn_w < 48) left_btn_w = 48;
    layout->wifi_security_btn.x = layout->wifi_scan_rect.x;
    layout->wifi_security_btn.y = layout->wifi_ssid_rect.y + tm->field_h + tm->gap_sm;
    layout->wifi_security_btn.w = left_btn_w;
    layout->wifi_security_btn.h = tm->button_h;
    layout->wifi_scan_btn = layout->wifi_security_btn;
    layout->wifi_scan_btn.x += left_btn_w + tm->gap_sm;
    layout->wifi_connect_btn = layout->wifi_scan_btn;
    layout->wifi_connect_btn.x += left_btn_w + tm->gap_sm;
    layout->wifi_save_btn.x = layout->wifi_saved_rect.x;
    layout->wifi_save_btn.y = layout->wifi_security_btn.y;
    layout->wifi_save_btn.w = left_btn_w;
    layout->wifi_save_btn.h = tm->button_h;
    layout->wifi_forget_btn = layout->wifi_save_btn;
    layout->wifi_forget_btn.x += left_btn_w + tm->gap_sm;
    layout->wifi_disconnect_btn = layout->wifi_forget_btn;
    layout->wifi_disconnect_btn.x += left_btn_w + tm->gap_sm;
    layout->wifi_msg_rect.x = layout->wifi_scan_rect.x;
    layout->wifi_msg_rect.y = layout->wifi_security_btn.y + tm->button_h + tm->gap_sm;
    layout->wifi_msg_rect.w = layout->wifi_card_rect.w - pad * 2;
    layout->wifi_msg_rect.h = tm->font_small;

    site_inner_x = layout->site_card_rect.x + pad;
    site_inner_w = layout->site_card_rect.w - pad * 2;
    layout->add_btn.w = 72;
    layout->add_btn.h = tm->button_h;
    layout->remove_btn.w = 72;
    layout->remove_btn.h = tm->button_h;
    layout->site_list_rect.x = site_inner_x;
    layout->site_list_rect.y = layout->site_card_rect.y + pad + tm->font_small + tm->font_small + tm->gap_md + 4;
    layout->site_list_rect.w = site_inner_w;
    site_available_rows_h = layout->site_card_rect.y + layout->site_card_rect.h - layout->site_list_rect.y
                          - tm->field_h - tm->button_h - tm->font_small - tm->gap_md * 2 - tm->gap_sm;
    layout->site_rows_to_draw = site_available_rows_h / (tm->list_row_h + row_gap);
    if (layout->site_rows_to_draw < 1) layout->site_rows_to_draw = 1;
    if (layout->site_rows_to_draw > 3) layout->site_rows_to_draw = 3;
    layout->site_list_rect.h = layout->site_rows_to_draw * (tm->list_row_h + row_gap) - row_gap;
    layout->site_input_rect.x = site_inner_x;
    layout->site_input_rect.y = layout->site_list_rect.y + layout->site_list_rect.h + tm->gap_sm;
    layout->site_input_rect.w = site_inner_w - 156;
    if (layout->site_input_rect.w < 140) layout->site_input_rect.w = site_inner_w;
    layout->site_input_rect.h = tm->field_h;
    layout->add_btn.x = site_inner_x + site_inner_w - 152;
    layout->add_btn.y = layout->site_input_rect.y;
    layout->remove_btn.x = site_inner_x + site_inner_w - 72;
    layout->remove_btn.y = layout->site_input_rect.y;
    layout->footer_rect.x = site_inner_x;
    layout->footer_rect.y = layout->site_input_rect.y + tm->button_h + tm->gap_sm;
    layout->footer_rect.w = site_inner_w;
    layout->footer_rect.h = tm->font_small * 2;

    if (wifi_scan_count() <= layout->wifi_rows_to_draw) {
        layout->wifi_scan_start = 0;
    } else if (g_wifi_scan_sel <= 0) {
        layout->wifi_scan_start = 0;
    } else if (g_wifi_scan_sel >= wifi_scan_count() - layout->wifi_rows_to_draw) {
        layout->wifi_scan_start = wifi_scan_count() - layout->wifi_rows_to_draw;
    } else {
        layout->wifi_scan_start = g_wifi_scan_sel;
    }

    if (wifi_saved_count() <= layout->wifi_rows_to_draw) {
        layout->wifi_saved_start = 0;
    } else if (g_wifi_saved_sel <= 0) {
        layout->wifi_saved_start = 0;
    } else if (g_wifi_saved_sel >= wifi_saved_count() - layout->wifi_rows_to_draw) {
        layout->wifi_saved_start = wifi_saved_count() - layout->wifi_rows_to_draw;
    } else {
        layout->wifi_saved_start = g_wifi_saved_sel;
    }

    if (count <= layout->site_rows_to_draw) {
        layout->site_list_start = 0;
    } else {
        if (g_site_sel < 0) g_site_sel = 0;
        if (g_site_sel >= count) g_site_sel = count - 1;
        if (g_site_sel <= 0) layout->site_list_start = 0;
        else if (g_site_sel >= count - layout->site_rows_to_draw) layout->site_list_start = count - layout->site_rows_to_draw;
        else layout->site_list_start = g_site_sel;
    }
}

static int settings_tab_rect(int index, gui_rect_t *out) {
    const th_metrics_t *tm = th_metrics();
    gui_rect_t r = gui_window_content(g_win_id);
    int gap = tm->gap_sm;
    int tab_w = (r.w - 24 - gap * (TAB_COUNT - 1)) / TAB_COUNT;
    int tab_y = r.y + 8 + th_page_header_height() + tm->gap_sm;

    if (!out || index < 0 || index >= TAB_COUNT) return 0;
    if (tab_w < 72) tab_w = 72;
    out->x = r.x + 12 + index * (tab_w + gap);
    out->y = tab_y;
    out->w = tab_w;
    out->h = tm->tab_h;
    return 1;
}

static void control_panel_on_paint(int win_id) {
    static const char *tabs[TAB_COUNT] = { "Display", "System", "Devices", "Users", "Network" };
    gui_rect_t r = gui_window_content(win_id);
    gui_rect_t body;

    gfx_fill_rect(r.x, r.y, r.w, r.h, COL_BG);
    th_draw_page_header(r.x + 8, r.y + 8, r.w - 16,
                        "Settings",
                        "Control Panel",
                        "Display, system, users, network, and session tools in one place.");
    for (int i = 0; i < TAB_COUNT; i++) {
        gui_rect_t tab;
        settings_tab_rect(i, &tab);
        th_draw_tab(tab.x, tab.y, tab.w, tab.h, tabs[i], i == g_tab);
    }
    body = settings_body_rect();
    th_draw_card(body.x, body.y, body.w, body.h, 0, gfx_rgb(250, 251, 254), 0);

    if (g_tab == TAB_DISPLAY) draw_display_tab(&body);
    else if (g_tab == TAB_SYSTEM) draw_system_tab(&body);
    else if (g_tab == TAB_DEVICES) draw_devices_tab(&body);
    else if (g_tab == TAB_USERS) draw_users_tab(&body);
    else draw_network_tab(&body);
}

static void control_panel_on_key(int win_id, char key) {
    (void)win_id;
    if (key == 0x11) { gui_window_close(g_win_id); return; }
    if (key == KEY_LEFT && g_tab > 0) { g_tab--; return; }
    if (key == KEY_RIGHT && g_tab + 1 < TAB_COUNT) { g_tab++; return; }

    if (g_tab == TAB_USERS) {
        if ((key == '\b' || key == (char)0x7F) && g_create_name_len > 0) {
            g_create_name[--g_create_name_len] = '\0';
            g_create_msg[0] = '\0';
            return;
        }
        if (key == '\r' || key == '\n') {
            if (!users_current_is_admin()) {
                if (!permission_prompt_run("add a new user")) {
                    gui_repaint();
                    return;
                }
                gui_repaint();
            }
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

    if (g_tab == TAB_NETWORK) {
        ensure_network_wifi_ready();
        int scan_count = wifi_scan_count();
        int saved_count = wifi_saved_count();
        int count = site_allow_count();
        if (key == '\t') {
            g_network_focus = (settings_network_focus_t)((g_network_focus + 1) % 5);
            return;
        }
        if (key == 'r' || key == 'R') {
            wifi_action_scan();
            return;
        }
        if (key == 'x' || key == 'X') {
            wifi_action_disconnect();
            return;
        }
        if (g_network_focus == NETWORK_FOCUS_WIFI_SCAN) {
            if (key == KEY_UP && g_wifi_scan_sel > 0) {
                g_wifi_scan_sel--;
                sync_wifi_inputs_from_scan(g_wifi_scan_sel);
                return;
            }
            if (key == KEY_DOWN && g_wifi_scan_sel + 1 < scan_count) {
                g_wifi_scan_sel++;
                sync_wifi_inputs_from_scan(g_wifi_scan_sel);
                return;
            }
            if (key == '\r' || key == '\n') {
                wifi_action_connect();
                return;
            }
        } else if (g_network_focus == NETWORK_FOCUS_WIFI_SAVED) {
            if (key == KEY_UP && g_wifi_saved_sel > 0) {
                g_wifi_saved_sel--;
                sync_wifi_inputs_from_saved(g_wifi_saved_sel);
                return;
            }
            if (key == KEY_DOWN && g_wifi_saved_sel + 1 < saved_count) {
                g_wifi_saved_sel++;
                sync_wifi_inputs_from_saved(g_wifi_saved_sel);
                return;
            }
            if (key == KEY_DELETE) {
                wifi_action_forget();
                return;
            }
            if (key == '\r' || key == '\n') {
                wifi_action_connect();
                return;
            }
        } else if (g_network_focus == NETWORK_FOCUS_SITE) {
            if (key == KEY_UP && g_site_sel > 0) {
                g_site_sel--;
                return;
            }
            if (key == KEY_DOWN && g_site_sel + 1 < count) {
                g_site_sel++;
                return;
            }
            if (key == '\r' || key == '\n') {
                int rc = site_allow_add(g_site_input);
                if (rc == SITE_ALLOW_OK) {
                    str_copy(g_site_msg, "Site added.", sizeof(g_site_msg));
                    g_site_msg_err = 0;
                    g_site_input[0] = '\0';
                    g_site_input_len = 0;
                    g_site_sel = site_allow_count() > 0 ? site_allow_count() - 1 : 0;
                } else if (rc == SITE_ALLOW_ERR_EXISTS) {
                    str_copy(g_site_msg, "Site already allowed.", sizeof(g_site_msg));
                    g_site_msg_err = 1;
                } else if (rc == SITE_ALLOW_ERR_FULL) {
                    str_copy(g_site_msg, "Allowed site list is full.", sizeof(g_site_msg));
                    g_site_msg_err = 1;
                } else if (rc == SITE_ALLOW_ERR_STORE) {
                    str_copy(g_site_msg, "Could not save allowed sites.", sizeof(g_site_msg));
                    g_site_msg_err = 1;
                } else {
                    str_copy(g_site_msg, "Enter a valid host or URL.", sizeof(g_site_msg));
                    g_site_msg_err = 1;
                }
                return;
            }
        }

        if (g_network_focus == NETWORK_FOCUS_WIFI_SSID || g_network_focus == NETWORK_FOCUS_WIFI_PASS) {
            if (key == KEY_LEFT) {
                g_wifi_security_sel = (g_wifi_security_sel + 2) % 3;
                return;
            }
            if (key == KEY_RIGHT) {
                g_wifi_security_sel = (g_wifi_security_sel + 1) % 3;
                return;
            }
            if (key == '\r' || key == '\n') {
                wifi_action_save();
                return;
            }
        }

        if (g_network_focus == NETWORK_FOCUS_WIFI_SSID) {
            if ((key == '\b' || key == (char)0x7F) && g_wifi_ssid_len > 0) {
                g_wifi_ssid_input[--g_wifi_ssid_len] = '\0';
                g_wifi_msg[0] = '\0';
                return;
            }
            if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                key != '|' && g_wifi_ssid_len + 1 < (int)sizeof(g_wifi_ssid_input)) {
                g_wifi_ssid_input[g_wifi_ssid_len++] = key;
                g_wifi_ssid_input[g_wifi_ssid_len] = '\0';
                g_wifi_msg[0] = '\0';
                return;
            }
        } else if (g_network_focus == NETWORK_FOCUS_WIFI_PASS) {
            if ((key == '\b' || key == (char)0x7F) && g_wifi_pass_len > 0) {
                g_wifi_pass_input[--g_wifi_pass_len] = '\0';
                g_wifi_msg[0] = '\0';
                return;
            }
            if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                key != '|' && g_wifi_pass_len + 1 < (int)sizeof(g_wifi_pass_input)) {
                g_wifi_pass_input[g_wifi_pass_len++] = key;
                g_wifi_pass_input[g_wifi_pass_len] = '\0';
                g_wifi_msg[0] = '\0';
                return;
            }
        } else if (g_network_focus == NETWORK_FOCUS_SITE) {
            if ((key == '\b' || key == (char)0x7F) && g_site_input_len > 0) {
                g_site_input[--g_site_input_len] = '\0';
                g_site_msg[0] = '\0';
                g_site_msg_err = 0;
                return;
            }
            if ((unsigned char)key >= 32 && (unsigned char)key <= 126 &&
                g_site_input_len + 1 < (int)sizeof(g_site_input)) {
                char ch = key;
                if ((ch >= 'A' && ch <= 'Z') ||
                    (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') ||
                    ch == '.' || ch == '-' || ch == ':' || ch == '/' || ch == '@' ||
                    ch == '?' || ch == '#') {
                    g_site_input[g_site_input_len++] = ch;
                    g_site_input[g_site_input_len] = '\0';
                    g_site_msg[0] = '\0';
                    g_site_msg_err = 0;
                }
            }
        }
    }
}

static void control_panel_on_mouse(int win_id, int x, int y, uint8_t buttons) {
    gui_rect_t r;
    const th_metrics_t *tm = th_metrics();
    gui_rect_t body;
    (void)win_id;
    if (!(buttons & 1)) return;
    r = gui_window_content(g_win_id);
    body = settings_body_rect();

    /* Tab row */
    {
        for (int i = 0; i < TAB_COUNT; i++) {
            gui_rect_t tab;
            if (!settings_tab_rect(i, &tab)) continue;
            if (x >= tab.x - r.x && x < tab.x - r.x + tab.w &&
                y >= tab.y - r.y && y < tab.y - r.y + tab.h) {
                g_tab = i;
                return;
            }
        }
    }

    /* Display tab: theme card clicks */
    if (g_tab == TAB_DISPLAY) {
        for (int i = 0; i < gui_background_theme_count(); i++) {
            gui_rect_t card;
            if (!display_theme_card_rect(&body, i, &card)) continue;
            if (x >= card.x - r.x && x < card.x - r.x + card.w &&
                y >= card.y - r.y && y < card.y - r.y + card.h) {
                gui_set_background_theme((gui_background_theme_t)i);
                return;
            }
        }
    }

    if (g_tab == TAB_USERS) {
        gui_rect_t inner = settings_inner_rect(&body);
        int count = users_count();
        int local_y = (inner.y - r.y) + tm->font_small + tm->gap_sm +
                      count * (tm->list_row_h + 4) +
                      ((count == 0) ? (tm->font_small + tm->gap_md) : 0) +
                      tm->gap_sm + tm->gap_sm + tm->font_small + tm->gap_sm;
        int fw = inner.w;
        int btn_x = (inner.x - r.x) + fw - 88;
        int btn_y = local_y;
        int fh = tm->button_h;
        if (x >= btn_x && x < btn_x + 80 && y >= btn_y && y < btn_y + fh) {
            if (!users_current_is_admin()) {
                if (!permission_prompt_run("add a new user")) {
                    gui_repaint();
                    return;
                }
                gui_repaint();
            }
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

    if (g_tab == TAB_NETWORK) {
        settings_network_layout_t layout;
        ensure_network_wifi_ready();
        int scan_count = wifi_scan_count();
        int saved_count = wifi_saved_count();
        int count = site_allow_count();
        network_tab_compute_layout(&body, count, &layout);

        for (int i = 0; i < layout.wifi_rows_to_draw; i++) {
            int row_y = layout.wifi_scan_rect.y - r.y + i * (tm->list_row_h + 4);
            if (x >= layout.wifi_scan_rect.x - r.x && x < layout.wifi_scan_rect.x - r.x + layout.wifi_scan_rect.w &&
                y >= row_y && y < row_y + tm->list_row_h) {
                if (layout.wifi_scan_start + i < scan_count) {
                    g_network_focus = NETWORK_FOCUS_WIFI_SCAN;
                    g_wifi_scan_sel = layout.wifi_scan_start + i;
                    sync_wifi_inputs_from_scan(g_wifi_scan_sel);
                }
                return;
            }
        }

        for (int i = 0; i < layout.wifi_rows_to_draw; i++) {
            int row_y = layout.wifi_saved_rect.y - r.y + i * (tm->list_row_h + 4);
            if (x >= layout.wifi_saved_rect.x - r.x && x < layout.wifi_saved_rect.x - r.x + layout.wifi_saved_rect.w &&
                y >= row_y && y < row_y + tm->list_row_h) {
                if (layout.wifi_saved_start + i < saved_count) {
                    g_network_focus = NETWORK_FOCUS_WIFI_SAVED;
                    g_wifi_saved_sel = layout.wifi_saved_start + i;
                    sync_wifi_inputs_from_saved(g_wifi_saved_sel);
                }
                return;
            }
        }

        if (x >= layout.wifi_ssid_rect.x - r.x && x < layout.wifi_ssid_rect.x - r.x + layout.wifi_ssid_rect.w &&
            y >= layout.wifi_ssid_rect.y - r.y && y < layout.wifi_ssid_rect.y - r.y + layout.wifi_ssid_rect.h) {
            g_network_focus = NETWORK_FOCUS_WIFI_SSID;
            return;
        }
        if (x >= layout.wifi_pass_rect.x - r.x && x < layout.wifi_pass_rect.x - r.x + layout.wifi_pass_rect.w &&
            y >= layout.wifi_pass_rect.y - r.y && y < layout.wifi_pass_rect.y - r.y + layout.wifi_pass_rect.h) {
            g_network_focus = NETWORK_FOCUS_WIFI_PASS;
            return;
        }

        if (x >= layout.wifi_security_btn.x - r.x && x < layout.wifi_security_btn.x - r.x + layout.wifi_security_btn.w &&
            y >= layout.wifi_security_btn.y - r.y && y < layout.wifi_security_btn.y - r.y + layout.wifi_security_btn.h) {
            g_wifi_security_sel = (g_wifi_security_sel + 1) % 3;
            g_network_focus = NETWORK_FOCUS_WIFI_SSID;
            return;
        }
        if (x >= layout.wifi_scan_btn.x - r.x && x < layout.wifi_scan_btn.x - r.x + layout.wifi_scan_btn.w &&
            y >= layout.wifi_scan_btn.y - r.y && y < layout.wifi_scan_btn.y - r.y + layout.wifi_scan_btn.h) {
            wifi_action_scan();
            return;
        }
        if (x >= layout.wifi_connect_btn.x - r.x && x < layout.wifi_connect_btn.x - r.x + layout.wifi_connect_btn.w &&
            y >= layout.wifi_connect_btn.y - r.y && y < layout.wifi_connect_btn.y - r.y + layout.wifi_connect_btn.h) {
            wifi_action_connect();
            return;
        }
        if (x >= layout.wifi_save_btn.x - r.x && x < layout.wifi_save_btn.x - r.x + layout.wifi_save_btn.w &&
            y >= layout.wifi_save_btn.y - r.y && y < layout.wifi_save_btn.y - r.y + layout.wifi_save_btn.h) {
            wifi_action_save();
            return;
        }
        if (x >= layout.wifi_forget_btn.x - r.x && x < layout.wifi_forget_btn.x - r.x + layout.wifi_forget_btn.w &&
            y >= layout.wifi_forget_btn.y - r.y && y < layout.wifi_forget_btn.y - r.y + layout.wifi_forget_btn.h) {
            wifi_action_forget();
            return;
        }
        if (x >= layout.wifi_disconnect_btn.x - r.x && x < layout.wifi_disconnect_btn.x - r.x + layout.wifi_disconnect_btn.w &&
            y >= layout.wifi_disconnect_btn.y - r.y && y < layout.wifi_disconnect_btn.y - r.y + layout.wifi_disconnect_btn.h) {
            wifi_action_disconnect();
            return;
        }

        for (int i = 0; i < layout.site_rows_to_draw; i++) {
            int row_y = layout.site_list_rect.y - r.y + i * (tm->list_row_h + 4);
            if (x >= layout.site_list_rect.x - r.x && x < layout.site_list_rect.x - r.x + layout.site_list_rect.w &&
                y >= row_y && y < row_y + tm->list_row_h) {
                if (layout.site_list_start + i < count) {
                    g_network_focus = NETWORK_FOCUS_SITE;
                    g_site_sel = layout.site_list_start + i;
                }
                return;
            }
        }

        if (x >= layout.site_input_rect.x - r.x && x < layout.site_input_rect.x - r.x + layout.site_input_rect.w &&
            y >= layout.site_input_rect.y - r.y && y < layout.site_input_rect.y - r.y + layout.site_input_rect.h) {
            g_network_focus = NETWORK_FOCUS_SITE;
            return;
        }

        if (x >= layout.add_btn.x - r.x && x < layout.add_btn.x - r.x + layout.add_btn.w &&
            y >= layout.add_btn.y - r.y && y < layout.add_btn.y - r.y + layout.add_btn.h) {
            int rc = site_allow_add(g_site_input);
            if (rc == SITE_ALLOW_OK) {
                str_copy(g_site_msg, "Site added.", sizeof(g_site_msg));
                g_site_msg_err = 0;
                g_site_input[0] = '\0';
                g_site_input_len = 0;
                g_site_sel = site_allow_count() > 0 ? site_allow_count() - 1 : 0;
            } else if (rc == SITE_ALLOW_ERR_EXISTS) {
                str_copy(g_site_msg, "Site already allowed.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            } else if (rc == SITE_ALLOW_ERR_FULL) {
                str_copy(g_site_msg, "Allowed site list is full.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            } else if (rc == SITE_ALLOW_ERR_STORE) {
                str_copy(g_site_msg, "Could not save allowed sites.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            } else {
                str_copy(g_site_msg, "Enter a valid host or URL.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            }
            return;
        }
        if (x >= layout.remove_btn.x - r.x && x < layout.remove_btn.x - r.x + layout.remove_btn.w &&
            y >= layout.remove_btn.y - r.y && y < layout.remove_btn.y - r.y + layout.remove_btn.h) {
            const char *host = site_allow_host_at(g_site_sel);
            int rc;
            if (!host) {
                str_copy(g_site_msg, "Select a site to remove.", sizeof(g_site_msg));
                g_site_msg_err = 1;
                return;
            }
            rc = site_allow_remove(host);
            if (rc == SITE_ALLOW_OK) {
                str_copy(g_site_msg, "Site removed.", sizeof(g_site_msg));
                g_site_msg_err = 0;
                if (g_site_sel >= site_allow_count() && g_site_sel > 0) g_site_sel--;
            } else if (rc == SITE_ALLOW_ERR_STORE) {
                str_copy(g_site_msg, "Could not save allowed sites.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            } else {
                str_copy(g_site_msg, "Could not remove that site.", sizeof(g_site_msg));
                g_site_msg_err = 1;
            }
            return;
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
    gui_window_set_min_size(g_win_id, 560, 360);
    g_tab = 0;
    g_create_name[0] = '\0'; g_create_name_len = 0;
    g_create_msg[0] = '\0'; g_create_msg_err = 0;
    g_wifi_ssid_input[0] = '\0'; g_wifi_ssid_len = 0;
    g_wifi_pass_input[0] = '\0'; g_wifi_pass_len = 0;
    g_wifi_msg[0] = '\0'; g_wifi_msg_err = 0;
    g_wifi_scan_sel = 0;
    g_wifi_saved_sel = 0;
    g_wifi_security_sel = WIFI_SECURITY_WPA2_PSK;
    g_network_focus = NETWORK_FOCUS_WIFI_SSID;
    g_site_input[0] = '\0'; g_site_input_len = 0;
    g_site_msg[0] = '\0'; g_site_msg_err = 0;
    g_site_sel = 0;
    site_allow_init();
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
