
#include "net/wifi_ath9k.h"
#include "net/mac80211.h"
#include "net/wpa.h"
#include "net/wifi.h"
#include "drivers/pci.h"
#include "lib/string.h"

#define ATH9K_VENDOR        0x168c
static const uint16_t k_ath9k_ids[] = {
    0x0023,
    0x0024,
    0x0027,
    0x0029,
    0x002a,
    0x002b,
    0x002c,
    0x002d,
    0x002e,
    0x0030,
    0x0033,
    0x0034,
    0x0037,
    0x0038,
    0x003c,
    0x0040,
    0x0041,
    0x0042,
    0x0046,
    0x0050,
    0x0053,
    0x0200,
    0,
};

#define AR_RTC_RC           0x7000
#define AR_RTC_PLL_CONTROL  0x7014
#define AR_RTC_RESET        0x7040
#define AR_RTC_STATUS       0x7044
#define AR_RTC_SLEEP_CLK    0x7048
#define AR_RTC_FORCE_WAKE   0x704c
#define AR_RTC_INTR_CAUSE   0x7068

#define AR_CFG              0x0000
#define AR_RXDP             0x000c
#define AR_CFG_SWRB         (1<<3)
#define AR_CFG_SWRD         (1<<4)
#define AR_CFG_SWRG         (1<<5)

#define AR_TXDP0            0x0800
#define AR_Q0_TXDP          0x0800
#define AR_Q_TXE            0x0840
#define AR_Q_TXD            0x0880
#define AR_Q0_RDYTIMECFG    0x09c0
#define AR_ISR              0x0080
#define AR_IMR              0x00a0
#define AR_ISR_RXOK         (1<<0)
#define AR_ISR_TXOK         (1<<6)
#define AR_ISR_RXERR        (1<<2)

#define AR_PHY_BASE         0x9800
#define AR_PHY_TURBO        (AR_PHY_BASE+0x04)
#define AR_PHY_SLEEP_CTR_CONTROL (AR_PHY_BASE+0x270)
#define AR_PHY_RADAR_0      (AR_PHY_BASE+0x24c)

#define AR_DIAG_SW          0x8048

#define AR_RTC_STATUS_M     0x0000000f
#define AR_RTC_STATUS_ON    0x00000002
#define AR_RTC_PM_STATUS_M  0x00000003

#define AR_RTC_FORCE_WAKE_EN       (1<<0)
#define AR_RTC_FORCE_WAKE_ON_INT   (1<<1)

#define AR_CFG_MACDEV_DIS   (1<<11)
#define AR_RC_MAC           (1<<1)
#define AR_RC_BB            (1<<2)

typedef struct {
    volatile uint32_t ds_link;
    volatile uint32_t ds_data;
    volatile uint32_t ds_ctl0;
    volatile uint32_t ds_ctl1;
} ath9k_desc_t;

#define AR_TXC1_MORE        (1<<2)
#define AR_TXC1_VEOL       (1<<30)
#define AR_TXC1_BUF_LEN_M  0x00000fff

#define AR_RXS0_DONE        (1<<31)
#define AR_RXS0_FRAMELEN_M  0x00000fff

#define ATH9K_TX_DESCS  8
#define ATH9K_RX_DESCS  8
#define ATH9K_BUF_SIZE  2048

static uint8_t g_initialized = 0;
static uint32_t g_mmio_base = 0;

static ath9k_desc_t g_tx_desc[ATH9K_TX_DESCS];
static ath9k_desc_t g_rx_desc[ATH9K_RX_DESCS];
static uint8_t      g_tx_buf[ATH9K_TX_DESCS][ATH9K_BUF_SIZE];
static uint8_t      g_rx_buf[ATH9K_RX_DESCS][ATH9K_BUF_SIZE];
static int g_tx_head, g_tx_tail;
static int g_rx_head;

#define ATH9K_MAX_SCAN  32
static mac80211_network_t g_scan_nets[ATH9K_MAX_SCAN];
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

static int reg_wait(uint32_t off, uint32_t mask, uint32_t val, int tries) {
    for (int i = 0; i < tries; i++) {
        if ((reg_read(off) & mask) == val) return 1;

        for (volatile int d = 0; d < 50000; d++) {}
    }
    return 0;
}

static void ath9k_set_power_active(void) {
    reg_write(AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);
    for (volatile int d = 0; d < 100000; d++) {}
    reg_wait(AR_RTC_STATUS, AR_RTC_STATUS_M, AR_RTC_STATUS_ON, 1000);
}

static void ath9k_hw_reset(void) {

    reg_write(AR_RTC_RC, AR_RC_MAC | AR_RC_BB);
    for (volatile int d = 0; d < 100000; d++) {}
    reg_write(AR_RTC_RC, 0);
    for (volatile int d = 0; d < 100000; d++) {}
}

static void ath9k_init_desc_rings(void) {
    int i;

    for (i = 0; i < ATH9K_TX_DESCS; i++) {
        g_tx_desc[i].ds_link = (uint32_t)(uintptr_t)&g_tx_desc[(i+1) % ATH9K_TX_DESCS];
        g_tx_desc[i].ds_data = (uint32_t)(uintptr_t)g_tx_buf[i];
        g_tx_desc[i].ds_ctl0 = 0;
        g_tx_desc[i].ds_ctl1 = 0;
    }
    g_tx_head = g_tx_tail = 0;

    for (i = 0; i < ATH9K_RX_DESCS; i++) {
        g_rx_desc[i].ds_link = (uint32_t)(uintptr_t)&g_rx_desc[(i+1) % ATH9K_RX_DESCS];
        g_rx_desc[i].ds_data = (uint32_t)(uintptr_t)g_rx_buf[i];
        g_rx_desc[i].ds_ctl0 = 0;
        g_rx_desc[i].ds_ctl1 = ATH9K_BUF_SIZE & AR_TXC1_BUF_LEN_M;
    }
    g_rx_head = 0;

    reg_write(AR_RXDP, (uint32_t)(uintptr_t)&g_rx_desc[0]);

    reg_write(AR_TXDP0, (uint32_t)(uintptr_t)&g_tx_desc[0]);
}

static void ath9k_set_channel(int chan) {

    uint32_t freq = 2407 + (uint32_t)chan * 5;

    uint32_t phy_chan = ((freq - 2192) / 5) & 0xff;
    reg_write(AR_PHY_BASE + 0x1c, (phy_chan << 16) | (1 << 8) | 1);
    for (volatile int d = 0; d < 50000; d++) {}
}

static int ath9k_hw_init(void) {
    uint32_t mac_hi, mac_lo;

    ath9k_set_power_active();
    ath9k_hw_reset();

    mac_hi = reg_read(0x800c);
    mac_lo = reg_read(0x8010);
    if (mac_hi || mac_lo) {
        g_sta_mac[0] = (uint8_t)(mac_hi >> 8);
        g_sta_mac[1] = (uint8_t)mac_hi;
        g_sta_mac[2] = (uint8_t)(mac_lo >> 24);
        g_sta_mac[3] = (uint8_t)(mac_lo >> 16);
        g_sta_mac[4] = (uint8_t)(mac_lo >> 8);
        g_sta_mac[5] = (uint8_t)mac_lo;
    }

    reg_write(AR_CFG, AR_CFG_SWRB | AR_CFG_SWRD);

    ath9k_init_desc_rings();

    ath9k_set_channel(6);

    reg_write(AR_IMR, 0);

    return 1;
}

static int ath9k_tx_frame(const uint8_t *frame, int len) {
    int slot;
    if (!g_initialized) return -1;
    if (len > ATH9K_BUF_SIZE) return -1;

    slot = g_tx_tail;
    if (g_tx_desc[slot].ds_ctl1 != 0) return -1;

    mem_copy(g_tx_buf[slot], frame, len);
    g_tx_desc[slot].ds_data = (uint32_t)(uintptr_t)g_tx_buf[slot];
    g_tx_desc[slot].ds_ctl0 = (uint32_t)len;

    g_tx_desc[slot].ds_ctl1 = ((uint32_t)len & AR_TXC1_BUF_LEN_M) | AR_TXC1_VEOL;

    reg_write(AR_Q_TXE, 1 << 0);

    g_tx_tail = (g_tx_tail + 1) % ATH9K_TX_DESCS;
    return 0;
}

static int ath9k_rx_frame(uint8_t *buf, int max_len) {
    ath9k_desc_t *d = &g_rx_desc[g_rx_head];

    if (d->ds_ctl1 & AR_TXC1_BUF_LEN_M) {

        if (!(d->ds_ctl0 & AR_RXS0_DONE)) return 0;

        int len = (int)(d->ds_ctl0 & AR_RXS0_FRAMELEN_M);
        if (len > max_len) len = max_len;
        mem_copy(buf, g_rx_buf[g_rx_head], len);

        d->ds_ctl0 = 0;
        d->ds_ctl1 = ATH9K_BUF_SIZE & AR_TXC1_BUF_LEN_M;
        g_rx_head = (g_rx_head + 1) % ATH9K_RX_DESCS;
        return len;
    }
    return 0;
}

int ath9k_scan(wifi_network_t *out, int max_entries) {
    int found = 0;
    uint8_t probe[256];
    uint8_t bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    uint8_t rx_buf[ATH9K_BUF_SIZE];
    int rx_len;

    g_scan_count = 0;

    for (int ch = 1; ch <= 13 && found < max_entries && found < ATH9K_MAX_SCAN; ch++) {
        ath9k_set_channel(ch);
        for (volatile int d = 0; d < 10000; d++) {}

        int probe_len = mac80211_build_probe_req(probe, sizeof(probe),
                                                 bcast, bcast, g_seq++);
        ath9k_tx_frame(probe, probe_len);

        for (int wait = 0; wait < 5000000; wait++) {
            rx_len = ath9k_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24) {
                mac80211_network_t net;
                mem_set(&net, 0, sizeof(net));
                if (mac80211_parse_beacon(rx_buf, rx_len, &net) ||
                    mac80211_parse_probe_resp(rx_buf, rx_len, &net))
                {
                    if (net.ssid_len == 0) continue;

                    int dup = 0;
                    for (int k = 0; k < found; k++) {
                        if (str_ncmp(g_scan_nets[k].ssid, net.ssid, 33) == 0) {
                            dup = 1; break;
                        }
                    }
                    if (!dup && found < ATH9K_MAX_SCAN) {
                        g_scan_nets[found] = net;

                        str_copy(out[found].ssid, net.ssid, 33);
                        out[found].signal_pct  = net.signal_pct ? net.signal_pct : 60;
                        out[found].connectable = 1;
                        out[found].security    = (net.has_rsn || net.has_wpa) ?
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

int ath9k_associate(const char *ssid, wifi_security_t security, const char *passphrase) {
    uint8_t auth_frame[64];
    uint8_t assoc_frame[256];
    uint8_t rx_buf[ATH9K_BUF_SIZE];
    int rx_len, len, retries;

    int net_idx = -1;
    for (int i = 0; i < g_scan_count; i++) {
        if (str_ncmp(g_scan_nets[i].ssid, ssid, 33) == 0) {
            net_idx = i;
            break;
        }
    }
    if (net_idx < 0) return WIFI_ERR_SCAN_FAILED;

    mem_copy(g_assoc_bssid, g_scan_nets[net_idx].bssid, 6);
    str_copy(g_assoc_ssid, ssid, 33);
    g_assoc_security = (uint8_t)security;

    ath9k_set_channel(g_scan_nets[net_idx].channel ? g_scan_nets[net_idx].channel : 6);

    len = mac80211_build_auth_req(auth_frame, sizeof(auth_frame), g_assoc_bssid, g_seq++);
    ath9k_tx_frame(auth_frame, len);

    retries = 2000000;
    while (retries-- > 0) {
        rx_len = ath9k_rx_frame(rx_buf, sizeof(rx_buf));
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
    ath9k_tx_frame(assoc_frame, len);

    retries = 2000000;
    while (retries-- > 0) {
        rx_len = ath9k_rx_frame(rx_buf, sizeof(rx_buf));
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
        while (retries-- > 0 && wpa_state() != WPA_STATE_CONNECTED &&
               wpa_state() != WPA_STATE_FAILED)
        {
            rx_len = ath9k_rx_frame(rx_buf, sizeof(rx_buf));
            if (rx_len >= 24 && mac80211_is_eapol(rx_buf, rx_len)) {
                uint8_t eapol_payload[256];
                int eplen = mac80211_parse_data(rx_buf, rx_len,
                                                eapol_payload, sizeof(eapol_payload));
                if (eplen > 2) {
                    uint8_t reply[256];
                    int reply_len = 0;
                    wpa_rx_eapol(eapol_payload + 2, eplen - 2, reply, &reply_len);
                    if (reply_len > 0) {
                        uint8_t eapol_frame[512];
                        int flen = mac80211_build_eapol(eapol_frame, sizeof(eapol_frame),
                                                        g_assoc_bssid, reply, reply_len,
                                                        g_seq++);
                        ath9k_tx_frame(eapol_frame, flen);
                    }
                }
            }
        }
        if (wpa_state() != WPA_STATE_CONNECTED) return WIFI_ERR_AUTH_FAILED;
    }

    return WIFI_OK;
}

void ath9k_disconnect(void) {
    mem_set(g_assoc_bssid, 0, 6);
    g_assoc_ssid[0] = '\0';
}

void ath9k_poll(void) {

    while (g_tx_head != g_tx_tail) {
        if (g_tx_desc[g_tx_head].ds_ctl1 != 0) {

            g_tx_desc[g_tx_head].ds_ctl1 = 0;
            g_tx_head = (g_tx_head + 1) % ATH9K_TX_DESCS;
        } else break;
    }
}

int ath9k_tx(const uint8_t *frame, uint16_t len) {

    if (g_assoc_security == WIFI_SECURITY_WPA2 &&
        wpa_state() == WPA_STATE_CONNECTED)
    {
        uint8_t enc_buf[ATH9K_BUF_SIZE];
        uint8_t pn[6];

        extern uint8_t g_tx_pn[6];

        for (int i = 5; i >= 0; i--) {
            if (++g_tx_pn[i]) break;
        }
        mem_copy(pn, g_tx_pn, 6);
        int enc_len = wpa_ccmp_encrypt(frame, len, enc_buf, pn,
                                       wpa_ptk_tk(), g_assoc_bssid, 0);
        return ath9k_tx_frame(enc_buf, enc_len);
    }
    return ath9k_tx_frame(frame, len);
}

int ath9k_rx(uint8_t *buf, uint16_t max_len) {
    uint8_t raw[ATH9K_BUF_SIZE];
    int raw_len = ath9k_rx_frame(raw, sizeof(raw));
    if (raw_len <= 0) return 0;

    if (g_assoc_security == WIFI_SECURITY_WPA2 &&
        wpa_state() == WPA_STATE_CONNECTED &&
        raw_len >= 24 && (mac80211_fc(raw) & IEEE80211_FC_PROTECTED))
    {

        const uint8_t *ccmp_hdr = raw + 24;
        uint8_t pn[6];
        pn[5] = ccmp_hdr[0]; pn[4] = ccmp_hdr[1];
        pn[3] = ccmp_hdr[4]; pn[2] = ccmp_hdr[5];
        pn[1] = ccmp_hdr[6]; pn[0] = ccmp_hdr[7];
        const uint8_t *ciphertext = raw + 24 + 8;
        int cipher_len = raw_len - 24 - 8;
        if (cipher_len <= 0 || cipher_len > (int)max_len) return 0;
        int plen = wpa_ccmp_decrypt(ciphertext, cipher_len, buf, pn,
                                    wpa_ptk_tk(), g_assoc_bssid, 0);
        return (plen > 0) ? plen : 0;
    }

    int payload_len = mac80211_parse_data(raw, raw_len, buf, max_len);
    return (payload_len > 0) ? payload_len : 0;
}

int ath9k_probe(void) {

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0x00);
            uint16_t vendor = (uint16_t)id;
            uint16_t device = (uint16_t)(id >> 16);
            if (vendor != ATH9K_VENDOR) continue;

            for (int j = 0; k_ath9k_ids[j]; j++) {
                if (k_ath9k_ids[j] == device) {

                    uint32_t bar0 = pci_read32(bus, dev, 0, 0x10) & ~0xf;
                    if (!bar0) continue;
                    g_mmio_base = bar0;

                    uint32_t cmd = pci_read32(bus, dev, 0, 0x04);
                    pci_write32(bus, dev, 0, 0x04, cmd | 0x06);

                    if (ath9k_hw_init()) {
                        g_initialized = 1;
                        return 1;
                    }
                    return 0;
                }
            }
        }
    }
    return 0;
}

const wifi_backend_ops_t ath9k_backend_ops = {
    WIFI_FAMILY_ATHEROS_AR9XXX,
    "ath9k",
    "",
    0,
    WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2,
    ath9k_scan,
    ath9k_associate,
    ath9k_disconnect,
    ath9k_poll,
    ath9k_tx,
    ath9k_rx,
};
