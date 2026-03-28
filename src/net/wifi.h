#pragma once

#include <stdint.h>

#define WIFI_ADAPTER_NAME_MAX 24
#define WIFI_SSID_MAX         32
#define WIFI_PASSPHRASE_MAX   64
#define WIFI_SCAN_MAX         8
#define WIFI_SAVED_MAX        8
#define WIFI_STATUS_MSG_MAX   96

typedef enum {
    WIFI_FAMILY_NONE = 0,
    WIFI_FAMILY_INTEL_2200,
    WIFI_FAMILY_INTEL_3945,
    WIFI_FAMILY_ATHEROS_AR5XXX,
    WIFI_FAMILY_BROADCOM_BCM43XX,
    WIFI_FAMILY_REALTEK_RTL8187,
    WIFI_FAMILY_UNKNOWN,
} wifi_family_t;

typedef enum {
    WIFI_SECURITY_OPEN = 0,
    WIFI_SECURITY_WEP,
    WIFI_SECURITY_WPA2_PSK,
} wifi_security_t;

typedef enum {
    WIFI_STATE_DOWN = 0,
    WIFI_STATE_IDLE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_UNSUPPORTED,
} wifi_state_t;

typedef struct {
    int present;
    int supported;
    int backend_ready;
    int via_usb;
    int transport_limited;
    wifi_family_t family;
    char name[WIFI_ADAPTER_NAME_MAX];
    char note[WIFI_STATUS_MSG_MAX];
    uint16_t vendor_id;
    uint16_t device_id;
} wifi_adapter_info_t;

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    wifi_security_t security;
    uint8_t signal_pct;
    int connectable;
} wifi_network_t;

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];
    wifi_security_t security;
    char passphrase[WIFI_PASSPHRASE_MAX + 1];
} wifi_saved_network_t;

enum {
    WIFI_OK = 0,
    WIFI_ERR_NO_ADAPTER = -1,
    WIFI_ERR_UNSUPPORTED = -2,
    WIFI_ERR_INVALID = -3,
    WIFI_ERR_SECURITY = -4,
    WIFI_ERR_BACKEND = -5,
    WIFI_ERR_SCAN = -6,
    WIFI_ERR_NOT_FOUND = -7,
    WIFI_ERR_STORE = -8,
    WIFI_ERR_FULL = -9,
};

void                        wifi_init(void);
int                         wifi_is_initialized(void);
void                        wifi_poll(void);
const wifi_adapter_info_t  *wifi_adapter_info(void);
int                         wifi_scan_count(void);
const wifi_network_t       *wifi_scan_at(int index);
int                         wifi_saved_count(void);
const wifi_saved_network_t *wifi_saved_at(int index);
int                         wifi_scan_request(void);
int                         wifi_save_network(const char *ssid, wifi_security_t security,
                                              const char *passphrase);
int                         wifi_connect_manual(const char *ssid, wifi_security_t security,
                                                const char *passphrase);
int                         wifi_connect_scan(int index, const char *passphrase);
int                         wifi_connect_saved(int index);
int                         wifi_forget_saved(int index);
void                        wifi_disconnect(void);
wifi_state_t                wifi_connection_state(void);
const char                 *wifi_connection_note(void);
const char                 *wifi_current_ssid(void);
wifi_security_t             wifi_current_security(void);
uint8_t                     wifi_signal_strength(void);
const char                 *wifi_family_name(wifi_family_t family);
const char                 *wifi_security_name(wifi_security_t security);
const char                 *wifi_result_string(int rc);
