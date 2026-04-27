#ifndef NET_WIFI_ATH9K_H
#define NET_WIFI_ATH9K_H

#include "net/wifi.h"

int ath9k_probe(void);
extern const wifi_backend_ops_t ath9k_backend_ops;

#endif
