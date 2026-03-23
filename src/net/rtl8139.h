#pragma once

#include <stdint.h>

/* Returns 1 if an RTL8139 was found and initialized */
int  rtl8139_init(void);
int  rtl8139_send(const uint8_t *buf, uint16_t len);
int  rtl8139_recv(uint8_t *buf, uint16_t max_len);
void rtl8139_get_mac(uint8_t *mac);
