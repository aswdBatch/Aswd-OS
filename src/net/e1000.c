#include "net/e1000.h"

#include <stdint.h>

#include "drivers/pci.h"
#include "lib/string.h"

/* ---- e1000 MMIO register offsets ---- */
#define E1000_CTRL    0x0000u
#define E1000_STATUS  0x0008u
#define E1000_EECD    0x0010u
#define E1000_EERD    0x0014u
#define E1000_MDIC    0x0020u
#define E1000_ICR     0x00C0u
#define E1000_IMS     0x00D0u
#define E1000_IMC     0x00D8u
#define E1000_RCTL    0x0100u
#define E1000_TCTL    0x0400u
#define E1000_TIPG    0x0410u
#define E1000_RDBAL   0x2800u
#define E1000_RDBAH   0x2804u
#define E1000_RDLEN   0x2808u
#define E1000_RDH     0x2810u
#define E1000_RDT     0x2818u
#define E1000_TDBAL   0x3800u
#define E1000_TDBAH   0x3804u
#define E1000_TDLEN   0x3808u
#define E1000_TDH     0x3810u
#define E1000_TDT     0x3818u
#define E1000_RAL0    0x5400u
#define E1000_RAH0    0x5404u
#define E1000_MTA     0x5200u

/* CTRL bits */
#define CTRL_SLU      (1u << 6)   /* Set Link Up */
#define CTRL_RST      (1u << 26)  /* Reset */
#define CTRL_PHY_RST  (1u << 31)

/* RCTL bits */
#define RCTL_EN       (1u << 1)
#define RCTL_BAM      (1u << 15)  /* Broadcast accept */
#define RCTL_SECRC    (1u << 26)  /* Strip CRC */
#define RCTL_BSIZE_2K (0u << 16)

/* TCTL bits */
#define TCTL_EN       (1u << 1)
#define TCTL_PSP      (1u << 3)   /* Pad short packets */
#define TCTL_CT_SHIFT 4
#define TCTL_COLD_SHIFT 12

/* TX/RX descriptor structures */
typedef struct __attribute__((packed,aligned(16))) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct __attribute__((packed,aligned(16))) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

/* TX cmd bits */
#define TX_CMD_EOP  0x01u
#define TX_CMD_IFCS 0x02u
#define TX_CMD_RS   0x08u

/* TX/RX status bits */
#define TX_STA_DD   0x01u  /* Descriptor Done */
#define RX_STA_DD   0x01u
#define RX_STA_EOP  0x02u

#define TX_DESC_COUNT  8
#define RX_DESC_COUNT  8
#define TX_BUF_SIZE    2048u
#define RX_BUF_SIZE    2048u

static e1000_tx_desc_t g_tx_ring[TX_DESC_COUNT] __attribute__((aligned(16)));
static e1000_rx_desc_t g_rx_ring[RX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t g_tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t g_rx_buf[RX_DESC_COUNT][RX_BUF_SIZE] __attribute__((aligned(4)));

static volatile uint8_t *g_mmio = 0;
static int g_initialized = 0;
static int g_tx_next = 0;
static int g_rx_next = 0;
static uint8_t g_mac[6];

typedef struct { uint8_t bus, dev, func; uint32_t bar0; } e1k_pci_t;
static e1k_pci_t g_pci;
static int g_pci_found = 0;

/* Known e1000 device IDs */
static const uint16_t e1k_devids[] = {
    0x100E,  /* 82540EM (QEMU default) */
    0x100F,  /* 82545EM copper */
    0x1011,  /* 82545EM fiber */
    0x1026,  /* 82545GM copper */
    0x1076,  /* 82541GI copper */
    0x107C,  /* 82541PI */
    0x10D3,  /* 82574L (e1000e family) */
    0,
};

static int e1k_visitor(const pci_device_t *d, void *ctx) {
    e1k_pci_t *out = (e1k_pci_t *)ctx;
    int i;
    if (d->vendor_id != 0x8086u) return 0;
    for (i = 0; e1k_devids[i]; i++) {
        if (d->device_id == e1k_devids[i]) {
            uint32_t bar0 = pci_read32(d->bus, d->dev, d->func, 0x10);
            if (!(bar0 & 1u)) {  /* memory BAR */
                out->bus  = d->bus;
                out->dev  = d->dev;
                out->func = d->func;
                out->bar0 = bar0 & 0xFFFFFFF0u;
                return 1;
            }
        }
    }
    return 0;
}

static inline uint32_t er32(uint32_t off) { return *(volatile uint32_t *)(g_mmio + off); }
static inline void     ew32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_mmio + off) = v; }

static void e1k_spin(uint32_t n) {
    volatile uint32_t i;
    for (i = 0; i < n; i++) (void)er32(E1000_STATUS);
}

/* Read MAC from EEPROM word 0 (using EERD) */
static void read_mac_eeprom(void) {
    uint32_t eerd;
    int i;
    /* Try EEPROM read for first 3 words (6 bytes) */
    for (i = 0; i < 3; i++) {
        ew32(E1000_EERD, (uint32_t)((i << 8) | 0x01u));  /* EERD: addr | start */
        int j;
        for (j = 0; j < 100000; j++) {
            eerd = er32(E1000_EERD);
            if (eerd & 0x10u) break;  /* done bit */
            e1k_spin(10);
        }
        uint16_t word = (uint16_t)(eerd >> 16);
        g_mac[i * 2]     = (uint8_t)(word & 0xFF);
        g_mac[i * 2 + 1] = (uint8_t)(word >> 8);
    }
}

int e1000_init(void) {
    int i;

    g_pci_found = 0;
    pci_enumerate(e1k_visitor, &g_pci);
    if (!g_pci.bar0) return 0;
    g_pci_found = 1;

    /* Enable PCI bus mastering + memory space access */
    pci_enable_busmaster(g_pci.bus, g_pci.dev, g_pci.func);

    g_mmio = (volatile uint8_t *)(uintptr_t)g_pci.bar0;

    /* Reset */
    ew32(E1000_IMC, 0xFFFFFFFFu);   /* mask all interrupts */
    ew32(E1000_CTRL, CTRL_RST | CTRL_PHY_RST);
    e1k_spin(500000);

    /* Wait for reset to complete */
    for (i = 0; i < 200000; i++) {
        if (!(er32(E1000_CTRL) & CTRL_RST)) break;
        e1k_spin(10);
    }

    ew32(E1000_IMC, 0xFFFFFFFFu);

    /* Set link up */
    ew32(E1000_CTRL, er32(E1000_CTRL) | CTRL_SLU);

    /* Read MAC */
    read_mac_eeprom();

    /* Write MAC to RAL/RAH */
    uint32_t ral = (uint32_t)g_mac[0] | ((uint32_t)g_mac[1] << 8) |
                   ((uint32_t)g_mac[2] << 16) | ((uint32_t)g_mac[3] << 24);
    uint32_t rah = (uint32_t)g_mac[4] | ((uint32_t)g_mac[5] << 8) | (1u << 31);
    ew32(E1000_RAL0, ral);
    ew32(E1000_RAH0, rah);

    /* Clear MTA */
    for (i = 0; i < 128; i++) ew32((uint32_t)(E1000_MTA + i * 4), 0);

    /* Set up TX ring */
    for (i = 0; i < TX_DESC_COUNT; i++) {
        g_tx_ring[i].addr   = (uint64_t)(uintptr_t)g_tx_buf[i];
        g_tx_ring[i].status = TX_STA_DD;  /* pre-mark as done */
    }
    ew32(E1000_TDBAL, (uint32_t)(uintptr_t)g_tx_ring);
    ew32(E1000_TDBAH, 0);
    ew32(E1000_TDLEN, TX_DESC_COUNT * 16u);
    ew32(E1000_TDH, 0);
    ew32(E1000_TDT, 0);
    g_tx_next = 0;

    /* Set up RX ring */
    for (i = 0; i < RX_DESC_COUNT; i++) {
        g_rx_ring[i].addr   = (uint64_t)(uintptr_t)g_rx_buf[i];
        g_rx_ring[i].status = 0;
    }
    ew32(E1000_RDBAL, (uint32_t)(uintptr_t)g_rx_ring);
    ew32(E1000_RDBAH, 0);
    ew32(E1000_RDLEN, RX_DESC_COUNT * 16u);
    ew32(E1000_RDH, 0);
    ew32(E1000_RDT, (uint32_t)(RX_DESC_COUNT - 1));
    g_rx_next = 0;

    /* TX config */
    ew32(E1000_TCTL, TCTL_EN | TCTL_PSP |
         (0x0Fu << TCTL_CT_SHIFT) | (0x40u << TCTL_COLD_SHIFT));
    ew32(E1000_TIPG, 0x0060200Au);

    /* RX config: enable, broadcast, strip CRC */
    ew32(E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);

    /* Clear interrupt status */
    (void)er32(E1000_ICR);
    ew32(E1000_IMC, 0xFFFFFFFFu);

    g_initialized = 1;
    return 1;
}

void e1000_get_mac(uint8_t *mac) {
    int i;
    for (i = 0; i < 6; i++) mac[i] = g_mac[i];
}

int e1000_send(const uint8_t *buf, uint16_t len) {
    e1000_tx_desc_t *desc;
    int i;

    if (!g_initialized) return -1;
    if (len < 14 || len > TX_BUF_SIZE) return -1;

    desc = &g_tx_ring[g_tx_next];

    /* Wait for descriptor to be free */
    for (i = 0; i < 200000; i++) {
        if (desc->status & TX_STA_DD) break;
        e1k_spin(10);
    }

    mem_copy(g_tx_buf[g_tx_next], buf, len);
    desc->length = len;
    desc->cmd    = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    desc->status = 0;

    g_tx_next = (g_tx_next + 1) % TX_DESC_COUNT;
    ew32(E1000_TDT, (uint32_t)g_tx_next);
    return 0;
}

int e1000_recv(uint8_t *buf, uint16_t max_len) {
    e1000_rx_desc_t *desc;
    uint16_t pkt_len;

    if (!g_initialized) return 0;

    /* Clear ICR */
    (void)er32(E1000_ICR);

    desc = &g_rx_ring[g_rx_next];
    if (!(desc->status & RX_STA_DD)) return 0;

    pkt_len = desc->length;
    if (pkt_len > max_len) pkt_len = max_len;
    mem_copy(buf, g_rx_buf[g_rx_next], pkt_len);

    /* Return descriptor to hardware */
    desc->status = 0;
    ew32(E1000_RDT, (uint32_t)g_rx_next);

    g_rx_next = (g_rx_next + 1) % RX_DESC_COUNT;
    return (int)pkt_len;
}
