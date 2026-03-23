#include "net/arp.h"

#include <stdint.h>

#include "net/ethernet.h"
#include "net/net.h"
#include "lib/string.h"

#define ARP_CACHE_SIZE 8

/* ARP hardware/protocol type constants */
#define ARP_HTYPE_ETH  0x0001u
#define ARP_PTYPE_IP   0x0800u
#define ARP_OP_REQUEST 0x0001u
#define ARP_OP_REPLY   0x0002u

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t op;
    uint8_t  sha[6];   /* sender hardware addr */
    uint8_t  spa[4];   /* sender protocol addr */
    uint8_t  tha[6];   /* target hardware addr */
    uint8_t  tpa[4];   /* target protocol addr */
} arp_pkt_t;

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    int     valid;
} arp_entry_t;

static arp_entry_t g_cache[ARP_CACHE_SIZE];
static int g_cache_next = 0;

static int ip_eq(const uint8_t *a, const uint8_t *b) {
    return a[0]==b[0] && a[1]==b[1] && a[2]==b[2] && a[3]==b[3];
}

void arp_update(const uint8_t *ip, const uint8_t *mac) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_cache[i].valid && ip_eq(g_cache[i].ip, ip)) {
            eth_addr_copy(g_cache[i].mac, mac);
            return;
        }
    }
    /* Add new entry */
    g_cache[g_cache_next].valid = 1;
    mem_copy(g_cache[g_cache_next].ip, ip, 4);
    eth_addr_copy(g_cache[g_cache_next].mac, mac);
    g_cache_next = (g_cache_next + 1) % ARP_CACHE_SIZE;
}

static void arp_send_request(const uint8_t *target_ip) {
    uint8_t frame[60];
    eth_header_t *eh = (eth_header_t *)frame;
    arp_pkt_t    *ap = (arp_pkt_t *)(frame + ETH_HEADER_LEN);
    const net_info_t *ni = net_get_info();

    eth_broadcast(eh->dst);
    eth_addr_copy(eh->src, ni->mac);
    eh->ethertype = bswap16(ETHERTYPE_ARP);

    ap->htype = bswap16(ARP_HTYPE_ETH);
    ap->ptype = bswap16(ARP_PTYPE_IP);
    ap->hlen  = 6;
    ap->plen  = 4;
    ap->op    = bswap16(ARP_OP_REQUEST);
    eth_addr_copy(ap->sha, ni->mac);
    mem_copy(ap->spa, ni->ip, 4);
    mem_set(ap->tha, 0, 6);
    mem_copy(ap->tpa, target_ip, 4);

    net_send_frame(frame, (uint16_t)sizeof(frame));
}

int arp_resolve(const uint8_t *ip, uint8_t *mac_out) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_cache[i].valid && ip_eq(g_cache[i].ip, ip)) {
            eth_addr_copy(mac_out, g_cache[i].mac);
            return 1;
        }
    }
    /* Not found: send ARP request */
    arp_send_request(ip);
    return 0;
}

void arp_rx(const uint8_t *pkt, uint16_t len) {
    const arp_pkt_t *ap;
    const net_info_t *ni;
    uint8_t frame[60];
    eth_header_t *eh;
    arp_pkt_t    *rep;

    if (len < (uint16_t)sizeof(arp_pkt_t)) return;
    ap = (const arp_pkt_t *)pkt;

    if (bswap16(ap->htype) != ARP_HTYPE_ETH) return;
    if (bswap16(ap->ptype) != ARP_PTYPE_IP)  return;

    /* Update cache for sender */
    arp_update(ap->spa, ap->sha);

    ni = net_get_info();

    if (bswap16(ap->op) == ARP_OP_REQUEST) {
        /* Is it asking for our IP? */
        if (!ip_eq(ap->tpa, ni->ip)) return;

        /* Send reply */
        eh  = (eth_header_t *)frame;
        rep = (arp_pkt_t *)(frame + ETH_HEADER_LEN);

        eth_addr_copy(eh->dst, ap->sha);
        eth_addr_copy(eh->src, ni->mac);
        eh->ethertype = bswap16(ETHERTYPE_ARP);

        rep->htype = bswap16(ARP_HTYPE_ETH);
        rep->ptype = bswap16(ARP_PTYPE_IP);
        rep->hlen  = 6;
        rep->plen  = 4;
        rep->op    = bswap16(ARP_OP_REPLY);
        eth_addr_copy(rep->sha, ni->mac);
        mem_copy(rep->spa, ni->ip, 4);
        eth_addr_copy(rep->tha, ap->sha);
        mem_copy(rep->tpa, ap->spa, 4);

        net_send_frame(frame, (uint16_t)sizeof(frame));
    }
}
