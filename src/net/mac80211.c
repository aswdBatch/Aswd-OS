
#include "net/mac80211.h"
#include "lib/string.h"

uint8_t g_sta_mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

static void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static const uint8_t bcast[6] = { 0xff,0xff,0xff,0xff,0xff,0xff };

static int ie_append(uint8_t *buf, int used, int max,
                     uint8_t tag, const uint8_t *data, int data_len)
{
    if (used + 2 + data_len > max) return used;
    buf[used++] = tag;
    buf[used++] = (uint8_t)data_len;
    for (int i = 0; i < data_len; i++) buf[used++] = data[i];
    return used;
}

static int write_mgmt_hdr(uint8_t *buf, uint16_t fc,
                           const uint8_t *da, const uint8_t *sa,
                           const uint8_t *bssid, uint16_t seq)
{
    put_le16(buf + 0, fc);
    put_le16(buf + 2, 0);
    mem_copy(buf + 4,  da,    6);
    mem_copy(buf + 10, sa,    6);
    mem_copy(buf + 16, bssid, 6);
    put_le16(buf + 22, (uint16_t)(seq << 4));
    return 24;
}

static const uint8_t k_rates[] = {
    0x82, 0x84, 0x8b, 0x96,
    0x0c, 0x12, 0x18, 0x24,
};

static const uint8_t k_rsn_ie[] = {

    0x01, 0x00,

    0x00, 0x0f, 0xac, 0x04,

    0x01, 0x00,

    0x00, 0x0f, 0xac, 0x04,

    0x01, 0x00,

    0x00, 0x0f, 0xac, 0x02,

    0x00, 0x00,
};

int mac80211_build_probe_req(uint8_t *buf, int buf_len,
                             const uint8_t *da, const uint8_t *bssid,
                             uint16_t seq)
{
    int n;
    uint16_t fc = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_PROBE_REQ;
    if (buf_len < 64) return 0;
    n = write_mgmt_hdr(buf, fc, da, g_sta_mac, bssid, seq);

    n = ie_append(buf, n, buf_len, IE_SSID, 0, 0);

    n = ie_append(buf, n, buf_len, IE_SUPPORTED_RATES, k_rates, sizeof(k_rates));
    return n;
}

int mac80211_build_auth_req(uint8_t *buf, int buf_len,
                            const uint8_t *bssid, uint16_t seq)
{
    int n;
    uint16_t fc = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH;
    if (buf_len < 30) return 0;
    n = write_mgmt_hdr(buf, fc, bssid, g_sta_mac, bssid, seq);
    put_le16(buf + n, AUTH_ALG_OPEN); n += 2;
    put_le16(buf + n, 1);             n += 2;
    put_le16(buf + n, 0);             n += 2;
    return n;
}

int mac80211_build_assoc_req(uint8_t *buf, int buf_len,
                             const char *ssid, const uint8_t *bssid,
                             uint16_t seq, uint8_t has_rsn)
{
    int n, ssid_len;
    uint16_t fc = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ASSOC_REQ;
    if (buf_len < 80) return 0;
    n = write_mgmt_hdr(buf, fc, bssid, g_sta_mac, bssid, seq);

    put_le16(buf + n, WLAN_CAP_ESS | (has_rsn ? WLAN_CAP_PRIVACY : 0));
    n += 2;
    put_le16(buf + n, 10); n += 2;

    ssid_len = (int)str_len(ssid);
    if (ssid_len > MAC80211_MAX_SSID) ssid_len = MAC80211_MAX_SSID;
    n = ie_append(buf, n, buf_len, IE_SSID, (const uint8_t *)ssid, ssid_len);
    n = ie_append(buf, n, buf_len, IE_SUPPORTED_RATES, k_rates, sizeof(k_rates));
    if (has_rsn)
        n = ie_append(buf, n, buf_len, IE_RSN, k_rsn_ie, sizeof(k_rsn_ie));
    return n;
}

int mac80211_build_data(uint8_t *buf, int buf_len,
                        const uint8_t *bssid,
                        const uint8_t *payload, int payload_len,
                        uint16_t seq)
{

    uint16_t fc = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA | IEEE80211_FC_TODS;
    int n = 0;
    if (buf_len < 26 + payload_len) return 0;

    put_le16(buf + 0, fc);
    put_le16(buf + 2, 0);
    mem_copy(buf + 4,  bssid,     6);
    mem_copy(buf + 10, g_sta_mac, 6);
    mem_copy(buf + 16, bssid,     6);
    put_le16(buf + 22, (uint16_t)(seq << 4));
    n = 24;

    buf[n++] = 0xaa; buf[n++] = 0xaa; buf[n++] = 0x03;
    buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00;

    for (int i = 0; i < payload_len; i++) buf[n++] = payload[i];
    return n;
}

int mac80211_build_eapol(uint8_t *buf, int buf_len,
                         const uint8_t *bssid,
                         const uint8_t *eapol_payload, int eapol_len,
                         uint16_t seq)
{

    uint8_t pkt[6 + 2048];
    int pkt_len;
    if (eapol_len + 8 > (int)sizeof(pkt)) return 0;

    pkt[0] = 0x88; pkt[1] = 0x8e;
    for (int i = 0; i < eapol_len; i++) pkt[2+i] = eapol_payload[i];
    pkt_len = 2 + eapol_len;
    return mac80211_build_data(buf, buf_len, bssid, pkt, pkt_len, seq);
}

static const uint8_t *find_ie(const uint8_t *ies, int ies_len,
                               uint8_t tag, int *out_len)
{
    int i = 0;
    while (i + 2 <= ies_len) {
        uint8_t t = ies[i];
        int     l = ies[i+1];
        if (i + 2 + l > ies_len) break;
        if (t == tag) { *out_len = l; return ies + i + 2; }
        i += 2 + l;
    }
    return 0;
}

static int parse_beacon_body(const uint8_t *body, int body_len,
                             const uint8_t *bssid,
                             mac80211_network_t *out)
{
    const uint8_t *ies;
    int ies_len, ie_len;
    const uint8_t *ie;

    if (body_len < 12) return 0;

    out->capabilities = (uint16_t)body[10] | ((uint16_t)body[11] << 8);
    mem_copy(out->bssid, bssid, 6);

    ies     = body + 12;
    ies_len = body_len - 12;

    ie = find_ie(ies, ies_len, IE_SSID, &ie_len);
    if (ie && ie_len > 0 && ie_len <= MAC80211_MAX_SSID) {
        mem_copy(out->ssid, ie, ie_len);
        out->ssid[ie_len] = '\0';
        out->ssid_len = (uint8_t)ie_len;
    }

    ie = find_ie(ies, ies_len, IE_DS_PARAM, &ie_len);
    if (ie && ie_len == 1) out->channel = ie[0];

    ie = find_ie(ies, ies_len, IE_RSN, &ie_len);
    out->has_rsn = (ie != 0) ? 1 : 0;

    ie = find_ie(ies, ies_len, IE_VENDOR, &ie_len);
    while (ie) {
        if (ie_len >= 4 && ie[0]==0x00 && ie[1]==0x50 && ie[2]==0xf2 && ie[3]==0x01) {
            out->has_wpa = 1;
            break;
        }

        ie = find_ie(ie + ie_len, ies_len - (int)(ie - ies) - ie_len,
                     IE_VENDOR, &ie_len);
    }

    return 1;
}

uint16_t mac80211_fc(const uint8_t *frame) {
    return (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
}

int mac80211_is_beacon(const uint8_t *frame, int len) {
    if (len < 24) return 0;
    return (mac80211_fc(frame) & 0x00fc) == IEEE80211_STYPE_BEACON;
}

int mac80211_is_probe_resp(const uint8_t *frame, int len) {
    if (len < 24) return 0;
    return (mac80211_fc(frame) & 0x00fc) == IEEE80211_STYPE_PROBE_RESP;
}

int mac80211_is_auth(const uint8_t *frame, int len) {
    if (len < 24) return 0;
    return (mac80211_fc(frame) & 0x00fc) == IEEE80211_STYPE_AUTH;
}

int mac80211_is_assoc_resp(const uint8_t *frame, int len) {
    if (len < 24) return 0;
    return (mac80211_fc(frame) & 0x00fc) == IEEE80211_STYPE_ASSOC_RESP;
}

int mac80211_is_data(const uint8_t *frame, int len) {
    if (len < 24) return 0;
    uint16_t fc = mac80211_fc(frame);
    return (fc & 0x000c) == IEEE80211_FTYPE_DATA;
}

int mac80211_is_eapol(const uint8_t *frame, int len) {

    if (!mac80211_is_data(frame, len)) return 0;
    if (len < 32) return 0;

    return (frame[24]==0xaa && frame[25]==0xaa &&
            frame[30]==0x88 && frame[31]==0x8e);
}

int mac80211_frame_for_us(const uint8_t *frame, int len) {
    int i;
    if (len < 10) return 0;

    const uint8_t *da = frame + 4;

    int is_bcast = 1, is_us = 1;
    for (i = 0; i < 6; i++) {
        if (da[i] != 0xff) is_bcast = 0;
        if (da[i] != g_sta_mac[i]) is_us = 0;
    }
    return is_bcast || is_us;
}

int mac80211_parse_beacon(const uint8_t *frame, int len, mac80211_network_t *out) {
    if (!mac80211_is_beacon(frame, len)) return 0;
    const uint8_t *bssid = frame + 16;
    return parse_beacon_body(frame + 24, len - 24, bssid, out);
}

int mac80211_parse_probe_resp(const uint8_t *frame, int len, mac80211_network_t *out) {
    if (!mac80211_is_probe_resp(frame, len)) return 0;
    const uint8_t *bssid = frame + 16;
    return parse_beacon_body(frame + 24, len - 24, bssid, out);
}

int mac80211_parse_auth_resp(const uint8_t *frame, int len) {
    if (!mac80211_is_auth(frame, len)) return -1;
    if (len < 30) return -1;

    uint16_t seq_num = get_le16(frame + 26);
    if (seq_num != 2) return -1;
    return (int)get_le16(frame + 28);
}

int mac80211_parse_assoc_resp(const uint8_t *frame, int len) {
    if (!mac80211_is_assoc_resp(frame, len)) return -1;
    if (len < 30) return -1;

    return (int)get_le16(frame + 26);
}

int mac80211_parse_data(const uint8_t *frame, int len,
                        uint8_t *out_buf, int out_max)
{
    int payload_start, payload_len;
    if (!mac80211_is_data(frame, len)) return -1;

    uint16_t fc = mac80211_fc(frame);
    int addr4 = ((fc & 0x0300) == 0x0300) ? 1 : 0;
    payload_start = 24 + (addr4 ? 6 : 0);

    if (len > payload_start + 7 &&
        frame[payload_start]   == 0xaa &&
        frame[payload_start+1] == 0xaa &&
        frame[payload_start+2] == 0x03)
    {
        payload_start += 6;
    }
    payload_len = len - payload_start;
    if (payload_len <= 0) return -1;
    if (payload_len > out_max) payload_len = out_max;
    mem_copy(out_buf, frame + payload_start, payload_len);
    return payload_len;
}
