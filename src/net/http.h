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
int http_post(const char *url, const char *post_body, uint16_t post_len,
              char *resp_buf, uint16_t resp_max, int *status_out);
/* Plain HTTP CONNECT tunnel to proxy (TLS payload not implemented here).
   On success TCP stays connected to proxy — caller must tcp_close(). Returns 1 OK. */
int http_proxy_connect_open(const char *proxy_url,
                            const char *target_host, uint16_t target_port);
http_error_t http_last_error(void);
const char *http_error_string(http_error_t err);
