#include "net/rtl8168.h"

#include <stdint.h>

#include "drivers/pci.h"
#include "lib/string.h"

/*
 * RTL8168/8111 (PCIe Gigabit) driver — uses MMIO, descriptor rings.
 *
 * Register offsets (MMIO).
 */
#define R8_IDR0      0x00u  /* MAC bytes 0-5 */
#define R8_MAR0      0x08u  /* Multicast filter */
#define R8_TNPDS     0x20u  /* TX normal priority descriptor start (low 32) */
#define R8_TNPDS_HI  0x24u
#define R8_THPDS     0x28u  /* TX high priority (low 32) */
#define R8_THPDS_HI  0x2Cu
#define R8_CHIPCMD   0x37u
#define R8_TPPOLL    0x38u  /* TX poll demand */
#define R8_INTRMASK  0x3Cu
#define R8_INTRSTATUS 0x3Eu
#define R8_TXCONFIG  0x40u
#define R8_RXCONFIG  0x44u
#define R8_RDSAR     0xE4u  /* RX descriptor start (low 32) */
#define R8_RDSAR_HI  0xE8u
#define R8_MAXTXPKTSIZE 0xECu
#define R8_CCR       0xE0u
#define R8_CFG1      0x52u
#define R8_CFG2      0x53u
#define R8_PHYSTATUS 0x6Cu

/* CHIPCMD */
#define R8_CMD_RESET  0x10u
#define R8_CMD_RXEN   0x08u
#define R8_CMD_TXEN   0x04u

/* TX/RX descriptor flags (upper 16 bits of opts1) */
#define DESC_OWN    (1u << 31)   /* 1 = NIC owns this descriptor */
#define DESC_EOR    (1u << 30)   /* End of ring */
#define DESC_FS     (1u << 29)   /* First segment */
#define DESC_LS     (1u << 28)   /* Last segment */

/* TPPOLL */
#define TPPOLL_NPQ  0x40u   /* Normal priority poll */

/* INTRSTATUS bits */
#define ISR_ROK  0x0001u
#define ISR_TOK  0x0004u

#define TX_DESC_COUNT  4
#define RX_DESC_COUNT  4
#define TX_BUF_SIZE    1536u
#define RX_BUF_SIZE    1536u

/* TX/RX descriptor (128-bit / 16 bytes) */
typedef struct __attribute__((packed,aligned(256))) {
    uint32_t opts1;   /* flags + length in lower 16 bits */
    uint32_t opts2;
    uint32_t buf_lo;
    uint32_t buf_hi;
} rtl8168_desc_t;

static rtl8168_desc_t g_tx_ring[TX_DESC_COUNT] __attribute__((aligned(256)));
static rtl8168_desc_t g_rx_ring[RX_DESC_COUNT] __attribute__((aligned(256)));
static uint8_t g_tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t g_rx_buf[RX_DESC_COUNT][RX_BUF_SIZE] __attribute__((aligned(4)));

static volatile uint8_t *g_mmio = 0;
static int      g_initialized = 0;
static int      g_tx_next = 0;
static int      g_rx_next = 0;
static uint8_t  g_mac[6];

/* ---- PCI scan ---- */
typedef struct { uint8_t bus, dev, func; uint32_t bar2; } r8168_pci_t;
static r8168_pci_t g_pci;
static int g_pci_found = 0;

static int r8168_visitor(const pci_device_t *d, void *ctx) {
    r8168_pci_t *out = (r8168_pci_t *)ctx;
    if (d->vendor_id == 0x10ECu && d->device_id == 0x8168u) {
        uint32_t mmio = 0;
        /* On real hardware RTL8111/8168 uses a 64-bit MMIO BAR at BAR1
         * (config offset 0x14 = low 32, 0x18 = high 32 bits, which is 0
         * for physical addresses < 4 GB — so reading 0x18 as "BAR2" gives
         * 0 and the device appears missing). Try BAR1 first. */
        uint32_t bar1 = pci_read32(d->bus, d->dev, d->func, 0x14);
        if (!(bar1 & 1u) && (bar1 & 0xFFFFFFF0u)) {
            mmio = bar1 & 0xFFFFFFF0u;
        } else {
            uint32_t bar2 = pci_read32(d->bus, d->dev, d->func, 0x18);
            if (!(bar2 & 1u) && (bar2 & 0xFFFFFFF0u)) {
                mmio = bar2 & 0xFFFFFFF0u;
            }
        }
        if (mmio) {
            out->bus  = d->bus;
            out->dev  = d->dev;
            out->func = d->func;
            out->bar2 = mmio;
            return 1;
        }
    }
    return 0;
}

/* ---- MMIO helpers ---- */
static inline uint8_t  mmr8 (uint32_t off) { return *(volatile uint8_t *)(g_mmio + off); }
static inline uint16_t mmr16(uint32_t off) { return *(volatile uint16_t *)(g_mmio + off); }
static inline uint32_t mmr32(uint32_t off) { return *(volatile uint32_t *)(g_mmio + off); }
static inline void mmw8 (uint32_t off, uint8_t  v) { *(volatile uint8_t *)(g_mmio + off)  = v; }
static inline void mmw16(uint32_t off, uint16_t v) { *(volatile uint16_t *)(g_mmio + off) = v; }
static inline void mmw32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_mmio + off) = v; }

static void r8168_spin(uint32_t n) {
    volatile uint32_t i;
    for (i = 0; i < n; i++) (void)mmr8(R8_CHIPCMD);
}

int rtl8168_init(void) {
    int i;

    g_pci_found = 0;
    mem_set(&g_pci, 0, sizeof(g_pci));
    pci_enumerate(r8168_visitor, &g_pci);
    if (!g_pci.bar2) return 0;
    g_pci_found = 1;

    /* Enable PCI bus mastering + memory space access */
    pci_enable_busmaster(g_pci.bus, g_pci.dev, g_pci.func);

    g_mmio = (volatile uint8_t *)(uintptr_t)g_pci.bar2;

    /* Unlock config registers */
    mmw8(0x50, 0xC0);   /* CFG9346: unlock */

    /* Software reset */
    mmw8(R8_CHIPCMD, R8_CMD_RESET);
    for (i = 0; i < 200000; i++) {
        if (!(mmr8(R8_CHIPCMD) & R8_CMD_RESET)) break;
        r8168_spin(10);
    }

    /* Read MAC */
    for (i = 0; i < 6; i++) g_mac[i] = mmr8((uint32_t)(R8_IDR0 + i));

    /* Set up TX ring */
    for (i = 0; i < TX_DESC_COUNT; i++) {
        g_tx_ring[i].opts1  = (i == TX_DESC_COUNT - 1) ? DESC_EOR : 0u;
        g_tx_ring[i].opts2  = 0;
        g_tx_ring[i].buf_lo = (uint32_t)(uintptr_t)g_tx_buf[i];
        g_tx_ring[i].buf_hi = 0;
    }
    g_tx_next = 0;

    /* Set up RX ring */
    for (i = 0; i < RX_DESC_COUNT; i++) {
        g_rx_ring[i].opts1  = DESC_OWN | RX_BUF_SIZE |
                              ((i == RX_DESC_COUNT - 1) ? DESC_EOR : 0u);
        g_rx_ring[i].opts2  = 0;
        g_rx_ring[i].buf_lo = (uint32_t)(uintptr_t)g_rx_buf[i];
        g_rx_ring[i].buf_hi = 0;
    }
    g_rx_next = 0;

    /* Write descriptor ring addresses */
    mmw32(R8_TNPDS,    (uint32_t)(uintptr_t)g_tx_ring);
    mmw32(R8_TNPDS_HI, 0);
    mmw32(R8_RDSAR,    (uint32_t)(uintptr_t)g_rx_ring);
    mmw32(R8_RDSAR_HI, 0);

    /* RX config: accept all, max RX size 1536 */
    mmw32(R8_RXCONFIG, 0x0000E70Fu);   /* RXFTH=unlimited, MXDMA=unlimited, AAP|APM|AM|AB */

    /* TX config */
    mmw32(R8_TXCONFIG, 0x03000700u);

    /* Max TX packet size */
    mmw16(R8_MAXTXPKTSIZE, 0x3B9u);

    /* Enable TX+RX */
    mmw8(R8_CHIPCMD, R8_CMD_RXEN | R8_CMD_TXEN);

    /* Lock config */
    mmw8(0x50, 0x00);

    /* Accept all multicast */
    mmw32(R8_MAR0, 0xFFFFFFFFu);
    mmw32(R8_MAR0 + 4, 0xFFFFFFFFu);

    /* Disable interrupts (we poll) */
    mmw16(R8_INTRMASK, 0x0000);
    mmw16(R8_INTRSTATUS, 0xFFFF);

    g_initialized = 1;
    return 1;
}

void rtl8168_get_mac(uint8_t *mac) {
    int i;
    for (i = 0; i < 6; i++) mac[i] = g_mac[i];
}

int rtl8168_send(const uint8_t *buf, uint16_t len) {
    rtl8168_desc_t *desc;
    int i;

    if (!g_initialized) return -1;
    if (len < 14 || len > TX_BUF_SIZE) return -1;

    desc = &g_tx_ring[g_tx_next];

    /* Wait for NIC to release descriptor */
    for (i = 0; i < 200000; i++) {
        if (!(desc->opts1 & DESC_OWN)) break;
        r8168_spin(10);
    }

    mem_copy(g_tx_buf[g_tx_next], buf, len);
    if (len < 60) {
        mem_set(g_tx_buf[g_tx_next] + len, 0, (uint32_t)(60 - len));
        len = 60;
    }

    uint32_t eor = (g_tx_next == TX_DESC_COUNT - 1) ? DESC_EOR : 0u;
    desc->opts1 = DESC_OWN | DESC_FS | DESC_LS | eor | (uint32_t)len;
    desc->opts2 = 0;

    /* Poll TX */
    mmw8(R8_TPPOLL, TPPOLL_NPQ);

    g_tx_next = (g_tx_next + 1) % TX_DESC_COUNT;
    return 0;
}

int rtl8168_recv(uint8_t *buf, uint16_t max_len) {
    rtl8168_desc_t *desc;
    uint32_t opts1;
    uint16_t pkt_len;

    if (!g_initialized) return 0;

    /* Clear interrupt status */
    mmw16(R8_INTRSTATUS, mmr16(R8_INTRSTATUS));

    desc = &g_rx_ring[g_rx_next];
    opts1 = desc->opts1;
    if (opts1 & DESC_OWN) return 0;  /* NIC still owns → no packet yet */

    pkt_len = (uint16_t)(opts1 & 0x3FFFu);
    if (pkt_len > 4) pkt_len -= 4;  /* strip CRC */
    if (pkt_len > max_len) pkt_len = max_len;

    mem_copy(buf, g_rx_buf[g_rx_next], pkt_len);

    /* Return descriptor to NIC */
    uint32_t eor = (g_rx_next == RX_DESC_COUNT - 1) ? DESC_EOR : 0u;
    desc->opts1 = DESC_OWN | eor | RX_BUF_SIZE;
    desc->opts2 = 0;

    g_rx_next = (g_rx_next + 1) % RX_DESC_COUNT;
    return (int)pkt_len;
}
