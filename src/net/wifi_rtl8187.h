#ifndef NET_WIFI_RTL8187_H
#define NET_WIFI_RTL8187_H

#include "net/wifi.h"

int rtl8187_probe(void);
extern const wifi_backend_ops_t rtl8187_backend_ops;

#endif
