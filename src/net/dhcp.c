#include "net/dhcp.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "net/ethernet.h"
#include "net/ip.h"
#include "net/net.h"
#include "net/udp.h"
#include "lib/string.h"

/* DHCP ports */
#define DHCP_SERVER_PORT 67u
#define DHCP_CLIENT_PORT 68u

/* DHCP message types (option 53) */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

/* Fixed-layout DHCP packet (BOOTP + magic cookie + options) */
#define DHCP_BUF_SIZE 548u

typedef struct __attribute__((packed)) {
    uint8_t  op;        /* 1=BOOTREQUEST */
    uint8_t  htype;     /* 1=Ethernet */
    uint8_t  hlen;      /* 6 */
    uint8_t  hops;
    uint32_t xid;       /* transaction ID */
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4]; /* client IP */
    uint8_t  yiaddr[4]; /* your IP */
    uint8_t  siaddr[4]; /* server IP */
    uint8_t  giaddr[4]; /* relay agent */
    uint8_t  chaddr[16];/* client hardware address */
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;     /* 0x63825363 */
    uint8_t  options[312];
} dhcp_pkt_t;

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_DISCOVER,
    DHCP_STATE_OFFER_RECEIVED,
    DHCP_STATE_REQUEST,
    DHCP_STATE_BOUND,
} dhcp_state_t;

static dhcp_state_t g_state      = DHCP_STATE_IDLE;
static uint32_t     g_xid        = 0x31415926u;
static uint32_t     g_send_tick  = 0;
static uint32_t     g_retry_tick = 0;
static int          g_bound      = 0;

/* Stored offer */
static uint8_t g_offered_ip[4];
static uint8_t g_server_ip[4];
static uint8_t g_subnet[4];
static uint8_t g_gateway[4];
static uint8_t g_dns[4];

static void dhcp_rx(const uint8_t *src_ip, uint16_t src_port,
                    const uint8_t *data, uint16_t len, void *ctx);

static void dhcp_send_discover(void) {
    static uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN + DHCP_BUF_SIZE];
    dhcp_pkt_t *pkt;
    const net_info_t *ni = net_get_info();
    uint8_t bcast[4] = {255, 255, 255, 255};
    int i;

    mem_set(frame, 0, sizeof(frame));
    pkt = (dhcp_pkt_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN);

    pkt->op    = 1;
    pkt->htype = 1;
    pkt->hlen  = 6;
    pkt->xid   = bswap32(g_xid);
    pkt->flags = bswap16(0x8000u);  /* broadcast flag */
    mem_copy(pkt->chaddr, ni->mac, 6);
    pkt->magic = bswap32(0x63825363u);

    /* Options */
    i = 0;
    pkt->options[i++] = 53;  /* DHCP message type */
    pkt->options[i++] = 1;
    pkt->options[i++] = DHCP_DISCOVER;
    pkt->options[i++] = 55;  /* Parameter request list */
    pkt->options[i++] = 4;
    pkt->options[i++] = 1;   /* subnet mask */
    pkt->options[i++] = 3;   /* router */
    pkt->options[i++] = 6;   /* DNS */
    pkt->options[i++] = 15;  /* domain name */
    pkt->options[i++] = 255; /* end */

    udp_send(bcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, frame,
             DHCP_BUF_SIZE);
}

static void dhcp_send_request(void) {
    static uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN + DHCP_BUF_SIZE];
    dhcp_pkt_t *pkt;
    const net_info_t *ni = net_get_info();
    uint8_t bcast[4] = {255, 255, 255, 255};
    int i;

    mem_set(frame, 0, sizeof(frame));
    pkt = (dhcp_pkt_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN + UDP_HEADER_LEN);

    pkt->op    = 1;
    pkt->htype = 1;
    pkt->hlen  = 6;
    pkt->xid   = bswap32(g_xid);
    pkt->flags = bswap16(0x8000u);
    mem_copy(pkt->chaddr, ni->mac, 6);
    pkt->magic = bswap32(0x63825363u);

    i = 0;
    pkt->options[i++] = 53; pkt->options[i++] = 1; pkt->options[i++] = DHCP_REQUEST;
    pkt->options[i++] = 50; pkt->options[i++] = 4; /* requested IP */
    mem_copy(&pkt->options[i], g_offered_ip, 4); i += 4;
    pkt->options[i++] = 54; pkt->options[i++] = 4; /* server ID */
    mem_copy(&pkt->options[i], g_server_ip, 4); i += 4;
    pkt->options[i++] = 255;

    udp_send(bcast, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, frame,
             DHCP_BUF_SIZE);
}

static void parse_offer(const dhcp_pkt_t *pkt, uint16_t opt_len) {
    const uint8_t *o = pkt->options;
    uint16_t i = 0;

    mem_copy(g_offered_ip, pkt->yiaddr, 4);

    while (i < opt_len) {
        uint8_t tag = o[i++];
        if (tag == 255) break;
        if (tag == 0)   continue;
        if (i >= opt_len) break;
        uint8_t olen = o[i++];
        if (i + olen > opt_len) break;

        if (tag == 1 && olen == 4) mem_copy(g_subnet, &o[i], 4);
        if (tag == 3 && olen >= 4) mem_copy(g_gateway, &o[i], 4);
        if (tag == 6 && olen >= 4) mem_copy(g_dns, &o[i], 4);
        if (tag == 54 && olen == 4) mem_copy(g_server_ip, &o[i], 4);
        i += olen;
    }
}

static void dhcp_rx(const uint8_t *src_ip, uint16_t src_port,
                    const uint8_t *data, uint16_t len, void *ctx) {
    const dhcp_pkt_t *pkt;
    net_info_t *ni;
    uint8_t msg_type = 0;
    const uint8_t *o;
    uint16_t i;
    (void)src_ip; (void)src_port; (void)ctx;

    if (len < (uint16_t)sizeof(dhcp_pkt_t)) return;
    pkt = (const dhcp_pkt_t *)data;
    if (bswap32(pkt->magic) != 0x63825363u) return;
    if (bswap32(pkt->xid) != g_xid) return;

    /* Find message type option */
    o = pkt->options;
    for (i = 0; i < 312; ) {
        uint8_t tag = o[i++];
        if (tag == 255) break;
        if (tag == 0)   continue;
        uint8_t olen = o[i++];
        if (tag == 53 && olen == 1) { msg_type = o[i]; }
        i += olen;
    }

    if (msg_type == DHCP_OFFER && g_state == DHCP_STATE_DISCOVER) {
        parse_offer(pkt, 312);
        g_state = DHCP_STATE_OFFER_RECEIVED;
        dhcp_send_request();
        g_state = DHCP_STATE_REQUEST;
    } else if (msg_type == DHCP_ACK && g_state == DHCP_STATE_REQUEST) {
        parse_offer(pkt, 312);
        ni = (net_info_t *)(uintptr_t)net_get_info(); /* cast away const for update */
        /* Note: net.c exposes net_apply_dhcp for this */
        net_apply_dhcp(g_offered_ip, g_subnet, g_gateway, g_dns);
        g_state  = DHCP_STATE_BOUND;
        g_bound  = 1;
    }
}

void dhcp_start(void) {
    g_state  = DHCP_STATE_IDLE;
    g_bound  = 0;
    mem_set(g_offered_ip, 0, 4);
    mem_set(g_server_ip, 0, 4);
    mem_set(g_subnet, 0, 4);
    mem_set(g_gateway, 0, 4);
    mem_set(g_dns, 0, 4);

    udp_register(DHCP_CLIENT_PORT, dhcp_rx, 0);

    g_state     = DHCP_STATE_DISCOVER;
    g_send_tick = timer_get_ticks();
    dhcp_send_discover();
    g_retry_tick = timer_get_ticks();
}

int dhcp_poll(void) {
    if (g_state == DHCP_STATE_BOUND) return 0;
    if (g_state == DHCP_STATE_IDLE)  return 0;

    /* Retry DISCOVER every ~3 seconds if still waiting */
    uint32_t now = timer_get_ticks();
    if (g_state == DHCP_STATE_DISCOVER && (now - g_retry_tick) > 300u) {
        dhcp_send_discover();
        g_retry_tick = now;
    }
    return 0;
}

int dhcp_bound(void) {
    return g_bound;
}
