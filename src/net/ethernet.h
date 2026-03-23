#pragma once

#include <stdint.h>

#define ETHERTYPE_ARP  0x0806u
#define ETHERTYPE_IP   0x0800u

#define ETH_ADDR_LEN   6
#define ETH_HEADER_LEN 14
#define ETH_MAX_FRAME  1514

typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;   /* big-endian */
} eth_header_t;

/* Helpers */
static inline uint16_t bswap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint32_t bswap32(uint32_t v) {
    return ((v & 0xFF000000u) >> 24) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x000000FFu) << 24);
}

void eth_broadcast(uint8_t *dst);
int  eth_addr_eq(const uint8_t *a, const uint8_t *b);
void eth_addr_copy(uint8_t *dst, const uint8_t *src);
