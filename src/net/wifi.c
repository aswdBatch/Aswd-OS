#include "net/wifi.h"

#include <stdint.h>

#include "cpu/pic.h"
#include "cpu/timer.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "usb/usb.h"

#define WIFI_FILE "WIFI.CFG"
#define WIFI_FILE_BUF_SIZE (WIFI_SAVED_MAX * (WIFI_SSID_MAX + WIFI_PASSPHRASE_MAX + 16))

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    wifi_family_t family;
    const char *name;
} wifi_pci_id_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    wifi_family_t family;
    const char *name;
} wifi_usb_id_t;

static const wifi_pci_id_t k_wifi_pci_ids[] = {
    { 0x8086u, 0x4220u, WIFI_FAMILY_INTEL_2200, "Intel PRO/Wireless 2200BG" },
    { 0x8086u, 0x4221u, WIFI_FAMILY_INTEL_2200, "Intel PRO/Wireless 2915ABG" },
    { 0x8086u, 0x4223u, WIFI_FAMILY_INTEL_2200, "Intel PRO/Wireless 2915ABG" },
    { 0x8086u, 0x4224u, WIFI_FAMILY_INTEL_2200, "Intel PRO/Wireless 2200BG" },
    { 0x8086u, 0x4222u, WIFI_FAMILY_INTEL_3945, "Intel PRO/Wireless 3945ABG" },
    { 0x8086u, 0x4227u, WIFI_FAMILY_INTEL_3945, "Intel PRO/Wireless 3945ABG" },
    { 0x168Cu, 0x0013u, WIFI_FAMILY_ATHEROS_AR5XXX, "Atheros AR5xxx Wi-Fi" },
    { 0x168Cu, 0x001Au, WIFI_FAMILY_ATHEROS_AR5XXX, "Atheros AR5xxx Wi-Fi" },
    { 0x168Cu, 0x001Bu, WIFI_FAMILY_ATHEROS_AR5XXX, "Atheros AR5xxx Wi-Fi" },
    { 0x168Cu, 0x001Cu, WIFI_FAMILY_ATHEROS_AR5XXX, "Atheros AR5xxx Wi-Fi" },
    { 0x168Cu, 0x0023u, WIFI_FAMILY_ATHEROS_AR5XXX, "Atheros AR5xxx Wi-Fi" },
    { 0x14E4u, 0x4311u, WIFI_FAMILY_BROADCOM_BCM43XX, "Broadcom BCM43xx Wi-Fi" },
    { 0x14E4u, 0x4312u, WIFI_FAMILY_BROADCOM_BCM43XX, "Broadcom BCM43xx Wi-Fi" },
    { 0x14E4u, 0x4318u, WIFI_FAMILY_BROADCOM_BCM43XX, "Broadcom BCM43xx Wi-Fi" },
    { 0x14E4u, 0x4320u, WIFI_FAMILY_BROADCOM_BCM43XX, "Broadcom BCM43xx Wi-Fi" },
    { 0x14E4u, 0x4324u, WIFI_FAMILY_BROADCOM_BCM43XX, "Broadcom BCM43xx Wi-Fi" },
};

static const wifi_usb_id_t k_wifi_usb_ids[] = {
    { 0x0BDAu, 0x8187u, WIFI_FAMILY_REALTEK_RTL8187, "Realtek RTL8187 USB Wi-Fi" },
    { 0x0846u, 0x4260u, WIFI_FAMILY_REALTEK_RTL8187, "Realtek RTL8187 USB Wi-Fi" },
    { 0x050Du, 0x705Eu, WIFI_FAMILY_REALTEK_RTL8187, "Realtek RTL8187 USB Wi-Fi" },
};

static wifi_adapter_info_t g_adapter;
static wifi_network_t g_scans[WIFI_SCAN_MAX];
static wifi_saved_network_t g_saved[WIFI_SAVED_MAX];
static int g_scan_count = 0;
static int g_saved_count = 0;
static int g_loaded = 0;
static wifi_state_t g_state = WIFI_STATE_DOWN;
static char g_status_note[WIFI_STATUS_MSG_MAX];
static char g_current_ssid[WIFI_SSID_MAX + 1];
static wifi_security_t g_current_security = WIFI_SECURITY_OPEN;
static uint8_t g_current_signal = 0;
static uint32_t g_last_probe_tick = 0;
static int g_last_usb_device_count = 0;
static int g_initialized = 0;

static void wifi_note(const char *note) {
    str_copy(g_status_note, note ? note : "", sizeof(g_status_note));
}

static int wifi_restore_path(const char *path) {
    char segment[13];
    int seg_len = 0;

    if (!vfs_available()) return 0;
    if (!path || path[0] != '/') return vfs_cd("/");
    if (!vfs_cd("/")) return 0;
    if (path[1] == '\0') return 1;

    for (int i = 1; ; i++) {
        char ch = path[i];
        if (ch == '/' || ch == '\0') {
            if (seg_len > 0) {
                segment[seg_len] = '\0';
                if (!vfs_cd(segment)) return 0;
                seg_len = 0;
            }
            if (ch == '\0') break;
        } else if (seg_len + 1 < (int)sizeof(segment)) {
            segment[seg_len++] = ch;
        }
    }
    return 1;
}

static const char *wifi_security_token(wifi_security_t security) {
    switch (security) {
        case WIFI_SECURITY_OPEN: return "OPEN";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA2_PSK: return "WPA2";
        default: return "";
    }
}

static int wifi_security_from_token(const char *token, wifi_security_t *out) {
    if (str_eq(token, "OPEN")) {
        *out = WIFI_SECURITY_OPEN;
        return 1;
    }
    if (str_eq(token, "WEP")) {
        *out = WIFI_SECURITY_WEP;
        return 1;
    }
    if (str_eq(token, "WPA2")) {
        *out = WIFI_SECURITY_WPA2_PSK;
        return 1;
    }
    return 0;
}

static int wifi_valid_text(const char *text, int max_len, int allow_empty) {
    int len = 0;
    if (!text) return allow_empty;
    while (text[len]) {
        unsigned char ch = (unsigned char)text[len];
        if (ch < 32 || ch > 126 || ch == '|') return 0;
        len++;
        if (len > max_len) return 0;
    }
    return allow_empty || len > 0;
}

static int wifi_validate_credentials(const char *ssid, wifi_security_t security,
                                     const char *passphrase) {
    int pass_len = (int)str_len(passphrase ? passphrase : "");

    if (!wifi_valid_text(ssid, WIFI_SSID_MAX, 0)) return WIFI_ERR_INVALID;
    if (security == WIFI_SECURITY_OPEN) {
        if (pass_len > 0 && !wifi_valid_text(passphrase, WIFI_PASSPHRASE_MAX, 1)) {
            return WIFI_ERR_INVALID;
        }
        return WIFI_OK;
    }
    if (!wifi_valid_text(passphrase, WIFI_PASSPHRASE_MAX, 0)) return WIFI_ERR_INVALID;
    if (security == WIFI_SECURITY_WEP) {
        if (!(pass_len == 5 || pass_len == 10 || pass_len == 13 || pass_len == 26)) {
            return WIFI_ERR_SECURITY;
        }
        return WIFI_OK;
    }
    if (security == WIFI_SECURITY_WPA2_PSK) {
        if (pass_len < 8 || pass_len > 63) return WIFI_ERR_SECURITY;
        return WIFI_OK;
    }
    return WIFI_ERR_SECURITY;
}

static int wifi_saved_find(const char *ssid) {
    for (int i = 0; i < g_saved_count; i++) {
        if (str_eq(g_saved[i].ssid, ssid)) return i;
    }
    return -1;
}

static int wifi_split_saved_line(char *line, char **ssid, char **security, char **passphrase) {
    char *p = line;

    if (!line || !ssid || !security || !passphrase) return 0;
    *ssid = p;
    while (*p && *p != '|') p++;
    if (!*p) return 0;
    *p++ = '\0';
    *security = p;
    while (*p && *p != '|') p++;
    if (!*p) return 0;
    *p++ = '\0';
    *passphrase = p;
    return 1;
}

static int wifi_save_store(void) {
    char saved_path[256];
    char buf[WIFI_FILE_BUF_SIZE];
    int pos = 0;

    if (!vfs_available()) return 1;

    buf[0] = '\0';
    for (int i = 0; i < g_saved_count; i++) {
        const char *token = wifi_security_token(g_saved[i].security);
        int ssid_len = (int)str_len(g_saved[i].ssid);
        int token_len = (int)str_len(token);
        int pass_len = (int)str_len(g_saved[i].passphrase);

        if (pos + ssid_len + token_len + pass_len + 4 >= (int)sizeof(buf)) break;
        mem_copy(buf + pos, g_saved[i].ssid, (uint32_t)ssid_len); pos += ssid_len;
        buf[pos++] = '|';
        mem_copy(buf + pos, token, (uint32_t)token_len); pos += token_len;
        buf[pos++] = '|';
        mem_copy(buf + pos, g_saved[i].passphrase, (uint32_t)pass_len); pos += pass_len;
        buf[pos++] = '\n';
    }

    str_copy(saved_path, vfs_cwd_path(), sizeof(saved_path));
    if (!vfs_cd("/")) return 0;
    if (g_saved_count == 0) {
        (void)vfs_rm(WIFI_FILE);
        wifi_restore_path(saved_path);
        pic_clear_mask(1);
        return 1;
    }
    if (vfs_write(WIFI_FILE, (const uint8_t *)buf, (uint32_t)pos) <= 0) {
        wifi_restore_path(saved_path);
        pic_clear_mask(1);
        return 0;
    }
    wifi_restore_path(saved_path);
    pic_clear_mask(1);
    return 1;
}

static void wifi_load_saved(void) {
    char saved_path[256];
    uint8_t buf[WIFI_FILE_BUF_SIZE];
    int read;
    int start = 0;

    if (g_loaded) return;
    g_loaded = 1;
    g_saved_count = 0;

    if (!vfs_available()) return;
    str_copy(saved_path, vfs_cwd_path(), sizeof(saved_path));
    if (!vfs_cd("/")) return;
    read = vfs_cat(WIFI_FILE, buf, (int)sizeof(buf) - 1);
    wifi_restore_path(saved_path);
    pic_clear_mask(1);
    if (read <= 0) return;

    buf[read] = '\0';
    for (int i = 0; i <= read && g_saved_count < WIFI_SAVED_MAX; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') {
            if (i > start) {
                char line[WIFI_SSID_MAX + WIFI_PASSPHRASE_MAX + 16];
                char *ssid = 0;
                char *security_token = 0;
                char *passphrase = 0;
                wifi_security_t security;
                int len = i - start;

                if (len >= (int)sizeof(line)) len = (int)sizeof(line) - 1;
                mem_copy(line, buf + start, (uint32_t)len);
                line[len] = '\0';

                if (wifi_split_saved_line(line, &ssid, &security_token, &passphrase) &&
                    wifi_security_from_token(security_token, &security) &&
                    wifi_validate_credentials(ssid, security, passphrase) == WIFI_OK &&
                    wifi_saved_find(ssid) < 0) {
                    str_copy(g_saved[g_saved_count].ssid, ssid, sizeof(g_saved[g_saved_count].ssid));
                    g_saved[g_saved_count].security = security;
                    str_copy(g_saved[g_saved_count].passphrase, passphrase,
                             sizeof(g_saved[g_saved_count].passphrase));
                    g_saved_count++;
                }
            }
            while (i + 1 <= read && (buf[i + 1] == '\n' || buf[i + 1] == '\r')) i++;
            start = i + 1;
        }
    }
}

static void wifi_clear_runtime(void) {
    g_scan_count = 0;
    g_current_ssid[0] = '\0';
    g_current_security = WIFI_SECURITY_OPEN;
    g_current_signal = 0;
}

static void wifi_fill_adapter(wifi_family_t family, const char *name, uint16_t vendor_id,
                              uint16_t device_id, int via_usb, int backend_ready,
                              int transport_limited, const char *note) {
    g_adapter.present = 1;
    g_adapter.supported = 1;
    g_adapter.backend_ready = backend_ready;
    g_adapter.via_usb = via_usb;
    g_adapter.transport_limited = transport_limited;
    g_adapter.family = family;
    g_adapter.vendor_id = vendor_id;
    g_adapter.device_id = device_id;
    str_copy(g_adapter.name, name, sizeof(g_adapter.name));
    str_copy(g_adapter.note, note, sizeof(g_adapter.note));
}

static int wifi_probe_pci_visit(const pci_device_t *dev, void *ctx) {
    (void)ctx;
    if (!dev) return 0;

    for (int i = 0; i < (int)(sizeof(k_wifi_pci_ids) / sizeof(k_wifi_pci_ids[0])); i++) {
        if (dev->vendor_id == k_wifi_pci_ids[i].vendor_id &&
            dev->device_id == k_wifi_pci_ids[i].device_id) {
            wifi_fill_adapter(k_wifi_pci_ids[i].family, k_wifi_pci_ids[i].name,
                              dev->vendor_id, dev->device_id, 0, 0, 0,
                              "Adapter detected. Association and packet drivers are the next step.");
            return 1;
        }
    }

    if (dev->class_code == 0x02u && dev->subclass == 0x80u && !g_adapter.present) {
        g_adapter.present = 1;
        g_adapter.supported = 0;
        g_adapter.backend_ready = 0;
        g_adapter.family = WIFI_FAMILY_UNKNOWN;
        g_adapter.vendor_id = dev->vendor_id;
        g_adapter.device_id = dev->device_id;
        str_copy(g_adapter.name, "Unsupported wireless adapter", sizeof(g_adapter.name));
        str_copy(g_adapter.note,
                 "A wireless controller was found, but it does not match the current first-wave driver families.",
                 sizeof(g_adapter.note));
    }
    return 0;
}

static void wifi_probe_usb(void) {
    for (int i = 0; i < usb_device_count(); i++) {
        const usb_device_t *dev = usb_device_at(i);
        if (!dev) continue;
        for (int j = 0; j < (int)(sizeof(k_wifi_usb_ids) / sizeof(k_wifi_usb_ids[0])); j++) {
            if (dev->vendor_id == k_wifi_usb_ids[j].vendor_id &&
                dev->product_id == k_wifi_usb_ids[j].product_id) {
                wifi_fill_adapter(k_wifi_usb_ids[j].family, k_wifi_usb_ids[j].name,
                                  dev->vendor_id, dev->product_id, 1,
                                  dev->supports_control && dev->supports_bulk,
                                  !dev->supports_bulk,
                                  dev->supports_bulk
                                      ? "USB adapter detected. Radio work can build on the available transport path."
                                      : "USB adapter detected, but the current USB stack still lacks the bulk transfers this radio needs.");
                return;
            }
        }
    }
}

static void wifi_probe_hardware(void) {
    mem_set(&g_adapter, 0, sizeof(g_adapter));
    g_adapter.name[0] = '\0';
    g_adapter.note[0] = '\0';

    pci_enumerate(wifi_probe_pci_visit, 0);
    if (!g_adapter.supported) {
        wifi_probe_usb();
    }

    if (!g_adapter.present) {
        g_state = WIFI_STATE_DOWN;
        wifi_note("No supported Wi-Fi adapter detected.");
        return;
    }

    if (!g_adapter.supported) {
        g_state = WIFI_STATE_UNSUPPORTED;
        wifi_note(g_adapter.note);
        serial_write("[wifi] unsupported wireless adapter detected\n");
        return;
    }

    g_state = g_adapter.backend_ready ? WIFI_STATE_IDLE : WIFI_STATE_UNSUPPORTED;
    wifi_note(g_adapter.note);
    serial_write("[wifi] adapter detected: ");
    serial_write(g_adapter.name);
    serial_write("\n");
}

void wifi_init(void) {
    if (g_initialized) return;
    g_initialized = 1;
    wifi_load_saved();
    wifi_clear_runtime();
    g_status_note[0] = '\0';
    g_last_probe_tick = timer_get_ticks();
    g_last_usb_device_count = usb_device_count();
    wifi_probe_hardware();
}

int wifi_is_initialized(void) {
    return g_initialized;
}

void wifi_poll(void) {
    if (!g_initialized) return;
    uint32_t now = timer_get_ticks();
    int usb_count = usb_device_count();

    if (!g_adapter.present || usb_count != g_last_usb_device_count ||
        now - g_last_probe_tick >= 500u) {
        g_last_probe_tick = now;
        g_last_usb_device_count = usb_count;
        wifi_probe_hardware();
    }
}

const wifi_adapter_info_t *wifi_adapter_info(void) {
    return &g_adapter;
}

int wifi_scan_count(void) {
    return g_scan_count;
}

const wifi_network_t *wifi_scan_at(int index) {
    if (index < 0 || index >= g_scan_count) return 0;
    return &g_scans[index];
}

int wifi_saved_count(void) {
    wifi_load_saved();
    return g_saved_count;
}

const wifi_saved_network_t *wifi_saved_at(int index) {
    wifi_load_saved();
    if (index < 0 || index >= g_saved_count) return 0;
    return &g_saved[index];
}

int wifi_scan_request(void) {
    g_scan_count = 0;
    if (!g_adapter.present) {
        wifi_note("No Wi-Fi adapter detected.");
        return WIFI_ERR_NO_ADAPTER;
    }
    if (!g_adapter.supported) {
        wifi_note(g_adapter.note);
        return WIFI_ERR_UNSUPPORTED;
    }
    if (!g_adapter.backend_ready) {
        wifi_note(g_adapter.note);
        return WIFI_ERR_SCAN;
    }
    g_state = WIFI_STATE_SCANNING;
    wifi_note("Scan requested.");
    g_state = WIFI_STATE_IDLE;
    return WIFI_OK;
}

int wifi_save_network(const char *ssid, wifi_security_t security, const char *passphrase) {
    int idx;
    int rc = wifi_validate_credentials(ssid, security, passphrase ? passphrase : "");
    if (rc != WIFI_OK) return rc;

    wifi_load_saved();
    idx = wifi_saved_find(ssid);
    if (idx < 0) {
        if (g_saved_count >= WIFI_SAVED_MAX) return WIFI_ERR_FULL;
        idx = g_saved_count++;
    }
    str_copy(g_saved[idx].ssid, ssid, sizeof(g_saved[idx].ssid));
    g_saved[idx].security = security;
    str_copy(g_saved[idx].passphrase, passphrase ? passphrase : "", sizeof(g_saved[idx].passphrase));
    if (!wifi_save_store()) return WIFI_ERR_STORE;
    wifi_note("Saved network profile updated.");
    return WIFI_OK;
}

static int wifi_prepare_connect(const char *ssid, wifi_security_t security, const char *passphrase) {
    int rc = wifi_validate_credentials(ssid, security, passphrase ? passphrase : "");
    if (rc != WIFI_OK) return rc;
    if (!g_adapter.present) return WIFI_ERR_NO_ADAPTER;
    if (!g_adapter.supported) return WIFI_ERR_UNSUPPORTED;
    if (!g_adapter.backend_ready) {
        g_state = WIFI_STATE_UNSUPPORTED;
        wifi_note(g_adapter.note);
        return WIFI_ERR_BACKEND;
    }
    str_copy(g_current_ssid, ssid, sizeof(g_current_ssid));
    g_current_security = security;
    g_current_signal = 0;
    g_state = WIFI_STATE_CONNECTING;
    wifi_note("Connection attempt started.");
    return WIFI_OK;
}

int wifi_connect_manual(const char *ssid, wifi_security_t security, const char *passphrase) {
    return wifi_prepare_connect(ssid, security, passphrase);
}

int wifi_connect_scan(int index, const char *passphrase) {
    const wifi_network_t *scan = wifi_scan_at(index);
    if (!scan) return WIFI_ERR_NOT_FOUND;
    if (!scan->connectable) return WIFI_ERR_SCAN;
    return wifi_prepare_connect(scan->ssid, scan->security, passphrase);
}

int wifi_connect_saved(int index) {
    const wifi_saved_network_t *saved = wifi_saved_at(index);
    if (!saved) return WIFI_ERR_NOT_FOUND;
    return wifi_prepare_connect(saved->ssid, saved->security, saved->passphrase);
}

int wifi_forget_saved(int index) {
    wifi_load_saved();
    if (index < 0 || index >= g_saved_count) return WIFI_ERR_NOT_FOUND;
    for (int i = index; i + 1 < g_saved_count; i++) {
        g_saved[i] = g_saved[i + 1];
    }
    g_saved_count--;
    g_saved[g_saved_count].ssid[0] = '\0';
    g_saved[g_saved_count].passphrase[0] = '\0';
    if (!wifi_save_store()) return WIFI_ERR_STORE;
    wifi_note("Saved network removed.");
    return WIFI_OK;
}

void wifi_disconnect(void) {
    g_current_ssid[0] = '\0';
    g_current_security = WIFI_SECURITY_OPEN;
    g_current_signal = 0;
    g_state = g_adapter.present
        ? (g_adapter.backend_ready ? WIFI_STATE_IDLE : WIFI_STATE_UNSUPPORTED)
        : WIFI_STATE_DOWN;
    wifi_note(g_adapter.present ? g_adapter.note : "No Wi-Fi adapter detected.");
}

wifi_state_t wifi_connection_state(void) {
    return g_state;
}

const char *wifi_connection_note(void) {
    return g_status_note;
}

const char *wifi_current_ssid(void) {
    return g_current_ssid;
}

wifi_security_t wifi_current_security(void) {
    return g_current_security;
}

uint8_t wifi_signal_strength(void) {
    return g_current_signal;
}

const char *wifi_family_name(wifi_family_t family) {
    switch (family) {
        case WIFI_FAMILY_INTEL_2200: return "Intel 2200/2915";
        case WIFI_FAMILY_INTEL_3945: return "Intel 3945";
        case WIFI_FAMILY_ATHEROS_AR5XXX: return "Atheros AR5xxx";
        case WIFI_FAMILY_BROADCOM_BCM43XX: return "Broadcom BCM43xx";
        case WIFI_FAMILY_REALTEK_RTL8187: return "Realtek RTL8187";
        case WIFI_FAMILY_UNKNOWN: return "Unknown wireless";
        default: return "None";
    }
}

const char *wifi_security_name(wifi_security_t security) {
    switch (security) {
        case WIFI_SECURITY_OPEN: return "Open";
        case WIFI_SECURITY_WEP: return "WEP";
        case WIFI_SECURITY_WPA2_PSK: return "WPA2-PSK";
        default: return "Unknown";
    }
}

const char *wifi_result_string(int rc) {
    switch (rc) {
        case WIFI_OK: return "Done.";
        case WIFI_ERR_NO_ADAPTER: return "No Wi-Fi adapter detected.";
        case WIFI_ERR_UNSUPPORTED: return "Adapter found, but this family is not supported yet.";
        case WIFI_ERR_INVALID: return "Enter a valid SSID and passphrase.";
        case WIFI_ERR_SECURITY: return "That passphrase does not match the selected security mode.";
        case WIFI_ERR_BACKEND: return "Adapter found, but its association backend is not implemented yet.";
        case WIFI_ERR_SCAN: return "Live scan is unavailable for the current adapter or USB transport.";
        case WIFI_ERR_NOT_FOUND: return "Select a saved or scanned network first.";
        case WIFI_ERR_STORE: return "Could not save the Wi-Fi profile.";
        case WIFI_ERR_FULL: return "Saved Wi-Fi list is full.";
        default: return "Wi-Fi action failed.";
    }
}
