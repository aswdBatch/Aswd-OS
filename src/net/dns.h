#pragma once

#include <stdint.h>

/* Resolve a hostname to IPv4. Blocking: polls the network for up to ~3 seconds.
   Returns 1 if resolved (ip_out filled), 0 if failed. */
int dns_resolve(const char *hostname, uint8_t *ip_out);
