
#include "net/wifi_rtl8187.h"
#include "net/mac80211.h"
#include "net/wpa.h"
#include "net/wifi.h"
#include "usb/uhci.h"
#include "lib/string.h"

#define RTL8187_VENDOR  0x0bda
static const uint16_t k_rtl8187_ids[] = {
    0x8187, 0x8189, 0x8197, 0x8198,
    0x8187b,

    0,
};

static const uint16_t k_rtl8187_extra_vids[] = { 0x050d, 0x0846, 0x07b8, 0x1737, 0 };
static const uint16_t k_rtl8187_extra_pids[] = { 0x705e, 0x4260, 0x8187, 0x0073, 0 };

#define RTL8187_REQT_READ   0xc0
#define RTL8187_REQT_WRITE  0x40
#define RTL8187_REQ_GETREG  0x05
#define RTL8187_REQ_SETREG  0x05

#define RTL8187_REG_MAC0        0x0000
#define RTL8187_REG_CMD         0x0037
#define RTL8187_REG_TXCONF      0x0040
#define RTL8187_REG_RXCONF      0x0044
#define RTL8187_REG_INTTIMEOUT  0x0048
#define RTL8187_REG_EEPROM_CMD  0x0050
#define RTL8187_REG_CONFIG3     0x0059
#define RTL8187_REG_TX_AGC_CTL  0x009c
#define RTL8187_REG_SYSCONFIG   0x00b4
#define RTL8187_REG_GPIO        0x0091

#define RTL8187_CMD_RESET       0x10
#define RTL8187_CMD_RX_ENABLE   0x08
#define RTL8187_CMD_TX_ENABLE   0x04

#define RTL8187_EP_BULK_IN   1
#define RTL8187_EP_BULK_OUT  2

static uint8_t  g_initialized = 0;
static int      g_usb_port    = -1;
static uint8_t  g_usb_addr    = 0;

#define RTL8187_MAX_SCAN  32
static mac80211_network_t g_scan_nets[RTL8187_MAX_SCAN];
static int  g_scan_count;
static uint8_t  g_assoc_bssid[6];
static char     g_assoc_ssid[33];
static uint8_t  g_assoc_security;
static uint16_t g_seq;

#define RTL8187_BUF_SIZE 2048
static uint8_t g_rx_buf[RTL8187_BUF_SIZE];
static uint8_t g_tx_buf[RTL8187_BUF_SIZE];

static uint8_t rtl8187_read8(uint16_t addr) {
    uint8_t val = 0;
    uhci_control_transfer(g_usb_addr, RTL8187_REQT_READ, RTL8187_REQ_GETREG,
                          addr, 0, &val, 1);
    return val;
}

static void rtl8187_write8(uint16_t addr, uint8_t val) {
    uhci_control_transfer(g_usb_addr, RTL8187_REQT_WRITE, RTL8187_REQ_SETREG,
                          addr, 0, &val, 1);
}

static uint32_t rtl8187_read32(uint16_t addr) {
    uint32_t val = 0;
    uhci_control_transfer(g_usb_addr, RTL8187_REQT_READ, RTL8187_REQ_GETREG,
                          addr, 0, (uint8_t *)&val, 4);
    return val;
}

static void rtl8187_write32(uint16_t addr, uint32_t val) {
    uhci_control_transfer(g_usb_addr, RTL8187_REQT_WRITE, RTL8187_REQ_SETREG,
                          addr, 0, (uint8_t *)&val, 4);
}

static int rtl8187_hw_init(void) {
    int i;

    rtl8187_write8(RTL8187_REG_CMD, RTL8187_CMD_RESET);
    for (volatile int d = 0; d < 500000; d++) {}

    for (i = 0; i < 100; i++) {
        if (!(rtl8187_read8(RTL8187_REG_CMD) & RTL8187_CMD_RESET)) break;
        for (volatile int d = 0; d < 10000; d++) {}
    }
    if (i == 100) return 0;

    for (i = 0; i < 6; i++)
        g_sta_mac[i] = rtl8187_read8(RTL8187_REG_MAC0 + i);

    rtl8187_write8(RTL8187_REG_CONFIG3, 0x00);
    rtl8187_write32(RTL8187_REG_TXCONF, 0x00000300);
    rtl8187_write32(RTL8187_REG_RXCONF,
                    (1<<6) |
                    (1<<3) |
                    (1<<1));

    rtl8187_write8(RTL8187_REG_CMD, RTL8187_CMD_TX_ENABLE | RTL8187_CMD_RX_ENABLE);

    return 1;
}

static void rtl8187_set_channel(int chan) {

    uint32_t freq_idx = (uint32_t)chan;
    rtl8187_write32(0x0090, freq_idx);
    for (volatile int d = 0; d < 30000; d++) {}
}

static int rtl8187_tx_frame(const uint8_t *frame, int len) {
    if (!g_initialized || len > RTL8187_BUF_SIZE - 4) return -1;

    g_tx_buf[0] = (uint8_t)len;
    g_tx_buf[1] = (uint8_t)(len >> 8);
    g_tx_buf[2] = 0;
    g_tx_buf[3] = 0;
    mem_copy(g_tx_buf + 4, frame, len);

    return uhci_bulk_transfer(g_usb_addr, RTL8187_EP_BULK_OUT,
                              g_tx_buf, len + 4);
}

static int rtl8187_rx_frame(uint8_t *buf, int max_len) {

    int got = uhci_bulk_transfer_in(g_usb_addr, RTL8187_EP_BULK_IN,
                                    g_rx_buf, RTL8187_BUF_SIZE);
    if (got < 6) return 0;

    int frame_len = got - 4;
    if (frame_len <= 0 || frame_len > max_len) return 0;
    mem_copy(buf, g_rx_buf, frame_len);
    return frame_len;
}

int rtl8187_scan(wifi_network_t *out, int max_entries) {
    int found = 0;
    uint8_t probe[256], rx_buf[RTL8187_BUF_SIZE];
    uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    g_scan_count = 0;

    for (int ch = 1; ch <= 13 && found < max_entries && found < RTL8187_MAX_SCAN; ch++) {
        rtl8187_set_channel(ch);
        int plen = mac80211_build_probe_req(probe, sizeof(probe), bcast, bcast, g_seq++);
        rtl8187_tx_frame(probe, plen);

        for (int wait = 0; wait < 3000000; wait++) {
            int rx_len = rtl8187_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24) {
                mac80211_network_t net;
                mem_set(&net, 0, sizeof(net));
                if (mac80211_parse_beacon(rx_buf, rx_len, &net) ||
                    mac80211_parse_probe_resp(rx_buf, rx_len, &net))
                {
                    if (!net.ssid_len) continue;
                    int dup = 0;
                    for (int k=0;k<found;k++)
                        if (str_ncmp(g_scan_nets[k].ssid,net.ssid,33)==0){dup=1;break;}
                    if (!dup && found < RTL8187_MAX_SCAN) {
                        g_scan_nets[found] = net;
                        str_copy(out[found].ssid, net.ssid, 33);
                        out[found].signal_pct  = 65;
                        out[found].connectable = 1;
                        out[found].security    = (net.has_rsn||net.has_wpa) ?
                                                  WIFI_SECURITY_WPA2 : WIFI_SECURITY_OPEN;
                        found++;
                    }
                }
            }
        }
    }
    g_scan_count = found;
    return found;
}

int rtl8187_associate(const char *ssid, wifi_security_t security, const char *passphrase) {
    uint8_t auth_frame[64], assoc_frame[256], rx_buf[RTL8187_BUF_SIZE];
    int net_idx = -1, retries;

    for (int i=0;i<g_scan_count;i++)
        if (str_ncmp(g_scan_nets[i].ssid,ssid,33)==0){net_idx=i;break;}
    if (net_idx < 0) return WIFI_ERR_SCAN_FAILED;

    mem_copy(g_assoc_bssid, g_scan_nets[net_idx].bssid, 6);
    str_copy(g_assoc_ssid, ssid, 33);
    g_assoc_security = (uint8_t)security;
    rtl8187_set_channel(g_scan_nets[net_idx].channel ? g_scan_nets[net_idx].channel : 6);

    int len = mac80211_build_auth_req(auth_frame, sizeof(auth_frame), g_assoc_bssid, g_seq++);
    rtl8187_tx_frame(auth_frame, len);
    retries = 2000000;
    while (retries-- > 0) {
        int rx_len = rtl8187_rx_frame(rx_buf, sizeof(rx_buf));
        if (rx_len >= 24 && mac80211_frame_for_us(rx_buf, rx_len)) {
            int status = mac80211_parse_auth_resp(rx_buf, rx_len);
            if (status == 0) break;
            if (status > 0) return WIFI_ERR_AUTH_FAILED;
        }
    }
    if (retries <= 0) return WIFI_ERR_TIMEOUT;

    uint8_t has_rsn = (security == WIFI_SECURITY_WPA2) ? 1 : 0;
    len = mac80211_build_assoc_req(assoc_frame, sizeof(assoc_frame),
                                   ssid, g_assoc_bssid, g_seq++, has_rsn);
    rtl8187_tx_frame(assoc_frame, len);
    retries = 2000000;
    while (retries-- > 0) {
        int rx_len = rtl8187_rx_frame(rx_buf, sizeof(rx_buf));
        if (rx_len >= 24 && mac80211_frame_for_us(rx_buf, rx_len)) {
            int status = mac80211_parse_assoc_resp(rx_buf, rx_len);
            if (status == 0) break;
            if (status > 0) return WIFI_ERR_ASSOC_FAILED;
        }
    }
    if (retries <= 0) return WIFI_ERR_TIMEOUT;

    if (security == WIFI_SECURITY_WPA2 && passphrase && passphrase[0]) {
        wpa_init(ssid, passphrase, g_assoc_bssid);
        retries = 5000000;
        while (retries-- > 0 && wpa_state()!=WPA_STATE_CONNECTED && wpa_state()!=WPA_STATE_FAILED) {
            int rx_len = rtl8187_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24 && mac80211_is_eapol(rx_buf, rx_len)) {
                uint8_t ep[256]; int eplen = mac80211_parse_data(rx_buf, rx_len, ep, sizeof(ep));
                if (eplen > 2) {
                    uint8_t reply[256]; int reply_len=0;
                    wpa_rx_eapol(ep+2, eplen-2, reply, &reply_len);
                    if (reply_len > 0) {
                        uint8_t ef[512];
                        int flen = mac80211_build_eapol(ef,sizeof(ef),g_assoc_bssid,reply,reply_len,g_seq++);
                        rtl8187_tx_frame(ef, flen);
                    }
                }
            }
        }
        if (wpa_state() != WPA_STATE_CONNECTED) return WIFI_ERR_AUTH_FAILED;
    }
    return WIFI_OK;
}

void rtl8187_disconnect(void) { mem_set(g_assoc_bssid,0,6); g_assoc_ssid[0]='\0'; }

void rtl8187_poll(void) {}

int rtl8187_tx(const uint8_t *frame, uint16_t len) {
    if (g_assoc_security==WIFI_SECURITY_WPA2 && wpa_state()==WPA_STATE_CONNECTED) {
        uint8_t enc[RTL8187_BUF_SIZE], pn[6];
        extern uint8_t g_tx_pn[6];
        for (int i=5;i>=0;i--) if(++g_tx_pn[i]) break;
        mem_copy(pn, g_tx_pn, 6);
        int elen = wpa_ccmp_encrypt(frame, len, enc, pn, wpa_ptk_tk(), g_assoc_bssid, 0);
        return rtl8187_tx_frame(enc, elen);
    }
    return rtl8187_tx_frame(frame, len);
}

int rtl8187_rx(uint8_t *buf, uint16_t max_len) {
    uint8_t raw[RTL8187_BUF_SIZE];
    int raw_len = rtl8187_rx_frame(raw, sizeof(raw));
    if (raw_len <= 0) return 0;
    if (g_assoc_security==WIFI_SECURITY_WPA2 && wpa_state()==WPA_STATE_CONNECTED &&
        raw_len >= 24 && (mac80211_fc(raw) & IEEE80211_FC_PROTECTED))
    {
        const uint8_t *ch = raw+24;
        uint8_t pn[6];
        pn[5]=ch[0];pn[4]=ch[1];pn[3]=ch[4];pn[2]=ch[5];pn[1]=ch[6];pn[0]=ch[7];
        int plen = wpa_ccmp_decrypt(raw+32, raw_len-32, buf, pn, wpa_ptk_tk(), g_assoc_bssid, 0);
        return plen > 0 ? plen : 0;
    }
    int plen = mac80211_parse_data(raw, raw_len, buf, max_len);
    return plen > 0 ? plen : 0;
}

int rtl8187_probe(void) {

    int n = uhci_device_count();
    for (int i = 0; i < n; i++) {
        uint16_t vid, pid;
        uint8_t addr;
        if (!uhci_get_device_info(i, &vid, &pid, &addr)) continue;

        int match = 0;
        if (vid == RTL8187_VENDOR) {
            for (int j = 0; k_rtl8187_ids[j]; j++)
                if (k_rtl8187_ids[j] == pid) { match=1; break; }
        }
        if (!match) {
            for (int j = 0; k_rtl8187_extra_vids[j]; j++)
                if (k_rtl8187_extra_vids[j]==vid && k_rtl8187_extra_pids[j]==pid)
                    { match=1; break; }
        }
        if (match) {
            g_usb_addr = addr;
            g_usb_port = i;
            if (rtl8187_hw_init()) {
                g_initialized = 1;
                return 1;
            }
        }
    }
    return 0;
}

const wifi_backend_ops_t rtl8187_backend_ops = {
    WIFI_FAMILY_REALTEK_RTL8187,
    "rtl8187",
    "",
    0,
    WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2,
    rtl8187_scan,
    rtl8187_associate,
    rtl8187_disconnect,
    rtl8187_poll,
    rtl8187_tx,
    rtl8187_rx,
};
