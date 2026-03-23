#include "net/ip.h"

#include <stdint.h>

#include "net/arp.h"
#include "net/ethernet.h"
#include "net/icmp.h"
#include "net/net.h"
#include "net/tcp.h"
#include "net/udp.h"
#include "lib/string.h"

static uint16_t g_ip_id = 0;

uint16_t ip_checksum(const void *data, uint16_t len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    uint16_t n = len;

    while (n > 1) {
        sum += *p++;
        n -= 2;
    }
    if (n) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

int ip_send(const uint8_t *dst_ip, uint8_t proto,
            uint8_t *packet_buf, uint16_t payload_len) {
    const net_info_t *ni = net_get_info();
    uint8_t  dst_mac[6];
    uint8_t  next_hop[4];
    ip_header_t *iph;
    eth_header_t *eth;
    uint16_t total_len;

    /* Determine next hop: same subnet → direct, else via gateway */
    int i;
    int same = 1;
    for (i = 0; i < 4; i++) {
        if ((dst_ip[i] & ni->netmask[i]) != (ni->ip[i] & ni->netmask[i])) {
            same = 0; break;
        }
    }
    if (same) mem_copy(next_hop, dst_ip, 4);
    else      mem_copy(next_hop, ni->gateway, 4);

    /* Check for broadcast */
    int is_bcast = (dst_ip[0]==255 && dst_ip[1]==255 && dst_ip[2]==255 && dst_ip[3]==255);
    int is_bcast_subnet = (dst_ip[3] == 255);

    if (is_bcast || is_bcast_subnet) {
        eth_broadcast(dst_mac);
    } else if (!arp_resolve(next_hop, dst_mac)) {
        /* ARP request sent; packet dropped for now (caller can retry) */
        return -1;
    }

    /* Fill Ethernet header */
    eth = (eth_header_t *)packet_buf;
    eth_addr_copy(eth->dst, dst_mac);
    eth_addr_copy(eth->src, ni->mac);
    eth->ethertype = bswap16(ETHERTYPE_IP);

    /* Fill IP header */
    total_len = (uint16_t)(IP_HEADER_LEN + payload_len);
    iph = (ip_header_t *)(packet_buf + ETH_HEADER_LEN);
    iph->ver_ihl   = 0x45;
    iph->tos       = 0;
    iph->total_len = bswap16(total_len);
    iph->id        = bswap16(g_ip_id++);
    iph->frag_off  = 0;
    iph->ttl       = 64;
    iph->proto     = proto;
    iph->checksum  = 0;
    mem_copy(iph->src, ni->ip, 4);
    mem_copy(iph->dst, dst_ip, 4);
    iph->checksum = ip_checksum(iph, IP_HEADER_LEN);

    return net_send_frame(packet_buf, (uint16_t)(ETH_HEADER_LEN + total_len));
}

void ip_rx(const uint8_t *pkt, uint16_t len) {
    const ip_header_t *iph;
    uint8_t  ihl;
    uint16_t total;
    const uint8_t *payload;
    uint16_t pay_len;

    if (len < IP_HEADER_LEN) return;
    iph = (const ip_header_t *)pkt;

    if ((iph->ver_ihl >> 4) != 4) return;  /* only IPv4 */
    ihl = (uint8_t)((iph->ver_ihl & 0x0F) * 4);
    if (ihl < IP_HEADER_LEN || ihl > len) return;

    total = bswap16(iph->total_len);
    if (total > len) total = len;
    if (total < ihl) return;

    payload = pkt + ihl;
    pay_len = (uint16_t)(total - ihl);

    /* Update ARP cache with sender */
    arp_update(iph->src, 0);   /* 0 MAC = skip (handled by ARP layer at eth level) */

    switch (iph->proto) {
    case IP_PROTO_ICMP:
        icmp_rx(iph->src, payload, pay_len);
        break;
    case IP_PROTO_UDP:
        udp_rx(iph->src, payload, pay_len);
        break;
    case IP_PROTO_TCP:
        tcp_rx(iph->src, payload, pay_len);
        break;
    }
}
