
#include "net/wifi_ath5k.h"
#include "net/mac80211.h"
#include "net/wpa.h"
#include "net/wifi.h"
#include "drivers/pci.h"
#include "lib/string.h"

#define ATH5K_VENDOR    0x168c
static const uint16_t k_ath5k_ids[] = {
    0x0007,
    0x0011,
    0x0012,
    0x0013,
    0x0014,
    0x0015,
    0x0016,
    0x0017,
    0x0018,
    0x0019,
    0x001a,
    0x001b,
    0x001c,
    0x001d,
    0x0020,
    0x0207,
    0x0213,
    0,
};

#define AR5K_CR             0x0008
#define AR5K_ISR            0x0010
#define AR5K_IMR            0x0018
#define AR5K_RXDP           0x000c
#define AR5K_TXDP0          0x0038
#define AR5K_Q_TXE          0x0040
#define AR5K_PHY_BASE       0x9800
#define AR5K_PHY_CHAN       (AR5K_PHY_BASE + 0x1c)
#define AR5K_RESET_CTL      0x4000
#define AR5K_SLEEP_CTL      0x4004

#define AR5K_CR_TXE0        (1<<0)
#define AR5K_CR_RXE         (1<<2)
#define AR5K_CR_TXD0        (1<<4)
#define AR5K_CR_RXD         (1<<6)

#define AR5K_ISR_RXOK       (1<<0)
#define AR5K_ISR_TXOK       (1<<6)

#define AR5K_RST_POWER_ON   (1<<0)
#define AR5K_RST_CHIP       (1<<1)
#define AR5K_RST_MAC        (1<<2)
#define AR5K_RST_BB         (1<<3)

typedef struct {
    volatile uint32_t ds_link;
    volatile uint32_t ds_data;
    volatile uint32_t ds_ctl0;
    volatile uint32_t ds_ctl1;
    volatile uint32_t ds_hw[2];
} ath5k_desc_t;

#define AR5K_TXCTL1_BUF_LEN  0x00000fff
#define AR5K_TXCTL1_DONE     (1<<31)
#define AR5K_RXCTL1_BUF_LEN  0x00000fff
#define AR5K_RXCTL1_DONE     (1<<31)
#define AR5K_RXCTL0_FRAMELEN 0x00000fff

#define ATH5K_TX_DESCS  8
#define ATH5K_RX_DESCS  8
#define ATH5K_BUF_SIZE  2048

static uint8_t   g_initialized = 0;
static uint32_t  g_mmio_base   = 0;
static ath5k_desc_t g_tx_desc[ATH5K_TX_DESCS];
static ath5k_desc_t g_rx_desc[ATH5K_RX_DESCS];
static uint8_t      g_tx_buf[ATH5K_TX_DESCS][ATH5K_BUF_SIZE];
static uint8_t      g_rx_buf[ATH5K_RX_DESCS][ATH5K_BUF_SIZE];
static int g_tx_head, g_tx_tail, g_rx_head;

#define ATH5K_MAX_SCAN 32
static mac80211_network_t g_scan_nets[ATH5K_MAX_SCAN];
static int g_scan_count;
static uint8_t  g_assoc_bssid[6];
static char     g_assoc_ssid[33];
static uint8_t  g_assoc_security;
static uint16_t g_seq;

static uint32_t reg_read(uint32_t off) {
    volatile uint32_t *p = (volatile uint32_t *)(g_mmio_base + off);
    return *p;
}
static void reg_write(uint32_t off, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)(g_mmio_base + off);
    *p = val;
}

static void ath5k_hw_reset(void) {
    reg_write(AR5K_RESET_CTL, AR5K_RST_CHIP | AR5K_RST_MAC | AR5K_RST_BB);
    for (volatile int d = 0; d < 200000; d++) {}
    reg_write(AR5K_RESET_CTL, 0);
    for (volatile int d = 0; d < 200000; d++) {}
}

static void ath5k_init_rings(void) {
    int i;
    for (i = 0; i < ATH5K_TX_DESCS; i++) {
        g_tx_desc[i].ds_link = (uint32_t)(uintptr_t)&g_tx_desc[(i+1)%ATH5K_TX_DESCS];
        g_tx_desc[i].ds_data = (uint32_t)(uintptr_t)g_tx_buf[i];
        g_tx_desc[i].ds_ctl0 = 0;
        g_tx_desc[i].ds_ctl1 = 0;
    }
    g_tx_head = g_tx_tail = 0;

    for (i = 0; i < ATH5K_RX_DESCS; i++) {
        g_rx_desc[i].ds_link = (uint32_t)(uintptr_t)&g_rx_desc[(i+1)%ATH5K_RX_DESCS];
        g_rx_desc[i].ds_data = (uint32_t)(uintptr_t)g_rx_buf[i];
        g_rx_desc[i].ds_ctl0 = 0;
        g_rx_desc[i].ds_ctl1 = ATH5K_BUF_SIZE & AR5K_RXCTL1_BUF_LEN;
    }
    g_rx_head = 0;
    reg_write(AR5K_RXDP,  (uint32_t)(uintptr_t)&g_rx_desc[0]);
    reg_write(AR5K_TXDP0, (uint32_t)(uintptr_t)&g_tx_desc[0]);
}

static void ath5k_set_channel(int chan) {
    uint32_t freq = (uint32_t)(2407 + chan * 5);
    uint32_t phy  = ((freq - 2192) / 5) & 0xff;
    reg_write(AR5K_PHY_CHAN, (phy << 16) | (1<<8) | 1);
    for (volatile int d = 0; d < 50000; d++) {}
}

static int ath5k_hw_init(void) {
    ath5k_hw_reset();

    uint32_t mac_lo = reg_read(0x800c);
    uint32_t mac_hi = reg_read(0x8010);
    if (mac_lo || mac_hi) {
        g_sta_mac[0] = (uint8_t)(mac_hi >> 8);
        g_sta_mac[1] = (uint8_t)mac_hi;
        g_sta_mac[2] = (uint8_t)(mac_lo >> 24);
        g_sta_mac[3] = (uint8_t)(mac_lo >> 16);
        g_sta_mac[4] = (uint8_t)(mac_lo >> 8);
        g_sta_mac[5] = (uint8_t)mac_lo;
    }
    ath5k_init_rings();
    ath5k_set_channel(6);
    reg_write(AR5K_IMR, 0);
    reg_write(AR5K_CR, AR5K_CR_RXE);
    return 1;
}

static int ath5k_tx_frame(const uint8_t *frame, int len) {
    if (!g_initialized || len > ATH5K_BUF_SIZE) return -1;
    int slot = g_tx_tail;
    if (g_tx_desc[slot].ds_ctl1 != 0) return -1;
    mem_copy(g_tx_buf[slot], frame, len);
    g_tx_desc[slot].ds_data = (uint32_t)(uintptr_t)g_tx_buf[slot];
    g_tx_desc[slot].ds_ctl0 = (uint32_t)len;
    g_tx_desc[slot].ds_ctl1 = (uint32_t)len & AR5K_TXCTL1_BUF_LEN;
    reg_write(AR5K_Q_TXE, 1);
    g_tx_tail = (g_tx_tail + 1) % ATH5K_TX_DESCS;
    return 0;
}

static int ath5k_rx_frame(uint8_t *buf, int max_len) {
    ath5k_desc_t *d = &g_rx_desc[g_rx_head];
    if (!(d->ds_ctl1 & AR5K_RXCTL1_DONE)) return 0;
    int len = (int)(d->ds_ctl0 & AR5K_RXCTL0_FRAMELEN);
    if (len > max_len) len = max_len;
    mem_copy(buf, g_rx_buf[g_rx_head], len);
    d->ds_ctl1 = ATH5K_BUF_SIZE & AR5K_RXCTL1_BUF_LEN;
    d->ds_ctl0 = 0;
    g_rx_head = (g_rx_head + 1) % ATH5K_RX_DESCS;
    return len;
}

int ath5k_scan(wifi_network_t *out, int max_entries) {
    int found = 0;
    uint8_t probe[256], rx_buf[ATH5K_BUF_SIZE];
    uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    g_scan_count = 0;

    for (int ch = 1; ch <= 13 && found < max_entries && found < ATH5K_MAX_SCAN; ch++) {
        ath5k_set_channel(ch);
        for (volatile int d = 0; d < 10000; d++) {}
        int plen = mac80211_build_probe_req(probe, sizeof(probe), bcast, bcast, g_seq++);
        ath5k_tx_frame(probe, plen);

        for (int wait = 0; wait < 5000000; wait++) {
            int rx_len = ath5k_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24) {
                mac80211_network_t net;
                mem_set(&net, 0, sizeof(net));
                if (mac80211_parse_beacon(rx_buf, rx_len, &net) ||
                    mac80211_parse_probe_resp(rx_buf, rx_len, &net))
                {
                    if (!net.ssid_len) continue;
                    int dup = 0;
                    for (int k = 0; k < found; k++)
                        if (str_ncmp(g_scan_nets[k].ssid, net.ssid, 33) == 0) { dup=1; break; }
                    if (!dup && found < ATH5K_MAX_SCAN) {
                        g_scan_nets[found] = net;
                        str_copy(out[found].ssid, net.ssid, 33);
                        out[found].signal_pct  = net.signal_pct ? net.signal_pct : 60;
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

int ath5k_associate(const char *ssid, wifi_security_t security, const char *passphrase) {
    uint8_t auth_frame[64], assoc_frame[256], rx_buf[ATH5K_BUF_SIZE];
    int net_idx = -1, retries;

    for (int i = 0; i < g_scan_count; i++)
        if (str_ncmp(g_scan_nets[i].ssid, ssid, 33) == 0) { net_idx=i; break; }
    if (net_idx < 0) return WIFI_ERR_SCAN_FAILED;

    mem_copy(g_assoc_bssid, g_scan_nets[net_idx].bssid, 6);
    str_copy(g_assoc_ssid, ssid, 33);
    g_assoc_security = (uint8_t)security;
    ath5k_set_channel(g_scan_nets[net_idx].channel ? g_scan_nets[net_idx].channel : 6);

    int len = mac80211_build_auth_req(auth_frame, sizeof(auth_frame), g_assoc_bssid, g_seq++);
    ath5k_tx_frame(auth_frame, len);
    retries = 2000000;
    while (retries-- > 0) {
        int rx_len = ath5k_rx_frame(rx_buf, sizeof(rx_buf));
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
    ath5k_tx_frame(assoc_frame, len);
    retries = 2000000;
    while (retries-- > 0) {
        int rx_len = ath5k_rx_frame(rx_buf, sizeof(rx_buf));
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
        while (retries-- > 0 && wpa_state() != WPA_STATE_CONNECTED && wpa_state() != WPA_STATE_FAILED) {
            int rx_len = ath5k_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24 && mac80211_is_eapol(rx_buf, rx_len)) {
                uint8_t ep[256];
                int eplen = mac80211_parse_data(rx_buf, rx_len, ep, sizeof(ep));
                if (eplen > 2) {
                    uint8_t reply[256]; int reply_len = 0;
                    wpa_rx_eapol(ep+2, eplen-2, reply, &reply_len);
                    if (reply_len > 0) {
                        uint8_t ef[512];
                        int flen = mac80211_build_eapol(ef, sizeof(ef), g_assoc_bssid, reply, reply_len, g_seq++);
                        ath5k_tx_frame(ef, flen);
                    }
                }
            }
        }
        if (wpa_state() != WPA_STATE_CONNECTED) return WIFI_ERR_AUTH_FAILED;
    }
    return WIFI_OK;
}

void ath5k_disconnect(void) { mem_set(g_assoc_bssid, 0, 6); g_assoc_ssid[0]='\0'; }

void ath5k_poll(void) {
    while (g_tx_head != g_tx_tail && g_tx_desc[g_tx_head].ds_ctl1 == 0)
        g_tx_head = (g_tx_head + 1) % ATH5K_TX_DESCS;
}

int ath5k_tx(const uint8_t *frame, uint16_t len) {
    if (g_assoc_security == WIFI_SECURITY_WPA2 && wpa_state() == WPA_STATE_CONNECTED) {
        uint8_t enc[ATH5K_BUF_SIZE], pn[6];
        extern uint8_t g_tx_pn[6];
        for (int i=5;i>=0;i--) if(++g_tx_pn[i]) break;
        mem_copy(pn, g_tx_pn, 6);
        int elen = wpa_ccmp_encrypt(frame, len, enc, pn, wpa_ptk_tk(), g_assoc_bssid, 0);
        return ath5k_tx_frame(enc, elen);
    }
    return ath5k_tx_frame(frame, len);
}

int ath5k_rx(uint8_t *buf, uint16_t max_len) {
    uint8_t raw[ATH5K_BUF_SIZE];
    int raw_len = ath5k_rx_frame(raw, sizeof(raw));
    if (raw_len <= 0) return 0;
    if (g_assoc_security == WIFI_SECURITY_WPA2 && wpa_state() == WPA_STATE_CONNECTED &&
        raw_len >= 24 && (mac80211_fc(raw) & IEEE80211_FC_PROTECTED))
    {
        const uint8_t *ch = raw + 24;
        uint8_t pn[6];
        pn[5]=ch[0]; pn[4]=ch[1]; pn[3]=ch[4]; pn[2]=ch[5]; pn[1]=ch[6]; pn[0]=ch[7];
        int plen = wpa_ccmp_decrypt(raw+32, raw_len-32, buf, pn, wpa_ptk_tk(), g_assoc_bssid, 0);
        return plen > 0 ? plen : 0;
    }
    int plen = mac80211_parse_data(raw, raw_len, buf, max_len);
    return plen > 0 ? plen : 0;
}

int ath5k_probe(void) {
    for (int bus=0; bus<256; bus++) {
        for (int dev=0; dev<32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0x00);
            if ((uint16_t)id != ATH5K_VENDOR) continue;
            uint16_t device = (uint16_t)(id >> 16);
            for (int j=0; k_ath5k_ids[j]; j++) {
                if (k_ath5k_ids[j] == device) {
                    uint32_t bar0 = pci_read32(bus,dev,0,0x10) & ~0xf;
                    if (!bar0) continue;
                    g_mmio_base = bar0;
                    uint32_t cmd = pci_read32(bus,dev,0,0x04);
                    pci_write32(bus,dev,0,0x04, cmd|0x06);
                    if (ath5k_hw_init()) { g_initialized=1; return 1; }
                    return 0;
                }
            }
        }
    }
    return 0;
}

const wifi_backend_ops_t ath5k_backend_ops = {
    WIFI_FAMILY_ATHEROS_AR5XXX,
    "ath5k",
    "",
    0,
    WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2,
    ath5k_scan,
    ath5k_associate,
    ath5k_disconnect,
    ath5k_poll,
    ath5k_tx,
    ath5k_rx,
};
