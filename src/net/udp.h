#pragma once

#include <stdint.h>

#define UDP_HEADER_LEN 8

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

/* Send a UDP packet. buf must have ETH+IP+UDP header room before payload. */
int udp_send(const uint8_t *dst_ip, uint16_t src_port, uint16_t dst_port,
             uint8_t *packet_buf, uint16_t payload_len);

/* Called by IP layer on incoming UDP */
void udp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len);

/* Register a simple one-shot receive callback for a port.
   When a UDP packet arrives on dst_port, callback is called once. */
typedef void (*udp_recv_fn)(const uint8_t *src_ip, uint16_t src_port,
                            const uint8_t *data, uint16_t len, void *ctx);
void udp_register(uint16_t port, udp_recv_fn fn, void *ctx);
void udp_unregister(uint16_t port);
