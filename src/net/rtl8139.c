#include "net/rtl8139.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/pci.h"
#include "lib/string.h"

/* ---- RTL8139 I/O register offsets ---- */
#define RTL_MAC0      0x00u
#define RTL_MAR0      0x08u
#define RTL_TXSTATUS0 0x10u   /* TX status, 4 registers (0x10,0x14,0x18,0x1C) */
#define RTL_TXADDR0   0x20u   /* TX buffer address, 4 registers */
#define RTL_RXBUF     0x30u
#define RTL_CHIPCMD   0x37u
#define RTL_RXBUFTAIL 0x38u
#define RTL_RXBUFHEAD 0x3Au
#define RTL_INTRMASK  0x3Cu
#define RTL_INTRSTATUS 0x3Eu
#define RTL_TXCONFIG  0x40u
#define RTL_RXCONFIG  0x44u
#define RTL_MPC       0x4Cu
#define RTL_CFG9346   0x52u
#define RTL_CONFIG1   0x52u   /* same offset as CFG9346 in some docs */
#define RTL_MEDIASTATUS 0x58u
#define RTL_BMCR      0x62u
#define RTL_BMSR      0x64u

/* CHIPCMD bits */
#define CMD_RESET     0x10u
#define CMD_RX_ENABLE 0x08u
#define CMD_TX_ENABLE 0x04u

/* TX status bits */
#define TX_OWN        0x2000u  /* 0 = NIC owns buffer (still sending) */
#define TX_TOK        0x8000u  /* TX OK */

/* RXCONFIG bits */
#define RX_ACCEPT_ALL_PHYS 0x01u
#define RX_ACCEPT_MCAST    0x04u
#define RX_ACCEPT_BCAST    0x08u
#define RX_WRAP            0x80u
#define RX_BUFLEN_32K      (0x01u << 11)
#define RX_FIFO_THRESH_NONE (0x07u << 13)

/* INTRSTATUS bits */
#define ISR_ROK  0x0001u  /* RX OK */
#define ISR_TOK  0x0004u  /* TX OK */
#define ISR_RER  0x0002u  /* RX error */

/* RX buffer header */
#define RX_HEADER_LEN 4   /* status(2) + length(2) */
#define RX_OK  0x0001u    /* RX status: OK */

/* ---- Static buffers ---- */
#define RX_BUF_SIZE (32768u + 16u + 1500u)  /* 32K + wrap + one extra frame */
#define TX_BUF_SIZE 1536u
#define TX_DESC_COUNT 4

static uint8_t  g_rx_buf[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t  g_tx_buf[TX_DESC_COUNT][TX_BUF_SIZE] __attribute__((aligned(4)));

static uint16_t g_iobase = 0;
static int      g_initialized = 0;
static uint16_t g_rx_pos = 0;   /* current read position in RX ring */
static int      g_tx_next = 0;  /* next TX descriptor to use */
static uint8_t  g_mac[6];

/* ---- PCI scan ---- */

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t iobase;
} rtl_pci_t;

static rtl_pci_t g_pci_found;
static int g_pci_ok = 0;

static int rtl_pci_visitor(const pci_device_t *d, void *ctx) {
    rtl_pci_t *out = (rtl_pci_t *)ctx;
    if (d->vendor_id == 0x10ECu && d->device_id == 0x8139u) {
        uint32_t bar0 = pci_read32(d->bus, d->dev, d->func, 0x10);
        if (bar0 & 1u) {  /* I/O BAR */
            out->bus    = d->bus;
            out->dev    = d->dev;
            out->func   = d->func;
            out->iobase = (uint16_t)(bar0 & 0xFFFCu);
            return 1;  /* stop enumeration */
        }
    }
    return 0;
}

/* ---- Register helpers ---- */
static inline uint8_t  r8 (uint16_t off) { return inb ((uint16_t)(g_iobase + off)); }
static inline uint16_t r16(uint16_t off) { return inw ((uint16_t)(g_iobase + off)); }
static inline uint32_t r32(uint16_t off) { return inl ((uint16_t)(g_iobase + off)); }
static inline void     w8 (uint16_t off, uint8_t  v) { outb((uint16_t)(g_iobase + off), v); }
static inline void     w16(uint16_t off, uint16_t v) { outw((uint16_t)(g_iobase + off), v); }
static inline void     w32(uint16_t off, uint32_t v) { outl((uint16_t)(g_iobase + off), v); }

static void spin_delay(uint32_t n) {
    volatile uint32_t i;
    for (i = 0; i < n; i++) (void)r8(RTL_CHIPCMD);
}

int rtl8139_init(void) {
    int i;
    uint8_t cmd;

    /* PCI scan for RTL8139 */
    g_pci_ok = 0;
    pci_enumerate(rtl_pci_visitor, &g_pci_found);
    if (!g_pci_found.iobase) return 0;
    g_pci_ok = 1;

    /* Enable PCI bus mastering and I/O space */
    pci_enable_busmaster(g_pci_found.bus, g_pci_found.dev, g_pci_found.func);

    g_iobase = g_pci_found.iobase;

    /* Power on: CONFIG1 = 0 */
    w8(RTL_CONFIG1, 0x00);
    spin_delay(1000);

    /* Software reset */
    w8(RTL_CHIPCMD, CMD_RESET);
    for (i = 0; i < 100000; i++) {
        if (!(r8(RTL_CHIPCMD) & CMD_RESET)) break;
        spin_delay(10);
    }

    /* Read MAC */
    for (i = 0; i < 6; i++) g_mac[i] = r8((uint16_t)(RTL_MAC0 + i));

    /* Set RX buffer */
    w32(RTL_RXBUF, (uint32_t)(uintptr_t)g_rx_buf);
    g_rx_pos = 0;

    /* Set TX buffer addresses */
    for (i = 0; i < TX_DESC_COUNT; i++) {
        w32((uint16_t)(RTL_TXADDR0 + i * 4), (uint32_t)(uintptr_t)g_tx_buf[i]);
    }
    g_tx_next = 0;

    /* Accept broadcast, multicast, own packets; 32K ring; no FIFO threshold */
    w32(RTL_RXCONFIG, RX_ACCEPT_ALL_PHYS | RX_ACCEPT_BCAST | RX_ACCEPT_MCAST |
                      RX_WRAP | RX_BUFLEN_32K | RX_FIFO_THRESH_NONE);

    /* TX config: max DMA burst 2048, no loopback */
    w32(RTL_TXCONFIG, (0x06u << 8) | (0x03u << 24));

    /* Enable TX+RX */
    cmd = r8(RTL_CHIPCMD);
    w8(RTL_CHIPCMD, (uint8_t)(cmd | CMD_RX_ENABLE | CMD_TX_ENABLE));

    /* Disable all interrupts (we poll) */
    w16(RTL_INTRMASK, 0x0000);
    w16(RTL_INTRSTATUS, 0xFFFF);  /* clear pending */

    /* Accept all multicast */
    w32(RTL_MAR0, 0xFFFFFFFFu);
    w32((uint16_t)(RTL_MAR0 + 4), 0xFFFFFFFFu);

    g_initialized = 1;
    return 1;
}

void rtl8139_get_mac(uint8_t *mac) {
    int i;
    for (i = 0; i < 6; i++) mac[i] = g_mac[i];
}

int rtl8139_send(const uint8_t *buf, uint16_t len) {
    uint32_t sts;
    int i;

    if (!g_initialized) return -1;
    if (len < 14 || len > TX_BUF_SIZE) return -1;

    /* Wait for this TX descriptor to be free */
    for (i = 0; i < 100000; i++) {
        sts = r32((uint16_t)(RTL_TXSTATUS0 + g_tx_next * 4));
        if (!(sts & TX_OWN)) break;  /* NIC doesn't own → free */
        spin_delay(10);
    }

    mem_copy(g_tx_buf[g_tx_next], buf, len);
    if (len < 60) {
        /* Pad to minimum Ethernet frame size */
        mem_set(g_tx_buf[g_tx_next] + len, 0, (uint32_t)(60 - len));
        len = 60;
    }

    /* Write TX status: size in bits 0-12, bit 13 = 0 to start TX */
    w32((uint16_t)(RTL_TXSTATUS0 + g_tx_next * 4), (uint32_t)len);

    g_tx_next = (g_tx_next + 1) & (TX_DESC_COUNT - 1);
    return 0;
}

int rtl8139_recv(uint8_t *buf, uint16_t max_len) {
    uint16_t isr;
    uint16_t rx_head;
    uint16_t status;
    uint16_t pkt_len;
    uint16_t copy_len;
    uint8_t *pkt;

    if (!g_initialized) return 0;

    /* Clear any ISR bits (don't use ISR as gate — some emulators don't set
     * ISR when INTRMASK=0, which would make us miss every received packet) */
    isr = r16(RTL_INTRSTATUS);
    w16(RTL_INTRSTATUS, isr);

    if (isr & ISR_RER) {
        /* RX error: reset tail pointer */
        g_rx_pos = r16(RTL_RXBUFHEAD);
        return 0;
    }

    /* Poll ring buffer directly */
    rx_head = r16(RTL_RXBUFHEAD);
    if (g_rx_pos == rx_head) return 0;

    /* Packet header: 2 bytes status + 2 bytes length */
    pkt = g_rx_buf + g_rx_pos;
    status  = (uint16_t)(pkt[0] | ((uint16_t)pkt[1] << 8));
    pkt_len = (uint16_t)(pkt[2] | ((uint16_t)pkt[3] << 8));

    if (!(status & RX_OK) || pkt_len < 14 || pkt_len > 1514) {
        /* Bad packet: skip */
        g_rx_pos = (uint16_t)((g_rx_pos + pkt_len + RX_HEADER_LEN + 3) & ~3u);
        g_rx_pos &= 0x7FFFu;
        w16(RTL_RXBUFTAIL, (uint16_t)(g_rx_pos - 16u));
        return 0;
    }

    /* Subtract 4-byte CRC from length */
    copy_len = (uint16_t)(pkt_len - 4);
    if (copy_len > max_len) copy_len = max_len;

    mem_copy(buf, pkt + RX_HEADER_LEN, copy_len);

    /* Advance read pointer (4-byte aligned) */
    g_rx_pos = (uint16_t)((g_rx_pos + pkt_len + RX_HEADER_LEN + 3) & ~3u);
    g_rx_pos &= 0x7FFFu;
    w16(RTL_RXBUFTAIL, (uint16_t)(g_rx_pos - 16u));

    return (int)copy_len;
}
