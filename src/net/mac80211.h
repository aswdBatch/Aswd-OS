#ifndef NET_MAC80211_H
#define NET_MAC80211_H

#include <stdint.h>

#define IEEE80211_FTYPE_MGMT        0x0000
#define IEEE80211_FTYPE_DATA        0x0008

#define IEEE80211_STYPE_PROBE_REQ   0x0040
#define IEEE80211_STYPE_PROBE_RESP  0x0050
#define IEEE80211_STYPE_BEACON      0x0080
#define IEEE80211_STYPE_AUTH        0x00b0
#define IEEE80211_STYPE_DEAUTH      0x00c0
#define IEEE80211_STYPE_ASSOC_REQ   0x0000
#define IEEE80211_STYPE_ASSOC_RESP  0x0010
#define IEEE80211_STYPE_DATA        0x0008

#define IEEE80211_FC_TODS           0x0100
#define IEEE80211_FC_PROTECTED      0x4000

#define IE_SSID                     0
#define IE_SUPPORTED_RATES          1
#define IE_DS_PARAM                 3
#define IE_RSN                      48
#define IE_VENDOR                   221

#define AUTH_ALG_OPEN               0

#define WLAN_STATUS_SUCCESS         0

#define WLAN_CAP_ESS                0x0001
#define WLAN_CAP_PRIVACY            0x0010

#define MAC80211_MAX_FRAME          2346
#define MAC80211_MAX_SSID           32

typedef struct {
    uint8_t  bssid[6];
    char     ssid[MAC80211_MAX_SSID + 1];
    uint8_t  ssid_len;
    uint8_t  channel;
    uint8_t  signal_pct;
    uint8_t  has_rsn;
    uint8_t  has_wpa;
    uint16_t capabilities;
} mac80211_network_t;

extern uint8_t g_sta_mac[6];

int mac80211_build_probe_req(uint8_t *buf, int buf_len,
                             const uint8_t *da,
                             const uint8_t *bssid,
                             uint16_t seq);

int mac80211_build_auth_req(uint8_t *buf, int buf_len,
                            const uint8_t *bssid,
                            uint16_t seq);

int mac80211_build_assoc_req(uint8_t *buf, int buf_len,
                             const char    *ssid,
                             const uint8_t *bssid,
                             uint16_t       seq,
                             uint8_t        has_rsn);

int mac80211_build_data(uint8_t *buf, int buf_len,
                        const uint8_t *bssid,
                        const uint8_t *payload,
                        int            payload_len,
                        uint16_t       seq);

int mac80211_build_eapol(uint8_t *buf, int buf_len,
                         const uint8_t *bssid,
                         const uint8_t *eapol_payload, int eapol_len,
                         uint16_t seq);

int mac80211_parse_beacon(const uint8_t *frame, int len,
                          mac80211_network_t *out);

int mac80211_parse_probe_resp(const uint8_t *frame, int len,
                              mac80211_network_t *out);

int mac80211_parse_auth_resp(const uint8_t *frame, int len);

int mac80211_parse_assoc_resp(const uint8_t *frame, int len);

int mac80211_parse_data(const uint8_t *frame, int len,
                        uint8_t *out_buf, int out_max);

int mac80211_frame_for_us(const uint8_t *frame, int len);

uint16_t mac80211_fc(const uint8_t *frame);
int      mac80211_is_beacon(const uint8_t *frame, int len);
int      mac80211_is_probe_resp(const uint8_t *frame, int len);
int      mac80211_is_auth(const uint8_t *frame, int len);
int      mac80211_is_assoc_resp(const uint8_t *frame, int len);
int      mac80211_is_data(const uint8_t *frame, int len);
int      mac80211_is_eapol(const uint8_t *frame, int len);

#endif
