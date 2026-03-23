#pragma once

#include <stdint.h>

/* Simple single-connection blocking TCP client.
   These functions may block and call net_poll() internally. */

/* Connect to dst_ip:dst_port. Returns 0 on success, -1 on failure. */
int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port);

/* Send data. Returns bytes sent or -1 on error. */
int tcp_send_data(const uint8_t *data, uint16_t len);

/* Receive data into buf (up to max_len). Returns bytes received, 0=no data, -1=closed. */
int tcp_recv_data(uint8_t *buf, uint16_t max_len);

/* Close the connection. */
void tcp_close(void);

/* Returns 1 if connected, 0 otherwise. */
int tcp_connected(void);

/* Called by IP layer on incoming TCP segment */
void tcp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len);
