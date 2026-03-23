#pragma once

#include <stdint.h>

/* Perform an HTTP GET request.
   url: "http://hostname/path" or "http://hostname:port/path"
   body_buf: buffer to receive the response body (not the headers)
   body_max: max bytes to store in body_buf
   Returns number of bytes in body_buf, or -1 on error.
   Sets status_out to the HTTP status code (e.g. 200) if not NULL. */
int http_get(const char *url, char *body_buf, uint16_t body_max, int *status_out);
