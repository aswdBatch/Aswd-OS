#include "net/ethernet.h"

#include <stdint.h>

void eth_broadcast(uint8_t *dst) {
    int i;
    for (i = 0; i < 6; i++) dst[i] = 0xFF;
}

int eth_addr_eq(const uint8_t *a, const uint8_t *b) {
    int i;
    for (i = 0; i < 6; i++) if (a[i] != b[i]) return 0;
    return 1;
}

void eth_addr_copy(uint8_t *dst, const uint8_t *src) {
    int i;
    for (i = 0; i < 6; i++) dst[i] = src[i];
}
