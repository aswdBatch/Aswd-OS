
#include "net/wifi_bcm43xx.h"
#include "net/mac80211.h"
#include "net/wpa.h"
#include "net/wifi.h"
#include "drivers/pci.h"
#include "lib/string.h"

#define BCM43XX_VENDOR  0x14e4

#define BCM43xx_MMIO_CORE_CTL       0x0008
#define BCM43xx_MMIO_STATUS         0x0120
#define BCM43xx_MMIO_MACCTL         0x0120
#define BCM43xx_MMIO_GEN_IRQ_REASON 0x0008
#define BCM43xx_MMIO_GEN_IRQ_MASK   0x000c
#define BCM43xx_MMIO_RAM_CONTROL    0x0130
#define BCM43xx_MMIO_RAM_DATA       0x0134
#define BCM43xx_MMIO_CHANNEL        0x03f0
#define BCM43xx_MMIO_TXCTL          0x0210
#define BCM43xx_MMIO_RXCTL          0x0230
#define BCM43xx_MMIO_RXRING         0x0234

#define BCM43xx_MACCTL_ENABLED      (1<<0)
#define BCM43xx_MACCTL_PSM_RUN      (1<<1)
#define BCM43xx_MACCTL_INFRA        (1<<8)

#define BCM43xx_BUF_SIZE  2048
#define BCM43xx_TX_DESCS  8
#define BCM43xx_RX_DESCS  8
#define BCM43xx_MAX_SCAN  32

typedef struct {
    volatile uint32_t addr;
    volatile uint32_t ctrl;
} bcm43xx_dma_desc_t;

static uint8_t  g_initialized = 0;
static uint32_t g_mmio_base   = 0;
static bcm43xx_dma_desc_t g_tx_ring[BCM43xx_TX_DESCS];
static bcm43xx_dma_desc_t g_rx_ring[BCM43xx_RX_DESCS];
static uint8_t g_tx_buf[BCM43xx_TX_DESCS][BCM43xx_BUF_SIZE];
static uint8_t g_rx_buf[BCM43xx_RX_DESCS][BCM43xx_BUF_SIZE];
static int g_tx_head, g_tx_tail, g_rx_head;

static mac80211_network_t g_scan_nets[BCM43xx_MAX_SCAN];
static int g_scan_count;
static uint8_t  g_assoc_bssid[6];
static char     g_assoc_ssid[33];
static uint8_t  g_assoc_security;
static uint16_t g_seq;

#define BCM43xx_FW_MAX  65536
static uint8_t g_fw_buf[BCM43xx_FW_MAX];
static int     g_fw_len = 0;

static uint32_t reg_read(uint32_t off) {
    volatile uint32_t *p = (volatile uint32_t *)(g_mmio_base + off);
    return *p;
}
static void reg_write(uint32_t off, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)(g_mmio_base + off);
    *p = val;
}

static void bcm43xx_upload_firmware(void) {
    reg_write(BCM43xx_MMIO_CORE_CTL, 0);
    for (volatile int d=0; d<100000; d++) {}

    int words = g_fw_len / 4;
    for (int i = 0; i < words; i++) {
        uint32_t word = ((uint32_t)g_fw_buf[i*4+0]      ) |
                        ((uint32_t)g_fw_buf[i*4+1] <<  8) |
                        ((uint32_t)g_fw_buf[i*4+2] << 16) |
                        ((uint32_t)g_fw_buf[i*4+3] << 24);
        reg_write(BCM43xx_MMIO_RAM_CONTROL, (uint32_t)i);
        reg_write(BCM43xx_MMIO_RAM_DATA, word);
    }
}

static void bcm43xx_set_channel(int chan) {
    uint32_t freq = (uint32_t)(2407 + chan * 5);
    reg_write(BCM43xx_MMIO_CHANNEL, freq);
    for (volatile int d=0; d<50000; d++) {}
}

static int bcm43xx_hw_init(void) {

    reg_write(BCM43xx_MMIO_CORE_CTL, 0);
    for (volatile int d=0; d<200000; d++) {}

    if (g_fw_len > 0) bcm43xx_upload_firmware();

    uint32_t mac_lo = reg_read(0x0180);
    uint32_t mac_hi = reg_read(0x0184);
    if (mac_lo || mac_hi) {
        g_sta_mac[0] = (uint8_t)(mac_lo >> 24);
        g_sta_mac[1] = (uint8_t)(mac_lo >> 16);
        g_sta_mac[2] = (uint8_t)(mac_lo >> 8);
        g_sta_mac[3] = (uint8_t)mac_lo;
        g_sta_mac[4] = (uint8_t)(mac_hi >> 8);
        g_sta_mac[5] = (uint8_t)mac_hi;
    }

    int i;
    for (i=0;i<BCM43xx_TX_DESCS;i++) {
        g_tx_ring[i].addr = (uint32_t)(uintptr_t)g_tx_buf[i];
        g_tx_ring[i].ctrl = 0;
    }
    for (i=0;i<BCM43xx_RX_DESCS;i++) {
        g_rx_ring[i].addr = (uint32_t)(uintptr_t)g_rx_buf[i];
        g_rx_ring[i].ctrl = BCM43xx_BUF_SIZE;
    }
    g_tx_head=g_tx_tail=g_rx_head=0;

    reg_write(BCM43xx_MMIO_RXRING, (uint32_t)(uintptr_t)g_rx_ring);
    reg_write(BCM43xx_MMIO_GEN_IRQ_MASK, 0);
    reg_write(BCM43xx_MMIO_MACCTL, BCM43xx_MACCTL_ENABLED | BCM43xx_MACCTL_INFRA);

    bcm43xx_set_channel(6);
    return 1;
}

static int bcm43xx_tx_frame(const uint8_t *frame, int len) {
    if (!g_initialized || len > BCM43xx_BUF_SIZE) return -1;
    int slot = g_tx_tail;
    if (g_tx_ring[slot].ctrl & 0x80000000) return -1;
    mem_copy(g_tx_buf[slot], frame, len);
    g_tx_ring[slot].ctrl = (uint32_t)len | 0x80000000;
    reg_write(BCM43xx_MMIO_TXCTL, 1);
    g_tx_tail = (g_tx_tail+1)%BCM43xx_TX_DESCS;
    return 0;
}

static int bcm43xx_rx_frame(uint8_t *buf, int max_len) {
    bcm43xx_dma_desc_t *d = &g_rx_ring[g_rx_head];
    if (d->ctrl & 0x80000000) return 0;
    int len = (int)(d->ctrl & 0xfff);
    if (len <= 0 || len > max_len) { d->ctrl = BCM43xx_BUF_SIZE; return 0; }
    mem_copy(buf, g_rx_buf[g_rx_head], len);
    d->ctrl = BCM43xx_BUF_SIZE;
    g_rx_head = (g_rx_head+1)%BCM43xx_RX_DESCS;
    return len;
}

int bcm43xx_scan(wifi_network_t *out, int max_entries) {
    int found=0;
    uint8_t probe[256], rx_buf[BCM43xx_BUF_SIZE];
    uint8_t bcast[6]={0xff,0xff,0xff,0xff,0xff,0xff};
    g_scan_count=0;
    for (int ch=1;ch<=13&&found<max_entries&&found<BCM43xx_MAX_SCAN;ch++) {
        bcm43xx_set_channel(ch);
        int plen=mac80211_build_probe_req(probe,sizeof(probe),bcast,bcast,g_seq++);
        bcm43xx_tx_frame(probe,plen);
        for (int w=0;w<5000000;w++) {
            int rx_len=bcm43xx_rx_frame(rx_buf,sizeof(rx_buf));
            if (rx_len>=24) {
                mac80211_network_t net; mem_set(&net,0,sizeof(net));
                if (mac80211_parse_beacon(rx_buf,rx_len,&net)||
                    mac80211_parse_probe_resp(rx_buf,rx_len,&net)) {
                    if (!net.ssid_len) continue;
                    int dup=0;
                    for (int k=0;k<found;k++) if(str_ncmp(g_scan_nets[k].ssid,net.ssid,33)==0){dup=1;break;}
                    if (!dup&&found<BCM43xx_MAX_SCAN) {
                        g_scan_nets[found]=net;
                        str_copy(out[found].ssid,net.ssid,33);
                        out[found].signal_pct=60; out[found].connectable=1;
                        out[found].security=(net.has_rsn||net.has_wpa)?WIFI_SECURITY_WPA2:WIFI_SECURITY_OPEN;
                        found++;
                    }
                }
            }
        }
    }
    g_scan_count=found; return found;
}

int bcm43xx_associate(const char *ssid, wifi_security_t security, const char *passphrase) {
    uint8_t auth_frame[64],assoc_frame[256],rx_buf[BCM43xx_BUF_SIZE];
    int net_idx=-1,retries;
    for (int i=0;i<g_scan_count;i++) if(str_ncmp(g_scan_nets[i].ssid,ssid,33)==0){net_idx=i;break;}
    if (net_idx<0) return WIFI_ERR_SCAN_FAILED;
    mem_copy(g_assoc_bssid,g_scan_nets[net_idx].bssid,6);
    str_copy(g_assoc_ssid,ssid,33); g_assoc_security=(uint8_t)security;
    bcm43xx_set_channel(g_scan_nets[net_idx].channel?g_scan_nets[net_idx].channel:6);
    int len=mac80211_build_auth_req(auth_frame,sizeof(auth_frame),g_assoc_bssid,g_seq++);
    bcm43xx_tx_frame(auth_frame,len);
    retries=2000000;
    while (retries-->0){int rx_len=bcm43xx_rx_frame(rx_buf,sizeof(rx_buf));
        if(rx_len>=24&&mac80211_frame_for_us(rx_buf,rx_len)){int s=mac80211_parse_auth_resp(rx_buf,rx_len);if(s==0)break;if(s>0)return WIFI_ERR_AUTH_FAILED;}}
    if(retries<=0)return WIFI_ERR_TIMEOUT;
    uint8_t has_rsn=(security==WIFI_SECURITY_WPA2)?1:0;
    len=mac80211_build_assoc_req(assoc_frame,sizeof(assoc_frame),ssid,g_assoc_bssid,g_seq++,has_rsn);
    bcm43xx_tx_frame(assoc_frame,len);
    retries=2000000;
    while (retries-->0){int rx_len=bcm43xx_rx_frame(rx_buf,sizeof(rx_buf));
        if(rx_len>=24&&mac80211_frame_for_us(rx_buf,rx_len)){int s=mac80211_parse_assoc_resp(rx_buf,rx_len);if(s==0)break;if(s>0)return WIFI_ERR_ASSOC_FAILED;}}
    if(retries<=0)return WIFI_ERR_TIMEOUT;
    if(security==WIFI_SECURITY_WPA2&&passphrase&&passphrase[0]){
        wpa_init(ssid,passphrase,g_assoc_bssid);
        retries=5000000;
        while(retries-->0&&wpa_state()!=WPA_STATE_CONNECTED&&wpa_state()!=WPA_STATE_FAILED){
            int rx_len=bcm43xx_rx_frame(rx_buf,sizeof(rx_buf));
            if(rx_len>=24&&mac80211_is_eapol(rx_buf,rx_len)){
                uint8_t ep[256];int eplen=mac80211_parse_data(rx_buf,rx_len,ep,sizeof(ep));
                if(eplen>2){uint8_t reply[256];int rl=0;wpa_rx_eapol(ep+2,eplen-2,reply,&rl);
                    if(rl>0){uint8_t ef[512];int flen=mac80211_build_eapol(ef,sizeof(ef),g_assoc_bssid,reply,rl,g_seq++);bcm43xx_tx_frame(ef,flen);}
                }
            }
        }
        if(wpa_state()!=WPA_STATE_CONNECTED)return WIFI_ERR_AUTH_FAILED;
    }
    return WIFI_OK;
}

void bcm43xx_disconnect(void){mem_set(g_assoc_bssid,0,6);g_assoc_ssid[0]='\0';}
void bcm43xx_poll(void){}

int bcm43xx_tx(const uint8_t *frame,uint16_t len){
    if(g_assoc_security==WIFI_SECURITY_WPA2&&wpa_state()==WPA_STATE_CONNECTED){
        uint8_t enc[BCM43xx_BUF_SIZE],pn[6];
        extern uint8_t g_tx_pn[6];
        for(int i=5;i>=0;i--)if(++g_tx_pn[i])break;
        mem_copy(pn,g_tx_pn,6);
        int elen=wpa_ccmp_encrypt(frame,len,enc,pn,wpa_ptk_tk(),g_assoc_bssid,0);
        return bcm43xx_tx_frame(enc,elen);
    }
    return bcm43xx_tx_frame(frame,len);
}

int bcm43xx_rx(uint8_t *buf,uint16_t max_len){
    uint8_t raw[BCM43xx_BUF_SIZE];
    int raw_len=bcm43xx_rx_frame(raw,sizeof(raw));
    if(raw_len<=0)return 0;
    if(g_assoc_security==WIFI_SECURITY_WPA2&&wpa_state()==WPA_STATE_CONNECTED&&
       raw_len>=24&&(mac80211_fc(raw)&IEEE80211_FC_PROTECTED)){
        const uint8_t *ch=raw+24;uint8_t pn[6];
        pn[5]=ch[0];pn[4]=ch[1];pn[3]=ch[4];pn[2]=ch[5];pn[1]=ch[6];pn[0]=ch[7];
        int pl=wpa_ccmp_decrypt(raw+32,raw_len-32,buf,pn,wpa_ptk_tk(),g_assoc_bssid,0);
        return pl>0?pl:0;
    }
    int pl=mac80211_parse_data(raw,raw_len,buf,max_len);
    return pl>0?pl:0;
}

void bcm43xx_set_firmware(const uint8_t *data, int len) {
    if (len > BCM43xx_FW_MAX) len = BCM43xx_FW_MAX;
    mem_copy(g_fw_buf, data, len);
    g_fw_len = len;
}

int bcm43xx_probe(void) {
    for (int bus=0;bus<256;bus++) {
        for (int dev=0;dev<32;dev++) {
            uint32_t id=pci_read32(bus,dev,0,0x00);
            if ((uint16_t)id!=BCM43XX_VENDOR) continue;
            uint32_t bar0=pci_read32(bus,dev,0,0x10)&~0xf;
            if (!bar0) continue;

            uint32_t cls=pci_read32(bus,dev,0,0x08)>>8;
            if ((cls&0xffff00)>>8 != 0x0280) continue;
            g_mmio_base=bar0;
            uint32_t cmd=pci_read32(bus,dev,0,0x04);
            pci_write32(bus,dev,0,0x04,cmd|0x06);
            if(bcm43xx_hw_init()){g_initialized=1;return 1;}
            return 0;
        }
    }
    return 0;
}

const wifi_backend_ops_t bcm43xx_backend_ops = {
    WIFI_FAMILY_BROADCOM_BCM43XX,
    "bcm43xx",
    "BCM43.BIN",
    1,
    WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2,
    bcm43xx_scan,
    bcm43xx_associate,
    bcm43xx_disconnect,
    bcm43xx_poll,
    bcm43xx_tx,
    bcm43xx_rx,
};

const wifi_backend_ops_t bcm432x_backend_ops = {
    WIFI_FAMILY_BROADCOM_BCM432X,
    "bcm432x",
    "BCM432X.BIN",
    1,
    WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2,
    bcm43xx_scan,
    bcm43xx_associate,
    bcm43xx_disconnect,
    bcm43xx_poll,
    bcm43xx_tx,
    bcm43xx_rx,
};
