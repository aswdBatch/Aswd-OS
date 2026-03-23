#pragma once

#include <stdint.h>

/* Start the DHCP discovery process */
void dhcp_start(void);

/* Poll DHCP state machine — call from net_poll. Returns 1 if IP was just assigned. */
int dhcp_poll(void);

/* Returns 1 if DHCP has successfully configured an IP */
int dhcp_bound(void);
