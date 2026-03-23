#pragma once

#include <stdint.h>

/* Process incoming ICMP packet */
void icmp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len);

/* Send a ping (ICMP echo request). Returns sequence number sent. */
int icmp_ping_send(const uint8_t *dst_ip);

/* Check if a ping reply has arrived for the given sequence number.
   Returns 1 if reply received, 0 otherwise. */
int icmp_ping_reply(int seq);
