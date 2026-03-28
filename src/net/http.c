#include "net/http.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "net/dns.h"
#include "net/net.h"
#include "net/site_allow.h"
#include "net/tcp.h"
#include "lib/string.h"

static http_error_t g_last_error = HTTP_ERR_NONE;

static void http_set_error(http_error_t err) {
    g_last_error = err;
}

/* ── URL parser ──────────────────────────────────────────────────── */

static int parse_url(const char *url,
                     char *host_out, uint16_t host_max,
                     uint16_t *port_out,
                     char *path_out, uint16_t path_max) {
    const char *p = url;

    if (str_ncmp(p, "http://", 7) != 0) return 0;
    p += 7;

    const char *slash = p;
    while (*slash && *slash != '/') slash++;

    const char *colon = p;
    while (colon < slash && *colon != ':') colon++;

    uint16_t host_len;
    if (*colon == ':') {
        host_len = (uint16_t)(colon - p);
        uint32_t port = 0;
        const char *q = colon + 1;
        while (q < slash) port = port * 10 + (uint8_t)(*q++ - '0');
        *port_out = (uint16_t)port;
    } else {
        host_len = (uint16_t)(slash - p);
        *port_out = 80u;
    }

    if (host_len == 0 || host_len >= host_max) return 0;
    mem_copy(host_out, p, host_len);
    host_out[host_len] = '\0';

    if (*slash) {
        uint16_t path_len = (uint16_t)str_len(slash);
        if (path_len >= path_max) path_len = (uint16_t)(path_max - 1);
        mem_copy(path_out, slash, path_len);
        path_out[path_len] = '\0';
    } else {
        path_out[0] = '/'; path_out[1] = '\0';
    }
    return 1;
}

/* ── Header buffer ───────────────────────────────────────────────── */

#define HDR_BUF_MAX 512

static char g_hdr_buf[HDR_BUF_MAX];
static int  g_hdr_len = 0;

static void hdr_reset(void) { g_hdr_len = 0; g_hdr_buf[0] = '\0'; }

/* Append bytes to header buffer until \r\n\r\n found.
   Returns pointer into recv_buf after headers, or NULL if not yet complete.
   Sets *body_start_len to bytes after the header terminator. */
static const uint8_t *hdr_feed(const uint8_t *data, int len, int *body_start_len) {
    for (int i = 0; i < len; i++) {
        if (g_hdr_len < HDR_BUF_MAX - 1)
            g_hdr_buf[g_hdr_len++] = (char)data[i];
        /* Check for \r\n\r\n ending */
        if (g_hdr_len >= 4 &&
            g_hdr_buf[g_hdr_len-4] == '\r' && g_hdr_buf[g_hdr_len-3] == '\n' &&
            g_hdr_buf[g_hdr_len-2] == '\r' && g_hdr_buf[g_hdr_len-1] == '\n') {
            g_hdr_buf[g_hdr_len] = '\0';
            *body_start_len = len - i - 1;
            return data + i + 1;
        }
    }
    return 0; /* not done */
}

/* Case-insensitive header field search: find "Field: value\r\n" in g_hdr_buf */
static const char *hdr_find(const char *field) {
    int flen = (int)str_len(field);
    const char *p = g_hdr_buf;
    while (*p) {
        /* skip to next line */
        const char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) p++; /* skip \n */
        /* compare field name (case-insensitive) */
        if ((int)(p - line) <= flen) continue;
        int match = 1;
        for (int i = 0; i < flen; i++) {
            char a = line[i], b = field[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) { match = 0; break; }
        }
        if (match && line[flen] == ':') {
            const char *val = line + flen + 1;
            while (*val == ' ') val++;
            return val;
        }
    }
    return 0;
}

static int hdr_parse_status(void) {
    if (str_ncmp(g_hdr_buf, "HTTP/", 5) != 0) return 0;
    const char *sp = g_hdr_buf + 5;
    while (*sp && *sp != ' ') sp++;
    if (*sp != ' ') return 0;
    sp++;
    int status = 0;
    while (*sp >= '0' && *sp <= '9') status = status * 10 + (*sp++ - '0');
    return status;
}

/* ── Chunked TE parser ───────────────────────────────────────────── */

typedef enum { CK_NONE = 0, CK_SIZE, CK_DATA, CK_TRAILER } chunk_state_t;

static chunk_state_t g_ck_state   = CK_NONE;
static uint32_t      g_ck_remain  = 0;

static void chunked_reset(void) { g_ck_state = CK_SIZE; g_ck_remain = 0; }

/* Process body bytes through the chunk parser. Appends decoded data to
   body_buf. Returns 1 when transfer complete (zero-length chunk), 0 otherwise. */
static int chunked_feed(const uint8_t *data, int len,
                        char *body_buf, uint16_t body_max, uint16_t *body_len)
{
    for (int i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (g_ck_state == CK_SIZE) {
            if (c == '\r') continue;
            if (c == '\n') {
                if (g_ck_remain == 0) return 1; /* terminal chunk */
                g_ck_state = CK_DATA;
                continue;
            }
            /* Hex digit */
            uint32_t nibble;
            if (c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
            else continue;
            g_ck_remain = (g_ck_remain << 4) | nibble;
        } else if (g_ck_state == CK_DATA) {
            if (*body_len + 1 < body_max) {
                body_buf[*body_len] = (char)c;
                (*body_len)++;
                body_buf[*body_len] = '\0';
            }
            if (--g_ck_remain == 0)
                g_ck_state = CK_TRAILER;
        } else if (g_ck_state == CK_TRAILER) {
            if (c == '\n') { /* end of chunk trailer \r\n */
                g_ck_remain = 0;
                g_ck_state  = CK_SIZE;
            }
        }
    }
    return 0;
}

/* ── Single HTTP GET (one request, no redirects) ─────────────────── */

static int http_get_once(const char *url,
                         char *body_buf, uint16_t body_max,
                         int *status_out,
                         char *location_out, uint16_t loc_max)
{
    char host[128];
    char path[256];
    uint16_t port;
    uint8_t  ip[4];
    char req[512];
    uint8_t  recv_buf[1024];
    int  in_body   = 0;
    int  chunked   = 0;
    uint16_t body_len = 0;
    uint32_t deadline;
    int status = 0;

    if (status_out)   *status_out   = 0;
    if (location_out) location_out[0] = '\0';

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        http_set_error(HTTP_ERR_URL);
        return -1;
    }
    if (!site_allow_matches(host)) {
        http_set_error(HTTP_ERR_NOT_ALLOWED);
        return -1;
    }
    if (!dns_resolve(host, ip)) {
        http_set_error(HTTP_ERR_DNS);
        return -1;
    }
    if (tcp_connect(ip, port) != 0) {
        http_set_error(HTTP_ERR_CONNECT);
        return -1;
    }

    /* Build GET request */
    {
        uint16_t pos = 0;
        const char *method = "GET ";
        const char *http11 = " HTTP/1.1\r\nHost: ";
        const char *crlf   = "\r\nConnection: close\r\n\r\n";
        uint16_t ml = (uint16_t)str_len(method);
        uint16_t pl = (uint16_t)str_len(path);
        uint16_t hl = (uint16_t)str_len(http11);
        uint16_t nl = (uint16_t)str_len(host);
        uint16_t cl = (uint16_t)str_len(crlf);
        if ((uint32_t)(ml + pl + hl + nl + cl + 1) < sizeof(req)) {
            mem_copy(req + pos, method, ml); pos += ml;
            mem_copy(req + pos, path,   pl); pos += pl;
            mem_copy(req + pos, http11, hl); pos += hl;
            mem_copy(req + pos, host,   nl); pos += nl;
            mem_copy(req + pos, crlf,   cl); pos += cl;
            req[pos] = '\0';
        }
    }

    if (tcp_send_data((const uint8_t *)req, (uint16_t)str_len(req)) < 0) {
        http_set_error(HTTP_ERR_SEND);
        tcp_close();
        return -1;
    }

    hdr_reset();

    deadline = timer_get_ticks() + 500u;  /* 5 second initial timeout */
    while (timer_get_ticks() < deadline) {
        net_poll();
        tcp_check_retransmit();
        int n = tcp_recv_data(recv_buf, (uint16_t)sizeof(recv_buf) - 1);
        if (n < 0) break;
        if (n == 0) {
            if (!tcp_connected()) break;
            __asm__ volatile("sti; hlt");
            continue;
        }
        recv_buf[n] = '\0';
        deadline = timer_get_ticks() + 300u; /* reset on data */

        uint8_t *p         = recv_buf;
        int      remaining = n;

        if (!in_body) {
            int body_start_len = 0;
            const uint8_t *body_start = hdr_feed(p, remaining, &body_start_len);
            if (!body_start) continue; /* headers not complete yet */

            /* Headers complete — parse them */
            status  = hdr_parse_status();

            if (location_out && loc_max > 0) {
                const char *loc = hdr_find("Location");
                if (loc) {
                    int llen = 0;
                    while (loc[llen] && loc[llen] != '\r' && loc[llen] != '\n') llen++;
                    if (llen >= (int)loc_max) llen = (int)loc_max - 1;
                    mem_copy(location_out, loc, (uint32_t)llen);
                    location_out[llen] = '\0';
                }
            }

            const char *te = hdr_find("Transfer-Encoding");
            if (te && str_ncmp(te, "chunked", 7) == 0) {
                chunked = 1;
                chunked_reset();
            }

            in_body   = 1;
            p         = (uint8_t *)body_start;
            remaining = body_start_len;
            if (remaining <= 0) continue;
        }

        /* Body accumulation */
        if (chunked) {
            if (chunked_feed(p, remaining, body_buf, body_max, &body_len))
                break; /* zero-length chunk = done */
        } else {
            uint16_t copy = (uint16_t)remaining;
            if (body_len + copy >= body_max) copy = (uint16_t)(body_max - body_len - 1);
            if (copy > 0) {
                mem_copy(body_buf + body_len, p, copy);
                body_len += copy;
                body_buf[body_len] = '\0';
            }
        }
    }

    tcp_close();
    if (status_out) *status_out = status;
    return (int)body_len;
}

/* ── Public http_get (with redirect following) ───────────────────── */

int http_get(const char *url, char *body_buf, uint16_t body_max, int *status_out) {
    static char current_url[512];
    static char location[512];
    int redirects = 0;
    int status;

    g_last_error = HTTP_ERR_NONE;
    str_copy(current_url, url, sizeof(current_url));
    if (body_max > 0) body_buf[0] = '\0';

    while (redirects <= 5) {
        status = 0;
        location[0] = '\0';
        int rc = http_get_once(current_url, body_buf, body_max,
                               &status, location, sizeof(location));
        if (rc < 0) {
            if (status_out) *status_out = 0;
            return -1;
        }
        if ((status == 301 || status == 302) && location[0] &&
            str_ncmp(location, "http://", 7) == 0) {
            str_copy(current_url, location, sizeof(current_url));
            if (body_max > 0) body_buf[0] = '\0';
            redirects++;
            continue;
        }
        if (status_out) *status_out = status;
        return rc;
    }
    http_set_error(HTTP_ERR_REDIRECT);
    if (status_out) *status_out = 0;
    return -1;
}

http_error_t http_last_error(void) {
    return g_last_error;
}

const char *http_error_string(http_error_t err) {
    if (err == HTTP_ERR_URL) return "Invalid URL";
    if (err == HTTP_ERR_NOT_ALLOWED) return "Site not allowed";
    if (err == HTTP_ERR_DNS) return "DNS lookup failed";
    if (err == HTTP_ERR_CONNECT) return "Connection failed";
    if (err == HTTP_ERR_SEND) return "Request send failed";
    if (err == HTTP_ERR_REDIRECT) return "Too many redirects";
    return "HTTP request failed";
}
