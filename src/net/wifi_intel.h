#ifndef NET_WIFI_INTEL_H
#define NET_WIFI_INTEL_H

#include "net/wifi.h"

int intel_wifi_probe(void);
int intel_wifi_init(void);
extern const wifi_backend_ops_t intel_wifi_backend_ops;

#endif
