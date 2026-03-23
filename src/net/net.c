#include "net/net.h"

#include <stdint.h>

#include "drivers/pci.h"
#include "drivers/serial.h"
#include "net/arp.h"
#include "net/dhcp.h"
#include "net/e1000.h"
#include "net/ethernet.h"
#include "net/ip.h"
#include "net/rtl8139.h"
#include "net/rtl8168.h"
#include "lib/string.h"

/* Log all network-class PCI devices via serial for diagnostics */
static void serial_hex16(uint16_t v) {
    const char *h = "0123456789ABCDEF";
    char buf[5];
    buf[0] = h[(v >> 12) & 0xF];
    buf[1] = h[(v >>  8) & 0xF];
    buf[2] = h[(v >>  4) & 0xF];
    buf[3] = h[(v >>  0) & 0xF];
    buf[4] = '\0';
    serial_write(buf);
}

static int net_diag_visitor(const pci_device_t *d, void *ctx) {
    (void)ctx;
    if (d->class_code == 0x02u) {  /* Network Controller */
        serial_write("[net] PCI NIC: ");
        serial_hex16(d->vendor_id);
        serial_write(":");
        serial_hex16(d->device_id);
        serial_write(" class=02 sub=");
        serial_hex16(d->subclass);
        serial_write("\n");
    }
    return 0;  /* keep scanning */
}

/* ---- NIC abstraction ---- */
typedef enum {
    NIC_NONE = 0,
    NIC_RTL8139,
    NIC_RTL8168,
    NIC_E1000,
} nic_kind_t;

static nic_kind_t g_nic = NIC_NONE;
static net_info_t g_info;

/* ---- RX frame buffer ---- */
static uint8_t g_rx_frame[ETH_MAX_FRAME];

/* ---- Internal: apply DHCP result (called by dhcp.c) ---- */
void net_apply_dhcp(const uint8_t *ip, const uint8_t *mask,
                    const uint8_t *gw, const uint8_t *dns) {
    mem_copy(g_info.ip,      ip,   4);
    mem_copy(g_info.netmask, mask, 4);
    mem_copy(g_info.gateway, gw,   4);
    mem_copy(g_info.dns,     dns,  4);
    g_info.dhcp_pending = 0;

    serial_write("[net] DHCP bound: ");
    int i;
    for (i = 0; i < 4; i++) {
        char tmp[4];
        uint8_t v = ip[i];
        int pos = 0;
        if (v >= 100) { tmp[pos++] = (char)('0' + v/100); v %= 100; }
        if (v >= 10 || pos) { tmp[pos++] = (char)('0' + v/10); v %= 10; }
        tmp[pos++] = (char)('0' + v);
        tmp[pos] = '\0';
        serial_write(tmp);
        if (i < 3) serial_write(".");
    }
    serial_write("\n");
}

void net_init(void) {
    mem_set(&g_info, 0, sizeof(g_info));

    /* Scan and log all network-class PCI devices so serial output can
     * reveal what NIC is present if none of our drivers claim it */
    pci_enumerate(net_diag_visitor, 0);

    /* Try NICs in order: RTL8139, RTL8168, e1000 */
    if (rtl8139_init()) {
        g_nic = NIC_RTL8139;
        rtl8139_get_mac(g_info.mac);
        str_copy(g_info.nic_name, "RTL8139", NET_NIC_NAME_MAX);
    } else if (rtl8168_init()) {
        g_nic = NIC_RTL8168;
        rtl8168_get_mac(g_info.mac);
        str_copy(g_info.nic_name, "RTL8168", NET_NIC_NAME_MAX);
    } else if (e1000_init()) {
        g_nic = NIC_E1000;
        e1000_get_mac(g_info.mac);
        str_copy(g_info.nic_name, "e1000", NET_NIC_NAME_MAX);
    } else {
        g_info.nic_name[0] = '\0';
        serial_write("[net] No NIC found\n");
        return;
    }

    g_info.link_up = 1;
    serial_write("[net] NIC: ");
    serial_write(g_info.nic_name);
    serial_write("\n");

    /* Start DHCP */
    g_info.dhcp_pending = 1;
    dhcp_start();
}

void net_poll(void) {
    int n;

    if (g_nic == NIC_NONE) return;

    /* Receive all pending frames */
    while (1) {
        if (g_nic == NIC_RTL8139)     n = rtl8139_recv(g_rx_frame, ETH_MAX_FRAME);
        else if (g_nic == NIC_RTL8168) n = rtl8168_recv(g_rx_frame, ETH_MAX_FRAME);
        else                           n = e1000_recv(g_rx_frame, ETH_MAX_FRAME);

        if (n < ETH_HEADER_LEN) break;

        eth_header_t *eh = (eth_header_t *)g_rx_frame;
        uint16_t etype = bswap16(eh->ethertype);
        const uint8_t *payload = g_rx_frame + ETH_HEADER_LEN;
        uint16_t pay_len = (uint16_t)(n - ETH_HEADER_LEN);

        if (etype == ETHERTYPE_ARP) {
            arp_rx(payload, pay_len);
        } else if (etype == ETHERTYPE_IP) {
            ip_rx(payload, pay_len);
        }
    }

    dhcp_poll();
}

const net_info_t *net_get_info(void) {
    return &g_info;
}

int net_send_frame(const uint8_t *frame, uint16_t len) {
    if (g_nic == NIC_NONE) return -1;
    if (g_nic == NIC_RTL8139)     return rtl8139_send(frame, len);
    if (g_nic == NIC_RTL8168)     return rtl8168_send(frame, len);
    return e1000_send(frame, len);
}

int net_recv_frame(uint8_t *buf, uint16_t max_len) {
    if (g_nic == NIC_NONE) return 0;
    if (g_nic == NIC_RTL8139)     return rtl8139_recv(buf, max_len);
    if (g_nic == NIC_RTL8168)     return rtl8168_recv(buf, max_len);
    return e1000_recv(buf, max_len);
}
