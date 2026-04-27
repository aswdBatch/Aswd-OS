#ifndef NET_WIFI_ATH5K_H
#define NET_WIFI_ATH5K_H

#include "net/wifi.h"

int ath5k_probe(void);
extern const wifi_backend_ops_t ath5k_backend_ops;

#endif
