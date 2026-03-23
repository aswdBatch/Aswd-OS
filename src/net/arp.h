#pragma once

#include <stdint.h>

/* Resolve IP to MAC. Returns 1 if found, 0 if not in cache (sends ARP request). */
int  arp_resolve(const uint8_t *ip, uint8_t *mac_out);

/* Process an incoming ARP packet (payload after Ethernet header). */
void arp_rx(const uint8_t *pkt, uint16_t len);

/* Update ARP cache entry */
void arp_update(const uint8_t *ip, const uint8_t *mac);
