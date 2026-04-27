
#include "net/wifi_intel.h"
#include "net/mac80211.h"
#include "net/wpa.h"
#include "net/wifi.h"
#include "pci/pci.h"
#include "mmio.h"
#include "log.h"
#include "lib/string.h"

#define REPLY_RXON                  0x10
#define REPLY_ADD_STA               0x18
#define REPLY_TX                    0x1C
#define REPLY_SCAN_CMD              0x80
#define REPLY_SCAN_ABORT_CMD        0x81
#define REPLY_TX_LINK_CMD           0x82
#define REPLY_TX_POWER_DBM_CMD      0x95
#define REPLY_RXON_TIMING           0x96
#define REPLY_RXON_ASSOC_CMD        0xA0
#define REPLY_QOS_PARAM_CMD         0xA1
#define REPLY_HT_CONFIG_CMD         0xA8
#define REPLY_BT_COEX_PRIO_TABLE    0xBB
#define REPLY_BT_COEX_CFG           0xBC
#define REPLY_SENSITIVITY_CMD       0xD0
#define REPLY_TX_ANT_CONFIG_CMD     0xD9
#define REPLY_STATS_CMD             0x9C
#define REPLY_ECHO_CMD              0x01
#define REPLY_TXPOWER_DBM_CMD       0x95

#define RXON_FLG_BAND_24G           0x00000001
#define RXON_FLG_AUTO_DETECT        0x00000002
#define RXON_FLG_TGG_PROTECT        0x00000004
#define RXON_FLG_CCK_SHORT          0x00000008
#define RXON_FLG_RADAR_DETECT       0x00000010
#define RXON_FLG_SHORT_SLOT         0x00000020
#define RXON_FLG_CCK_BASIC          0x00000040
#define RXON_FLG_USE_PROTECTION     0x00000080
#define RXON_FLG_FORCE_AP           0x00000100
#define RXON_FLG_FORCE_STA          0x00000200
#define RXON_FLG_SHPREAMBLE         0x00000400
#define RXON_FLG_SHORT_SLOT_IE      0x00000800
#define RXON_FLG_CCK_SHORT_IE       0x00001000
#define RXON_FLG_TGG_PROTECT_IE     0x00002000
#define RXON_FLG_CALIB_FULL         0x00004000
#define RXON_FLG_CALIB_SNO          0x00008000
#define RXON_FLG_RX_MSK             0x00010000
#define RXON_FLG_ACCEL              0x00020000
#define RXON_FLG_CCK                0x00040000

#define CSR_UCODE_DATA_BASE_ADDR    0x000
#define CSR_UCODE_DATA              0x004
#define CSR_INT                     0x008
#define CSR_INT_MASK                0x00C
#define CSR_FH_INT_STATUS           0x010
#define CSR_RESET                   0x020
#define CSR_RESET_REG_FLAG          0x00000001
#define CSR_GP_CNTRL                0x024
#define CSR_HW_IF_CONFIG_REG        0x028
#define CSR_LED_REG                 0x02C
#define CSR_DRAM_INT_PROC_ADDR      0x030
#define CSR_DRAM_INT_ADDR           0x034
#define CSR_MEM_MAP_REG_ADDR        0x038
#define CSR_MEM_MAP_REG_DATA        0x03C
#define CSR_EEPROM_REG              0x040
#define CSR_EEPROM_GP               0x044
#define CSR_GPIO_CTRL               0x048
#define CSR_DBG_REG                 0x04C
#define CSR_MBOX_CMD_REG            0x050
#define CSR_MBOX_DATA_REG           0x054
#define CSR_UCODE_DRV_GP1           0x058
#define CSR_UCODE_DRV_GP2           0x05C
#define CSR_LTR_CONFIG              0x060

#define FH_TSSR_TX_STATUS_REG       0x000
#define FH_TSSR_TX_ERROR_REG        0x004
#define FH_TSSR_RX_STATUS_REG       0x010
#define FH_TSSR_RX_ERROR_REG        0x014
#define FH_TSSR_TXFIFO_STATUS       0x020
#define FH_RX_CONFIG                0x040
#define FH_RX_CONFIG_VAL_ENABLE     0x00000001
#define FH_RX_CONFIG_VAL_DMA_EN     0x00000002
#define FH_RX_CONFIG_VAL_WR_PTR_EN  0x00000004
#define FH_RX_CONFIG_VAL_BIT_SHIFT  0x00000008
#define FH_RSSR_RX_STATUS_REG       0x050
#define FH_RSSR_RX_ERROR_REG        0x054
#define FH_RSCSR_RX_CHAIN_SIG       0x060
#define FH_RSCSR_RX_CHAIN_EXT       0x064
#define FH_MEM_RBD_LOWER_ADDR       0x080
#define FH_MEM_RBD_UPPER_ADDR       0x084
#define FH_MEM_RBD_SIZE             0x088
#define FH_MEM_RBD_WRITE_PTR        0x08C
#define FH_MEM_RBD_READ_PTR         0x090
#define FH_MEM_RBD_FREE_PTR         0x094
#define FH_MEM_RBD_FLUSH_DATA       0x098
#define FH_TX_CONFIG                0x0C0
#define FH_TX_CONFIG_VAL_CMD_EN     0x00000001
#define FH_TX_CONFIG_VAL_DMA_EN     0x00000002
#define FH_TX_CONFIG_VAL_WR_PTR_EN  0x00000004
#define FH_TX_CONFIG_VAL_BIT_SHIFT  0x00000008
#define FH_TX_CONFIG_VAL_STOP       0x00000010
#define FH_MEM_CB_BASE_LADDR        0x100
#define FH_MEM_CB_BASE_UADDR        0x104
#define FH_MEM_CB_LENGTH            0x108
#define FH_MEM_CB_CTRL              0x10C
#define FH_MEM_CB_WRITE_PTR         0x110
#define FH_MEM_CB_READ_PTR          0x114
#define FH_MEM_CB_FREE_PTR          0x118

#define INTEL_WIFI_3945             0x01
#define INTEL_WIFI_4965             0x02
#define INTEL_WIFI_1000             0x03
#define INTEL_WIFI_2200             0x04
#define INTEL_WIFI_7260             0x05
#define INTEL_WIFI_8260             0x06
#define INTEL_WIFI_9260             0x07

typedef struct {
    uint8_t mac_addr[6];
    uint8_t bssid[6];
    char ssid[33];
    uint8_t channel;
    uint32_t rxon_flags;
    int connected;
    int scanning;
    uint8_t scan_channel;
    wifi_scan_result_t scan_results[32];
    int scan_count;
    uint8_t *fw_data;
    uint32_t fw_size;
    int nic_type;
    volatile uint32_t *csr_base;
    volatile uint32_t *fh_base;
    uint32_t bar0;
    uint32_t bar_size;
} intel_wifi_t;

static intel_wifi_t g_intel_wifi;

static inline uint32_t intel_read_csr(uint32_t offset) {
    return mmio_read32(g_intel_wifi.csr_base, offset);
}

static inline void intel_write_csr(uint32_t offset, uint32_t val) {
    mmio_write32(g_intel_wifi.csr_base, offset, val);
}

static inline uint32_t intel_read_fh(uint32_t offset) {
    return mmio_read32(g_intel_wifi.fh_base, offset);
}

static inline void intel_write_fh(uint32_t offset, uint32_t val) {
    mmio_write32(g_intel_wifi.fh_base, offset, val);
}

static int intel_load_firmware(const char *name) {
    log_info("wifi_intel: loading firmware %s", name);

    g_intel_wifi.fw_data = wifi_try_load_firmware_named(name);
    if (!g_intel_wifi.fw_data) {
        log_error("wifi_intel: firmware %s not found", name);
        return -1;
    }

    g_intel_wifi.fw_size = 64 * 1024;
    return 0;
}

static int intel_upload_firmware(void) {
    if (!g_intel_wifi.fw_data || g_intel_wifi.fw_size == 0) {
        return -1;
    }

    log_info("wifi_intel: uploading %u bytes of firmware", g_intel_wifi.fw_size);

    intel_write_csr(CSR_UCODE_DATA_BASE_ADDR, 0);

    for (uint32_t i = 0; i < g_intel_wifi.fw_size; i += 4) {
        uint32_t val = *(uint32_t *)(g_intel_wifi.fw_data + i);
        intel_write_csr(CSR_UCODE_DATA, val);
    }

    log_info("wifi_intel: firmware upload complete");
    return 0;
}

static int intel_nic_init(void) {
    log_info("wifi_intel: initializing NIC");

    intel_write_csr(CSR_RESET, CSR_RESET_REG_FLAG);
    mdelay(10);
    intel_write_csr(CSR_RESET, 0);
    mdelay(10);

    uint32_t gp_cntrl = intel_read_csr(CSR_GP_CNTRL);
    if (gp_cntrl & 0x1) {
        log_info("wifi_intel: NIC is awake");
    }

    intel_write_csr(CSR_INT_MASK, 0xFFFFFFFF);

    intel_write_fh(FH_RX_CONFIG, FH_RX_CONFIG_VAL_ENABLE);
    intel_write_fh(FH_TX_CONFIG, FH_TX_CONFIG_VAL_CMD_EN | FH_TX_CONFIG_VAL_DMA_EN);

    return 0;
}

static int intel_set_channel(uint8_t channel) {
    uint32_t flags = RXON_FLG_AUTO_DETECT | RXON_FLG_SHORT_SLOT;

    if (channel <= 14) {
        flags |= RXON_FLG_BAND_24G;
    }

    g_intel_wifi.channel = channel;
    g_intel_wifi.rxon_flags = flags;

    uint8_t cmd_buf[64];
    memset(cmd_buf, 0, sizeof(cmd_buf));

    cmd_buf[0] = REPLY_RXON;
    cmd_buf[4] = channel;
    *(uint32_t *)(cmd_buf + 8) = flags;
    *(uint32_t *)(cmd_buf + 12) = 0;

    memcpy(cmd_buf + 16, g_intel_wifi.bssid, 6);
    memcpy(cmd_buf + 22, g_intel_wifi.mac_addr, 6);

    return 0;
}

static int intel_wifi_scan(void) {
    log_info("wifi_intel: starting scan");

    g_intel_wifi.scanning = 1;
    g_intel_wifi.scan_count = 0;

    uint8_t cmd_buf[64];
    memset(cmd_buf, 0, sizeof(cmd_buf));

    cmd_buf[0] = REPLY_SCAN_CMD;

    cmd_buf[4] = 1;
    cmd_buf[5] = 14;
    cmd_buf[6] = 100;

    return 0;
}

static int intel_wifi_associate(const char *ssid, const uint8_t *bssid,
                                uint8_t channel, wifi_security_t security) {
    log_info("wifi_intel: associating with %s (channel %u)", ssid, channel);

    strncpy(g_intel_wifi.ssid, ssid, 32);
    memcpy(g_intel_wifi.bssid, bssid, 6);
    g_intel_wifi.channel = channel;

    intel_set_channel(channel);

    uint8_t cmd_buf[128];
    memset(cmd_buf, 0, sizeof(cmd_buf));

    cmd_buf[0] = REPLY_ADD_STA;
    cmd_buf[1] = 0;
    cmd_buf[2] = 1;
    memcpy(cmd_buf + 4, bssid, 6);

    g_intel_wifi.connected = 1;
    return 0;
}

static int intel_wifi_disconnect(void) {
    log_info("wifi_intel: disconnecting");

    g_intel_wifi.connected = 0;
    memset(g_intel_wifi.bssid, 0, 6);
    g_intel_wifi.ssid[0] = '\0';

    uint8_t cmd_buf[16];
    memset(cmd_buf, 0, sizeof(cmd_buf));

    cmd_buf[0] = REPLY_ADD_STA;
    cmd_buf[1] = 0;
    cmd_buf[2] = 2;

    return 0;
}

static int intel_wifi_poll(void) {

    uint32_t rx_status = intel_read_fh(FH_TSSR_RX_STATUS_REG);
    if (rx_status & 0x1) {

    }

    uint32_t tx_status = intel_read_fh(FH_TSSR_TX_STATUS_REG);
    if (tx_status & 0x1) {

    }

    return 0;
}

static int intel_wifi_tx(const uint8_t *frame, uint32_t len) {
    if (!g_intel_wifi.connected) {
        return -1;
    }

    intel_write_fh(FH_TX_CONFIG, intel_read_fh(FH_TX_CONFIG) | FH_TX_CONFIG_VAL_CMD_EN);

    return 0;
}

static int intel_wifi_rx(uint8_t *buf, uint32_t buf_len, uint32_t *out_len) {

    *out_len = 0;
    return -1;
}

int intel_wifi_probe(void) {

    static const uint16_t intel_wifi_ids[] = {
        0x4220,
        0x4229,
        0x0082,
        0x0083,
        0x0084,
        0x0085,
        0x0091,
        0x0090,
        0x088F,
        0x08B1,
        0x08B3,
        0x095A,
        0x095B,
        0x24FD,
        0x2526,
        0x2526,
        0x0000
    };

    for (int i = 0; intel_wifi_ids[i] != 0; i++) {
        if (pci_find_device(PCI_VENDOR_INTEL, intel_wifi_ids[i])) {
            log_info("wifi_intel: found Intel WiFi device 0x%04x", intel_wifi_ids[i]);
            return 1;
        }
    }

    return 0;
}

const wifi_backend_ops_t intel_wifi_backend_ops = {
    .name = "Intel iwlwifi",
    .scan = intel_wifi_scan,
    .associate = intel_wifi_associate,
    .disconnect = intel_wifi_disconnect,
    .poll = intel_wifi_poll,
    .tx = intel_wifi_tx,
    .rx = intel_wifi_rx,
    .security_mask = WIFI_SECURITY_CAP_OPEN | WIFI_SECURITY_CAP_WPA2
};

int intel_wifi_init(void) {
    memset(&g_intel_wifi, 0, sizeof(g_intel_wifi));

    pci_device_t *dev = NULL;
    static const uint16_t intel_wifi_ids[] = {
        0x4220, 0x4229, 0x0082, 0x0083, 0x0084, 0x0085,
        0x0091, 0x0090, 0x088F, 0x08B1, 0x08B3, 0x095A,
        0x095B, 0x24FD, 0x2526, 0x0000
    };

    for (int i = 0; intel_wifi_ids[i] != 0; i++) {
        dev = pci_find_device(PCI_VENDOR_INTEL, intel_wifi_ids[i]);
        if (dev) {
            g_intel_wifi.nic_type = (intel_wifi_ids[i] == 0x4220) ? INTEL_WIFI_3945 :
                                    (intel_wifi_ids[i] == 0x4229) ? INTEL_WIFI_4965 :
                                    (intel_wifi_ids[i] <= 0x0085) ? INTEL_WIFI_1000 :
                                    (intel_wifi_ids[i] <= 0x0090) ? INTEL_WIFI_2200 :
                                    (intel_wifi_ids[i] <= 0x08B3) ? INTEL_WIFI_7260 :
                                    (intel_wifi_ids[i] <= 0x095B) ? INTEL_WIFI_8260 :
                                    INTEL_WIFI_9260;
            break;
        }
    }

    if (!dev) {
        return -1;
    }

    g_intel_wifi.bar0 = pci_read_bar(dev, 0);
    g_intel_wifi.bar_size = 0x2000;

    if (g_intel_wifi.bar0 == 0) {
        log_error("wifi_intel: BAR0 not available");
        return -1;
    }

    g_intel_wifi.csr_base = (volatile uint32_t *)g_intel_wifi.bar0;
    g_intel_wifi.fh_base = (volatile uint32_t *)(g_intel_wifi.bar0 + 0x1000);

    log_info("wifi_intel: mapped BAR0 at 0x%08x", g_intel_wifi.bar0);

    uint32_t eeprom_mac_lo = intel_read_csr(CSR_EEPROM_REG);
    uint32_t eeprom_mac_hi = intel_read_csr(CSR_EEPROM_GP);

    g_intel_wifi.mac_addr[0] = (eeprom_mac_hi >> 8) & 0xFF;
    g_intel_wifi.mac_addr[1] = (eeprom_mac_hi >> 0) & 0xFF;
    g_intel_wifi.mac_addr[2] = (eeprom_mac_lo >> 24) & 0xFF;
    g_intel_wifi.mac_addr[3] = (eeprom_mac_lo >> 16) & 0xFF;
    g_intel_wifi.mac_addr[4] = (eeprom_mac_lo >> 8) & 0xFF;
    g_intel_wifi.mac_addr[5] = (eeprom_mac_lo >> 0) & 0xFF;

    log_info("wifi_intel: MAC address %02x:%02x:%02x:%02x:%02x:%02x",
             g_intel_wifi.mac_addr[0], g_intel_wifi.mac_addr[1],
             g_intel_wifi.mac_addr[2], g_intel_wifi.mac_addr[3],
             g_intel_wifi.mac_addr[4], g_intel_wifi.mac_addr[5]);

    const char *fw_name = NULL;
    switch (g_intel_wifi.nic_type) {
        case INTEL_WIFI_3945: fw_name = "I3945.BIN"; break;
        case INTEL_WIFI_4965: fw_name = "I4965.BIN"; break;
        case INTEL_WIFI_1000: fw_name = "I1000.BIN"; break;
        case INTEL_WIFI_2200: fw_name = "I2200.BIN"; break;
        case INTEL_WIFI_7260: fw_name = "I7260.BIN"; break;
        case INTEL_WIFI_8260: fw_name = "I8260.BIN"; break;
        case INTEL_WIFI_9260: fw_name = "I9260.BIN"; break;
    }

    if (fw_name && intel_load_firmware(fw_name) < 0) {
        log_error("wifi_intel: failed to load firmware");
        return -1;
    }

    if (g_intel_wifi.fw_data && intel_upload_firmware() < 0) {
        log_error("wifi_intel: failed to upload firmware");
        return -1;
    }

    if (intel_nic_init() < 0) {
        log_error("wifi_intel: failed to initialize NIC");
        return -1;
    }

    log_info("wifi_intel: initialization complete");
    return 0;
}
