#pragma once

#include <stddef.h>

#define SITE_ALLOW_MAX      24
#define SITE_ALLOW_HOST_MAX 64

typedef enum {
    SITE_ALLOW_OK = 0,
    SITE_ALLOW_ERR_INVALID = -1,
    SITE_ALLOW_ERR_FULL = -2,
    SITE_ALLOW_ERR_EXISTS = -3,
    SITE_ALLOW_ERR_NOT_FOUND = -4,
    SITE_ALLOW_ERR_STORE = -5,
} site_allow_result_t;

void site_allow_init(void);
int  site_allow_count(void);
const char *site_allow_host_at(int index);
int  site_allow_add(const char *input);
int  site_allow_remove(const char *input);
int  site_allow_matches(const char *host);
int  site_allow_enabled(void);
int  site_allow_persistent_available(void);

