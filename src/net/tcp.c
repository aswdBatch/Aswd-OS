#include "net/tcp.h"

#include <stdint.h>

#include "cpu/timer.h"
#include "net/ethernet.h"
#include "net/ip.h"
#include "net/net.h"
#include "lib/string.h"

/* TCP header */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;   /* upper 4 bits = header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_header_t;

#define TCP_HDR_LEN 20

/* TCP flag bits */
#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

/* RX ring buffer */
#define TCP_RX_BUF 4096

typedef enum {
    TCP_ST_CLOSED = 0,
    TCP_ST_SYN_SENT,
    TCP_ST_ESTABLISHED,
    TCP_ST_FIN_WAIT,
    TCP_ST_CLOSE_WAIT,
} tcp_state_t;

static tcp_state_t g_state     = TCP_ST_CLOSED;
static uint8_t     g_remote_ip[4];
static uint16_t    g_remote_port;
static uint16_t    g_local_port = 49152u;
static uint32_t    g_snd_seq;   /* next seq to send */
static uint32_t    g_rcv_nxt;   /* next expected from remote */

/* RX ring */
static uint8_t  g_rx_buf[TCP_RX_BUF];
static uint16_t g_rx_head = 0;
static uint16_t g_rx_tail = 0;

static uint16_t rx_avail(void) {
    return (uint16_t)((g_rx_head - g_rx_tail) & (TCP_RX_BUF - 1));
}

static void rx_push(const uint8_t *data, uint16_t len) {
    uint16_t i;
    for (i = 0; i < len; i++) {
        g_rx_buf[g_rx_head & (TCP_RX_BUF - 1)] = data[i];
        g_rx_head++;
    }
}

/* Pseudo-header checksum for TCP */
static uint16_t tcp_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                              const void *tcp_seg, uint16_t tcp_len) {
    uint32_t sum = 0;
    const uint16_t *p;
    uint16_t n;
    int i;

    /* Pseudo header */
    for (i = 0; i < 4; i += 2)
        sum += (uint16_t)((uint16_t)src_ip[i] << 8 | src_ip[i+1]);
    for (i = 0; i < 4; i += 2)
        sum += (uint16_t)((uint16_t)dst_ip[i] << 8 | dst_ip[i+1]);
    sum += IP_PROTO_TCP;
    sum += tcp_len;

    p = (const uint16_t *)tcp_seg;
    n = tcp_len;
    while (n > 1) { sum += *p++; n -= 2; }
    if (n) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

static int tcp_send_segment(uint8_t flags, const uint8_t *data, uint16_t data_len) {
    const net_info_t *ni = net_get_info();
    uint8_t frame[ETH_HEADER_LEN + IP_HEADER_LEN + TCP_HDR_LEN + 1500];
    tcp_header_t *th = (tcp_header_t *)(frame + ETH_HEADER_LEN + IP_HEADER_LEN);
    uint16_t tcp_len = (uint16_t)(TCP_HDR_LEN + data_len);

    if (data_len > 1500) data_len = 1500;

    th->src_port  = bswap16(g_local_port);
    th->dst_port  = bswap16(g_remote_port);
    th->seq       = bswap32(g_snd_seq);
    th->ack       = bswap32(g_rcv_nxt);
    th->data_off  = (uint8_t)((TCP_HDR_LEN / 4) << 4);
    th->flags     = flags;
    th->window    = bswap16(4096);
    th->checksum  = 0;
    th->urgent    = 0;

    if (data && data_len)
        mem_copy((uint8_t *)th + TCP_HDR_LEN, data, data_len);

    th->checksum = tcp_checksum(ni->ip, g_remote_ip, th, tcp_len);

    return ip_send(g_remote_ip, IP_PROTO_TCP, frame, tcp_len);
}

void tcp_rx(const uint8_t *src_ip, const uint8_t *pkt, uint16_t len) {
    const tcp_header_t *th;
    uint8_t  data_off;
    uint16_t hdr_len;
    const uint8_t *payload;
    uint16_t pay_len;
    uint8_t  flags;

    if (len < TCP_HDR_LEN) return;
    th = (const tcp_header_t *)pkt;

    /* Check if this is for our connection */
    if (g_state == TCP_ST_CLOSED) return;
    if (bswap16(th->dst_port) != g_local_port) return;

    int i;
    for (i = 0; i < 4; i++) if (src_ip[i] != g_remote_ip[i]) return;
    if (bswap16(th->src_port) != g_remote_port) return;

    flags    = th->flags;
    data_off = (uint8_t)(th->data_off >> 4);
    hdr_len  = (uint16_t)(data_off * 4);
    if (hdr_len < TCP_HDR_LEN || hdr_len > len) return;
    payload  = pkt + hdr_len;
    pay_len  = (uint16_t)(len - hdr_len);

    if (flags & TCP_RST) {
        g_state = TCP_ST_CLOSED;
        return;
    }

    if (g_state == TCP_ST_SYN_SENT && (flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        g_rcv_nxt = bswap32(th->seq) + 1;
        g_snd_seq = bswap32(th->ack);
        g_state   = TCP_ST_ESTABLISHED;
        tcp_send_segment(TCP_ACK, 0, 0);
        return;
    }

    if (g_state == TCP_ST_ESTABLISHED || g_state == TCP_ST_FIN_WAIT) {
        /* Data */
        if (pay_len > 0) {
            rx_push(payload, pay_len);
            g_rcv_nxt += pay_len;
            tcp_send_segment(TCP_ACK, 0, 0);
        }
        /* FIN */
        if (flags & TCP_FIN) {
            g_rcv_nxt++;
            tcp_send_segment(TCP_ACK, 0, 0);
            if (g_state == TCP_ST_ESTABLISHED)
                g_state = TCP_ST_CLOSE_WAIT;
            else
                g_state = TCP_ST_CLOSED;
        }
    }
}

int tcp_connect(const uint8_t *dst_ip, uint16_t dst_port) {
    uint32_t deadline;
    int i;

    if (g_state != TCP_ST_CLOSED) tcp_close();

    mem_copy(g_remote_ip, dst_ip, 4);
    g_remote_port = dst_port;
    g_local_port  = (uint16_t)(g_local_port + 1);
    if (g_local_port < 49152u) g_local_port = 49152u;
    g_snd_seq     = 0x12345678u;
    g_rcv_nxt     = 0;
    g_rx_head     = 0;
    g_rx_tail     = 0;
    g_state       = TCP_ST_SYN_SENT;

    /* Send SYN */
    for (i = 0; i < 3; i++) {
        tcp_send_segment(TCP_SYN, 0, 0);
        g_snd_seq++;  /* SYN consumes one seq number */

        deadline = timer_get_ticks() + 200u;  /* 2s */
        while (g_state == TCP_ST_SYN_SENT && timer_get_ticks() < deadline) {
            net_poll();
            __asm__ volatile("sti; hlt");
        }
        if (g_state == TCP_ST_ESTABLISHED) return 0;
        if (g_state == TCP_ST_CLOSED) return -1;
        g_snd_seq--;  /* revert before retry */
    }
    g_state = TCP_ST_CLOSED;
    return -1;
}

int tcp_send_data(const uint8_t *data, uint16_t len) {
    uint16_t sent = 0;
    while (sent < len) {
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > 1460) chunk = 1460;
        int rc = tcp_send_segment(TCP_PSH | TCP_ACK, data + sent, chunk);
        if (rc != 0) return -1;
        g_snd_seq += chunk;
        sent += chunk;
    }
    return (int)sent;
}

int tcp_recv_data(uint8_t *buf, uint16_t max_len) {
    uint16_t avail;
    uint16_t n;
    uint16_t i;

    if (g_state == TCP_ST_CLOSED && rx_avail() == 0) return -1;
    avail = rx_avail();
    n = avail < max_len ? avail : max_len;
    for (i = 0; i < n; i++) {
        buf[i] = g_rx_buf[g_rx_tail & (TCP_RX_BUF - 1)];
        g_rx_tail++;
    }
    return (int)n;
}

void tcp_close(void) {
    if (g_state == TCP_ST_ESTABLISHED || g_state == TCP_ST_CLOSE_WAIT) {
        tcp_send_segment(TCP_FIN | TCP_ACK, 0, 0);
        g_snd_seq++;
        /* Brief wait for FIN-ACK */
        uint32_t deadline = timer_get_ticks() + 50u;
        while (g_state != TCP_ST_CLOSED && timer_get_ticks() < deadline) {
            net_poll();
            __asm__ volatile("sti; hlt");
        }
    }
    g_state = TCP_ST_CLOSED;
}

int tcp_connected(void) {
    return g_state == TCP_ST_ESTABLISHED || g_state == TCP_ST_CLOSE_WAIT;
}
