#pragma once

#include <stdint.h>

/* NIC name length */
#define NET_NIC_NAME_MAX 24

typedef enum {
    NET_TRANSPORT_NONE = 0,
    NET_TRANSPORT_WIRED,
    NET_TRANSPORT_WIFI,
} net_transport_t;

typedef enum {
    NET_CONN_DOWN = 0,
    NET_CONN_DISCOVERING,
    NET_CONN_IDLE,
    NET_CONN_SCANNING,
    NET_CONN_CONNECTING,
    NET_CONN_CONNECTED,
    NET_CONN_LIMITED,
    NET_CONN_UNSUPPORTED,
} net_connection_state_t;

typedef struct {
    char    nic_name[NET_NIC_NAME_MAX]; /* e.g. "RTL8139", "RTL8168", "e1000" */
    char    wifi_name[NET_NIC_NAME_MAX];
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
    uint8_t dns[4];
    int     dhcp_pending;   /* 1 = DHCP in progress */
    int     link_up;        /* 1 = NIC initialized */
    int     wifi_detected;
    int     wifi_supported;
    int     wifi_backend_ready;
    uint8_t wifi_signal_pct;
    uint8_t wifi_security;
    uint8_t wifi_family;
    char    wifi_ssid[33];
    net_transport_t active_transport;
    net_connection_state_t connection_state;
} net_info_t;

/* Initialize network subsystem: PCI scan → driver init → start DHCP */
void net_init(void);

/* Poll: process received packets, re-arm DHCP timer if needed.
   Call from the main loop (gui_run idle tick). */
void net_poll(void);

/* Get current network status (read-only) */
const net_info_t *net_get_info(void);
const char       *net_transport_name(net_transport_t transport);
const char       *net_connection_state_name(net_connection_state_t state);

/* Send a raw Ethernet frame. Returns 0 on success. */
int net_send_frame(const uint8_t *frame, uint16_t len);

/* Called internally by dhcp.c when a lease is obtained */
void net_apply_dhcp(const uint8_t *ip, const uint8_t *mask,
                    const uint8_t *gw, const uint8_t *dns);

/* Receive a pending raw Ethernet frame into buf (max len).
   Returns number of bytes received, or 0 if none. */
int net_recv_frame(uint8_t *buf, uint16_t max_len);
