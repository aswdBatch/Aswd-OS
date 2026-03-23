#include "net/http.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "net/dns.h"
#include "net/net.h"
#include "net/tcp.h"
#include "lib/string.h"

/* Parse "http://hostname[:port]/path" */
static int parse_url(const char *url,
                     char *host_out, uint16_t host_max,
                     uint16_t *port_out,
                     char *path_out, uint16_t path_max) {
    const char *p = url;

    if (str_ncmp(p, "http://", 7) != 0) return 0;
    p += 7;

    /* Find end of host[:port] */
    const char *slash = p;
    while (*slash && *slash != '/') slash++;

    /* Check for port */
    const char *colon = p;
    while (colon < slash && *colon != ':') colon++;

    uint16_t host_len;
    if (*colon == ':') {
        host_len = (uint16_t)(colon - p);
        /* Parse port */
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

    /* Path */
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

int http_get(const char *url, char *body_buf, uint16_t body_max, int *status_out) {
    char host[128];
    char path[256];
    uint16_t port;
    uint8_t  ip[4];
    char req[512];
    uint8_t  recv_buf[1024];
    int  in_body = 0;
    uint16_t body_len = 0;
    uint32_t deadline;
    int status = 0;
    int rc;

    if (status_out) *status_out = 0;

    if (!parse_url(url, host, sizeof(host), &port, path, sizeof(path))) return -1;

    if (!dns_resolve(host, ip)) return -1;

    if (tcp_connect(ip, port) != 0) return -1;

    /* Build GET request */
    {
        uint16_t pos = 0;
        const char *method = "GET ";
        const char *http11 = " HTTP/1.0\r\nHost: ";
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

    rc = tcp_send_data((const uint8_t *)req, (uint16_t)str_len(req));
    if (rc < 0) { tcp_close(); return -1; }

    /* Receive response */
    deadline = timer_get_ticks() + 500u;  /* 5 second timeout */
    while (timer_get_ticks() < deadline) {
        net_poll();
        int n = tcp_recv_data(recv_buf, (uint16_t)sizeof(recv_buf) - 1);
        if (n < 0) break;
        if (n == 0) {
            if (!tcp_connected()) break;
            __asm__ volatile("sti; hlt");
            continue;
        }
        recv_buf[n] = '\0';
        deadline = timer_get_ticks() + 300u;  /* reset timeout on data */

        uint8_t *p = recv_buf;
        int remaining = n;

        if (!in_body) {
            /* Look for end of headers (\r\n\r\n or \n\n) */
            int i;
            for (i = 0; i < remaining - 3; i++) {
                if (p[i] == '\r' && p[i+1] == '\n' && p[i+2] == '\r' && p[i+3] == '\n') {
                    /* Parse status line before we skip */
                    if (!status) {
                        /* "HTTP/1.x NNN " */
                        const char *sl = (const char *)p;
                        if (str_ncmp(sl, "HTTP/", 5) == 0) {
                            const char *sp = sl + 5;
                            while (*sp && *sp != ' ') sp++;
                            if (*sp == ' ') {
                                sp++;
                                status = 0;
                                while (*sp >= '0' && *sp <= '9') status = status * 10 + (*sp++ - '0');
                            }
                        }
                    }
                    in_body = 1;
                    p += i + 4;
                    remaining -= i + 4;
                    break;
                }
            }
            if (!in_body) continue;
        }

        if (in_body && remaining > 0) {
            uint16_t copy = (uint16_t)remaining;
            if (body_len + copy >= body_max) copy = (uint16_t)(body_max - body_len - 1);
            mem_copy(body_buf + body_len, p, copy);
            body_len += copy;
            body_buf[body_len] = '\0';
        }
    }

    tcp_close();
    if (status_out) *status_out = status;
    return (int)body_len;
}
