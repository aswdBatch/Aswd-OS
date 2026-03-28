#pragma once

#include <stdint.h>

typedef enum {
    HTTP_ERR_NONE = 0,
    HTTP_ERR_URL,
    HTTP_ERR_NOT_ALLOWED,
    HTTP_ERR_DNS,
    HTTP_ERR_CONNECT,
    HTTP_ERR_SEND,
    HTTP_ERR_REDIRECT,
} http_error_t;

/* Perform an HTTP GET request.
   url: "http://hostname/path" or "http://hostname:port/path"
   body_buf: buffer to receive the response body (not the headers)
   body_max: max bytes to store in body_buf
   Returns number of bytes in body_buf, or -1 on error.
   Sets status_out to the HTTP status code (e.g. 200) if not NULL. */
int http_get(const char *url, char *body_buf, uint16_t body_max, int *status_out);
http_error_t http_last_error(void);
const char *http_error_string(http_error_t err);
