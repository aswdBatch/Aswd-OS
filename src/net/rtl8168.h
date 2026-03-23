#pragma once

#include <stdint.h>

int  rtl8168_init(void);
int  rtl8168_send(const uint8_t *buf, uint16_t len);
int  rtl8168_recv(uint8_t *buf, uint16_t max_len);
void rtl8168_get_mac(uint8_t *mac);
