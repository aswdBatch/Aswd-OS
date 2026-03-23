#pragma once

#include <stdint.h>

int  e1000_init(void);
int  e1000_send(const uint8_t *buf, uint16_t len);
int  e1000_recv(uint8_t *buf, uint16_t max_len);
void e1000_get_mac(uint8_t *mac);
