#include "net/udp.h"

#include <stdint.h>

#include "net/ethernet.h"
#include "net/ip.h"
#include "net/net.h"
#include "lib/string.h"

#define UDP_HANDLER_MAX 4

typedef struct {
    uint16_t    port;
    udp_recv_fn fn;
    void       *ctx;
    int         active;
} udp_handler_t;

static udp_handler_t g_handlers[UDP_HANDLER_MAX];

void udp_register(uint16_t port, udp_recv_fn fn, void *ctx) {
    int i;
    for (i = 0; i < UDP_HANDLER_MAX; i++) {
        if (!g_handlers[i].active) {
            g_handlers[i].port   = port;
            g_handlers[i].fn     = fn;
            g_handlers[i].ctx    = ctx;
            g_handlers[i].active = 1;
            return;
        }
    }
}

void udp_unregister(uint16_t port) {
    int i;
    for (i = 0; i < UDP_HANDLER_MAX; i++) {
        if (g_handlers[i].active && g_handlers[i].port == port) {
            g_handlers[i].active = 0;
        }
    }
}

int udp_send(const uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port,
             uint8_t *packet_buf, uint16_t payload_len) {
    udp_header_t *uh = (udp_header_t *)(packet_buf + ETH_HEADER_LEN + IP_HEADER_LEN);
    uint16_t udp_len = (uint16_t)(UDP_HEADER_LEN + payload_len);

    uh->src_port = bswap16(src_port);
    uh->dst_port = bswap16(dst_port);
    uh->length   = bswap16(udp_len);
    uh->checksum = 0;   /* optional for IPv4 */

    return ip_send(dst_ip, IP_PROTO_UDP, packet_buf, udp_len);
}

void udp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len) {
    const udp_header_t *uh;
    uint16_t dst_port;
    const uint8_t *payload;
    uint16_t pay_len;
    int i;

    if (len < UDP_HEADER_LEN) return;
    uh = (const udp_header_t *)pkt;
    dst_port = bswap16(uh->dst_port);
    payload  = pkt + UDP_HEADER_LEN;
    pay_len  = (uint16_t)(len - UDP_HEADER_LEN);

    for (i = 0; i < UDP_HANDLER_MAX; i++) {
        if (g_handlers[i].active && g_handlers[i].port == dst_port) {
            g_handlers[i].fn(src_ip, bswap16(uh->src_port), payload, pay_len, g_handlers[i].ctx);
            return;
        }
    }
}
