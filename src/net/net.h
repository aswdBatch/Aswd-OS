#pragma once

#include <stdint.h>

/* NIC name length */
#define NET_NIC_NAME_MAX 24

typedef struct {
    char    nic_name[NET_NIC_NAME_MAX]; /* e.g. "RTL8139", "RTL8168", "e1000" */
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
    uint8_t dns[4];
    int     dhcp_pending;   /* 1 = DHCP in progress */
    int     link_up;        /* 1 = NIC initialized */
} net_info_t;

/* Initialize network subsystem: PCI scan → driver init → start DHCP */
void net_init(void);

/* Poll: process received packets, re-arm DHCP timer if needed.
   Call from the main loop (gui_run idle tick). */
void net_poll(void);

/* Get current network status (read-only) */
const net_info_t *net_get_info(void);

/* Send a raw Ethernet frame. Returns 0 on success. */
int net_send_frame(const uint8_t *frame, uint16_t len);

/* Called internally by dhcp.c when a lease is obtained */
void net_apply_dhcp(const uint8_t *ip, const uint8_t *mask,
                    const uint8_t *gw, const uint8_t *dns);

/* Receive a pending raw Ethernet frame into buf (max len).
   Returns number of bytes received, or 0 if none. */
int net_recv_frame(uint8_t *buf, uint16_t max_len);
