#include "net/dns.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "net/ethernet.h"
#include "net/ip.h"
#include "net/net.h"
#include "net/udp.h"
#include "lib/string.h"

#define DNS_PORT     53u
#define DNS_SRC_PORT 1053u

/* DNS header */
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

/* Encode a hostname into DNS label format, returns length. */
static uint16_t encode_name(const char *hostname, uint8_t *out, uint16_t max) {
    uint16_t pos = 0;
    const char *p = hostname;

    while (*p && pos < max - 2) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        uint8_t llen = (uint8_t)(dot - p);
        if (pos + 1 + llen >= max) break;
        out[pos++] = llen;
        while (p < dot && pos < max) out[pos++] = (uint8_t)*p++;
        if (*p == '.') p++;
    }
    if (pos < max) out[pos++] = 0;  /* root label */
    return pos;
}

/* DNS reply parse state */
static int      g_pending = 0;
static uint16_t g_txid    = 0x4153u;
static uint8_t  g_result[4];
static int      g_got_reply = 0;

static void dns_rx(const uint8_t *src_ip, uint16_t src_port,
                   const uint8_t *data, uint16_t len, void *ctx) {
    const dns_header_t *hdr;
    uint16_t flags;
    int ancount;
    uint16_t i;
    (void)src_ip; (void)src_port; (void)ctx;

    if (!g_pending) return;
    if (len < (uint16_t)sizeof(dns_header_t)) return;

    hdr    = (const dns_header_t *)data;
    flags  = bswap16(hdr->flags);
    ancount = (int)bswap16(hdr->ancount);

    if (bswap16(hdr->id) != g_txid) return;
    if (!(flags & 0x8000u)) return;  /* not a response */
    if (ancount == 0) return;

    /* Skip question section: scan past the query name and QTYPE/QCLASS */
    i = (uint16_t)sizeof(dns_header_t);
    while (i < len && data[i] != 0) {
        i += data[i] + 1;
    }
    i++;       /* skip root label */
    i += 4;    /* skip QTYPE + QCLASS */

    /* Parse answer records looking for A record */
    int a;
    for (a = 0; a < ancount && i + 12 <= len; a++) {
        /* Skip name (may be a pointer 0xC0xx or label) */
        if ((data[i] & 0xC0u) == 0xC0u) {
            i += 2;
        } else {
            while (i < len && data[i] != 0) i += data[i] + 1;
            i++;
        }
        if (i + 10 > len) break;
        uint16_t rtype  = (uint16_t)((uint16_t)data[i] << 8 | data[i+1]); i += 2;
        i += 2; /* class */
        i += 4; /* TTL */
        uint16_t rdlen = (uint16_t)((uint16_t)data[i] << 8 | data[i+1]); i += 2;
        if (rtype == 1 && rdlen == 4 && i + 4 <= len) {
            /* A record */
            mem_copy(g_result, data + i, 4);
            g_got_reply = 1;
            break;
        }
        i += rdlen;
    }
}

int dns_resolve(const char *hostname, uint8_t *ip_out) {
    uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN + 512];
    dns_header_t *hdr;
    uint8_t *qbuf;
    uint16_t name_len;
    const net_info_t *ni = net_get_info();
    uint32_t deadline;
    int i;

    if (!ni->link_up || !(ni->dns[0] || ni->dns[1] || ni->dns[2] || ni->dns[3])) return 0;

    /* Check if it's already an IP literal */
    {
        int dotcount = 0;
        int all_digits = 1;
        const char *p = hostname;
        while (*p) {
            if (*p == '.') dotcount++;
            else if (*p < '0' || *p > '9') { all_digits = 0; break; }
            p++;
        }
        if (all_digits && dotcount == 3) {
            /* Parse a.b.c.d */
            uint8_t v[4];
            int vi = 0;
            uint32_t cur = 0;
            const char *q = hostname;
            for (; *q && vi < 4; q++) {
                if (*q == '.') { v[vi++] = (uint8_t)cur; cur = 0; }
                else cur = cur * 10 + (uint8_t)(*q - '0');
            }
            v[vi] = (uint8_t)cur;
            mem_copy(ip_out, v, 4);
            return 1;
        }
    }

    g_got_reply = 0;
    g_pending   = 1;
    g_txid      = (uint16_t)(g_txid + 1);

    udp_register(DNS_SRC_PORT, dns_rx, 0);

    mem_set(frame, 0, sizeof(frame));
    hdr  = (dns_header_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN);
    qbuf = (uint8_t *)hdr + sizeof(dns_header_t);

    hdr->id      = bswap16(g_txid);
    hdr->flags   = bswap16(0x0100u);  /* RD = 1 */
    hdr->qdcount = bswap16(1);

    name_len = encode_name(hostname, qbuf, 256);
    qbuf[name_len]     = 0; qbuf[name_len + 1] = 1;  /* QTYPE=A */
    qbuf[name_len + 2] = 0; qbuf[name_len + 3] = 1;  /* QCLASS=IN */

    uint16_t pay_len = (uint16_t)(sizeof(dns_header_t) + name_len + 4);

    /* Send up to 3 times, wait up to 3 seconds total */
    deadline = timer_get_ticks() + 300u;  /* 3 seconds at 100 Hz */
    for (i = 0; i < 3 && !g_got_reply; i++) {
        udp_send(ni->dns, DNS_SRC_PORT, DNS_PORT, frame, pay_len);
        uint32_t retry = timer_get_ticks() + 100u;
        while (!g_got_reply && timer_get_ticks() < retry &&
               timer_get_ticks() < deadline) {
            net_poll();
            __asm__ volatile("sti; hlt");
        }
    }

    udp_unregister(DNS_SRC_PORT);
    g_pending = 0;

    if (g_got_reply) {
        mem_copy(ip_out, g_result, 4);
        return 1;
    }
    return 0;
}
