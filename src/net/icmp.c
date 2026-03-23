#include "net/icmp.h"

#include <stdint.h>

#include "net/ethernet.h"
#include "net/ip.h"
#include "net/net.h"
#include "lib/string.h"

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_header_t;

#define ICMP_PAYLOAD_LEN 32
#define ICMP_ID  0x4153u   /* "AS" */

static int g_last_reply_seq = -1;

void icmp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len) {
    const icmp_header_t *ih;
    uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN];
    icmp_header_t *rep;
    uint16_t rep_pay_len;

    if (len < (uint16_t)sizeof(icmp_header_t)) return;
    ih = (const icmp_header_t *)pkt;

    if (ih->type == ICMP_ECHO_REPLY && bswap16(ih->id) == ICMP_ID) {
        g_last_reply_seq = (int)bswap16(ih->seq);
        return;
    }

    if (ih->type != ICMP_ECHO_REQUEST) return;

    /* Send echo reply */
    rep_pay_len = (uint16_t)(sizeof(icmp_header_t) +
                  (len > sizeof(icmp_header_t) ? len - sizeof(icmp_header_t) : 0));
    if (rep_pay_len > (uint16_t)(sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN))
        rep_pay_len = (uint16_t)(sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN);

    rep = (icmp_header_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN);
    rep->type     = ICMP_ECHO_REPLY;
    rep->code     = 0;
    rep->checksum = 0;
    rep->id       = ih->id;
    rep->seq      = ih->seq;
    if (len > sizeof(icmp_header_t)) {
        uint16_t data_len = (uint16_t)(len - sizeof(icmp_header_t));
        if (data_len > ICMP_PAYLOAD_LEN) data_len = ICMP_PAYLOAD_LEN;
        mem_copy((uint8_t *)rep + sizeof(icmp_header_t),
                 pkt + sizeof(icmp_header_t), data_len);
    }
    rep->checksum = ip_checksum(rep, rep_pay_len);

    ip_send(src_ip, IP_PROTO_ICMP, frame, rep_pay_len);
}

int icmp_ping_send(const uint8_t *dst_ip) {
    static int g_seq = 0;
    uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN];
    icmp_header_t *req;
    uint8_t *payload;
    int i;

    req = (icmp_header_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN);
    payload = (uint8_t *)req + sizeof(icmp_header_t);

    req->type     = ICMP_ECHO_REQUEST;
    req->code     = 0;
    req->checksum = 0;
    req->id       = bswap16(ICMP_ID);
    req->seq      = bswap16((uint16_t)g_seq);
    for (i = 0; i < ICMP_PAYLOAD_LEN; i++) payload[i] = (uint8_t)i;
    req->checksum = ip_checksum(req, (uint16_t)(sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN));

    ip_send(dst_ip, IP_PROTO_ICMP, frame,
            (uint16_t)(sizeof(icmp_header_t) + ICMP_PAYLOAD_LEN));

    return g_seq++;
}

int icmp_ping_reply(int seq) {
    return (g_last_reply_seq == seq) ? 1 : 0;
}
