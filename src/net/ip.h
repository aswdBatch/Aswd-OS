#pragma once

#include <stdint.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} ip_header_t;

#define IP_HEADER_LEN 20

uint16_t ip_checksum(const void *data, uint16_t len);

/* Build and send an IP packet.
   payload points to data after the IP header; payload_len is its size.
   The buffer must have ETH_HEADER_LEN + IP_HEADER_LEN bytes before payload. */
int ip_send(const uint8_t *dst_ip, uint8_t proto,
            uint8_t *packet_buf, uint16_t payload_len);

/* Process incoming IP packet (starting at IP header, len includes IP header) */
void ip_rx(const uint8_t *pkt, uint16_t len);
