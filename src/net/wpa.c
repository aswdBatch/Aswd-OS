
#include "net/wpa.h"
#include "crypto/aes.h"
#include "crypto/sha1.h"
#include "lib/string.h"

static uint8_t g_pmk[32];
static uint8_t g_ptk[64];
static uint8_t g_gtk[32];
static uint8_t g_gtk_len;
static uint8_t g_snonce[32];
static uint8_t g_anonce[32];
static uint8_t g_ap_mac[6];
static uint8_t g_sta_mac_wpa[6];
static wpa_state_t g_wpa_state = WPA_STATE_IDLE;

static uint8_t g_tx_pn[6] = { 0,0,0,0,0,1 };

#define KCK_OFF  0
#define KEK_OFF  16
#define TK_OFF   32

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
static void put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void mac_min_max(const uint8_t *a, const uint8_t *b,
                        uint8_t *mn, uint8_t *mx)
{
    int i;
    for (i = 0; i < 6; i++) {
        if (a[i] < b[i]) { mem_copy(mn,a,6); mem_copy(mx,b,6); return; }
        if (a[i] > b[i]) { mem_copy(mn,b,6); mem_copy(mx,a,6); return; }
    }
    mem_copy(mn,a,6); mem_copy(mx,b,6);
}

static void nonce_min_max(const uint8_t *a, const uint8_t *b,
                          uint8_t *mn, uint8_t *mx)
{
    int i;
    for (i = 0; i < 32; i++) {
        if (a[i] < b[i]) { mem_copy(mn,a,32); mem_copy(mx,b,32); return; }
        if (a[i] > b[i]) { mem_copy(mn,b,32); mem_copy(mx,a,32); return; }
    }
    mem_copy(mn,a,32); mem_copy(mx,b,32);
}

static void generate_snonce(uint8_t out[32]) {
    static uint8_t ctr = 0;
    uint8_t buf[32 + 6 + 6 + 1];
    mem_copy(buf, g_pmk, 32);
    mem_copy(buf+32, g_ap_mac, 6);
    mem_copy(buf+38, g_sta_mac_wpa, 6);
    buf[44] = ctr++;
    sha1(buf, 45, out);
    sha1(out, 20, out+12);

    uint8_t tmp[20];
    sha1(buf+1, 44, tmp);
    mem_copy(out+12, tmp, 20);
}

#define EAPOL_HDR         4
#define EKEY_DESC         0
#define EKEY_INFO         1
#define EKEY_LEN          3
#define EKEY_REPLAY       5
#define EKEY_NONCE        13
#define EKEY_IV           45
#define EKEY_RSC          61
#define EKEY_ID           69
#define EKEY_MIC          77
#define EKEY_DATALEN      93
#define EKEY_DATA         95
#define EKEY_BODY_MIN     95

static uint16_t ekey_info(const uint8_t *body) {
    return be16(body + EKEY_INFO);
}

static int build_eapol_msg2(uint8_t *out, const uint8_t *replay_ctr) {

    int n = 0;
    uint16_t kinfo = EAPOL_KEY_TYPE_PAIRWISE | EAPOL_KEY_MIC;

    out[n++] = 0x02;
    out[n++] = 0x03;

    out[n++] = 0; out[n++] = 0;

    int body_start = n;

    out[n++] = EAPOL_KEY_DESC_RSN;

    put_be16(out+n, kinfo); n+=2;

    put_be16(out+n, 0); n+=2;

    mem_copy(out+n, replay_ctr, 8); n+=8;

    mem_copy(out+n, g_snonce, 32); n+=32;

    for (int i=0;i<16+8+8;i++) out[n++]=0;

    int mic_off = n - body_start + EAPOL_HDR;
    for (int i=0;i<16;i++) out[n++]=0;

    put_be16(out+n, 0); n+=2;

    put_be16(out+2, (uint16_t)(n - EAPOL_HDR));

    uint8_t mac[20];
    hmac_sha1(g_ptk + KCK_OFF, 16, out, n, mac);
    mem_copy(out + mic_off, mac, 16);

    (void)body_start;
    return n;
}

static int build_eapol_msg4(uint8_t *out, const uint8_t *replay_ctr) {
    int n = 0;
    uint16_t kinfo = EAPOL_KEY_TYPE_PAIRWISE | EAPOL_KEY_MIC | EAPOL_KEY_SECURE;

    out[n++] = 0x02; out[n++] = 0x03;
    out[n++] = 0; out[n++] = 0;

    int body_start = n;

    out[n++] = EAPOL_KEY_DESC_RSN;
    put_be16(out+n, kinfo); n+=2;
    put_be16(out+n, 0); n+=2;
    mem_copy(out+n, replay_ctr, 8); n+=8;
    for (int i=0;i<32;i++) out[n++]=0;
    for (int i=0;i<16+8+8;i++) out[n++]=0;
    int mic_off = n;
    for (int i=0;i<16;i++) out[n++]=0;
    put_be16(out+n, 0); n+=2;

    put_be16(out+2, (uint16_t)(n - EAPOL_HDR));

    uint8_t mac[20];
    hmac_sha1(g_ptk + KCK_OFF, 16, out, n, mac);
    mem_copy(out + mic_off, mac, 16);

    (void)body_start;
    return n;
}

void wpa_init(const char *ssid, const char *passphrase, const uint8_t *ap_mac) {
    uint32_t pass_len = (uint32_t)str_len(passphrase);
    uint32_t ssid_len = (uint32_t)str_len(ssid);

    pbkdf2_sha1((const uint8_t *)passphrase, pass_len,
                (const uint8_t *)ssid,       ssid_len,
                4096, g_pmk, 32);

    mem_copy(g_ap_mac, ap_mac, 6);

    extern uint8_t g_sta_mac[6];
    mem_copy(g_sta_mac_wpa, g_sta_mac, 6);

    mem_set(g_ptk, 0, sizeof(g_ptk));
    mem_set(g_gtk, 0, sizeof(g_gtk));
    g_gtk_len = 0;
    g_wpa_state = WPA_STATE_WAIT_MSG1;
}

wpa_state_t wpa_state(void) { return g_wpa_state; }

int wpa_rx_eapol(const uint8_t *frame, int frame_len,
                 uint8_t *out_buf, int *out_len)
{
    *out_len = 0;

    if (frame_len < EAPOL_HDR + EKEY_BODY_MIN) return 0;
    if (frame[1] != 0x03) return 0;

    const uint8_t *body   = frame + EAPOL_HDR;
    int            body_len = frame_len - EAPOL_HDR;
    (void)body_len;

    if (body[EKEY_DESC] != EAPOL_KEY_DESC_RSN) return 0;

    uint16_t kinfo = ekey_info(body);
    const uint8_t *replay_ctr = body + EKEY_REPLAY;
    const uint8_t *nonce      = body + EKEY_NONCE;

    if ((kinfo & EAPOL_KEY_TYPE_PAIRWISE) && (kinfo & EAPOL_KEY_ACK) &&
        !(kinfo & EAPOL_KEY_MIC) && g_wpa_state == WPA_STATE_WAIT_MSG1)
    {

        mem_copy(g_anonce, nonce, 32);
        generate_snonce(g_snonce);

        uint8_t label[] = "Pairwise key expansion";
        uint8_t data[6+6+32+32];
        uint8_t mn_mac[6], mx_mac[6], mn_nonce[32], mx_nonce[32];
        mac_min_max(g_ap_mac, g_sta_mac_wpa, mn_mac, mx_mac);
        nonce_min_max(g_anonce, g_snonce, mn_nonce, mx_nonce);
        mem_copy(data,    mn_mac,   6);
        mem_copy(data+6,  mx_mac,   6);
        mem_copy(data+12, mn_nonce, 32);
        mem_copy(data+44, mx_nonce, 32);
        prf_sha1(g_pmk, 32, label, 22, data, 76, g_ptk, 64);

        int len = build_eapol_msg2(out_buf, replay_ctr);
        *out_len = len;
        g_wpa_state = WPA_STATE_WAIT_MSG3;
        return 1;
    }

    if ((kinfo & EAPOL_KEY_TYPE_PAIRWISE) && (kinfo & EAPOL_KEY_ACK) &&
        (kinfo & EAPOL_KEY_MIC) && g_wpa_state == WPA_STATE_WAIT_MSG3)
    {

        uint8_t tmp[512];
        int flen = frame_len;
        if (flen > (int)sizeof(tmp)) return -1;
        mem_copy(tmp, frame, flen);
        mem_set(tmp + EAPOL_HDR + EKEY_MIC, 0, 16);
        uint8_t mic[20];
        hmac_sha1(g_ptk + KCK_OFF, 16, tmp, flen, mic);

        const uint8_t *frame_mic = body + EKEY_MIC;
        for (int i = 0; i < 16; i++) {
            if (mic[i] != frame_mic[i]) {
                g_wpa_state = WPA_STATE_FAILED;
                return -1;
            }
        }

        uint16_t kdata_len = be16(body + EKEY_DATALEN);
        if (kdata_len >= 8 && kdata_len <= 64) {
            uint8_t gtk_plain[64];
            int unwrap_len = (int)kdata_len - 8;
            if (unwrap_len > 0 &&
                aes_key_unwrap(g_ptk + KEK_OFF, 16,
                               body + EKEY_DATA, kdata_len,
                               gtk_plain) == 0)
            {

                if (unwrap_len >= 8 &&
                    gtk_plain[0] == 0xdd &&
                    gtk_plain[2] == 0x00 && gtk_plain[3] == 0x0f &&
                    gtk_plain[4] == 0xac && gtk_plain[5] == 0x01)
                {
                    int gtk_key_len = unwrap_len - 8;
                    if (gtk_key_len > (int)sizeof(g_gtk))
                        gtk_key_len = sizeof(g_gtk);
                    mem_copy(g_gtk, gtk_plain + 8, gtk_key_len);
                    g_gtk_len = (uint8_t)gtk_key_len;
                }
            }
        }

        int len = build_eapol_msg4(out_buf, replay_ctr);
        *out_len = len;
        g_wpa_state = WPA_STATE_CONNECTED;
        return 1;
    }

    return 0;
}

static void ccmp_nonce(uint8_t nonce[13], const uint8_t *addr, const uint8_t pn[6],
                       uint8_t tid)
{
    nonce[0] = tid & 0x0f;
    mem_copy(nonce + 1, addr, 6);

    nonce[7]  = pn[5]; nonce[8]  = pn[4];
    nonce[9]  = pn[3]; nonce[10] = pn[2];
    nonce[11] = pn[1]; nonce[12] = pn[0];
}

static void aes_ctr(const uint8_t key[16], const uint8_t nonce[13],
                    uint16_t ctr_start, const uint8_t *in, uint8_t *out, int len)
{
    uint8_t blk[16], ks[16];
    uint16_t ctr = ctr_start;
    int i = 0;
    while (i < len) {

        blk[0] = 0x01;
        mem_copy(blk+1, nonce, 13);
        blk[14] = (uint8_t)(ctr >> 8);
        blk[15] = (uint8_t)ctr;
        aes128_encrypt(key, blk, ks);
        int take = len - i;
        if (take > 16) take = 16;
        for (int j = 0; j < take; j++) out[i+j] = in[i+j] ^ ks[j];
        i += take;
        ctr++;
    }
}

static void ccmp_cbc_mac(const uint8_t key[16], const uint8_t nonce[13],
                         const uint8_t *aad, int aad_len,
                         const uint8_t *data, int data_len,
                         uint8_t mic[8])
{
    uint8_t b[16], x[16];
    int i, j;

    b[0] = 0x59;
    mem_copy(b+1, nonce, 13);
    b[14] = (uint8_t)(data_len >> 8);
    b[15] = (uint8_t)data_len;
    aes128_encrypt(key, b, x);

    uint8_t aad_block[16];
    aad_block[0] = 0;
    aad_block[1] = (uint8_t)aad_len;
    int aad_in_first = 14;
    if (aad_in_first > aad_len) aad_in_first = aad_len;
    mem_copy(aad_block+2, aad, aad_in_first);
    for (i = 2 + aad_in_first; i < 16; i++) aad_block[i] = 0;
    for (j = 0; j < 16; j++) x[j] ^= aad_block[j];
    aes128_encrypt(key, x, x);

    int aad_done = aad_in_first;
    while (aad_done < aad_len) {
        mem_set(aad_block, 0, 16);
        int take = aad_len - aad_done;
        if (take > 16) take = 16;
        mem_copy(aad_block, aad + aad_done, take);
        for (j = 0; j < 16; j++) x[j] ^= aad_block[j];
        aes128_encrypt(key, x, x);
        aad_done += take;
    }

    i = 0;
    while (i < data_len) {
        uint8_t blk2[16];
        mem_set(blk2, 0, 16);
        int take = data_len - i;
        if (take > 16) take = 16;
        mem_copy(blk2, data+i, take);
        for (j = 0; j < 16; j++) x[j] ^= blk2[j];
        aes128_encrypt(key, x, x);
        i += take;
    }

    uint8_t s0[16];
    b[0] = 0x01;
    mem_copy(b+1, nonce, 13);
    b[14] = 0; b[15] = 0;
    aes128_encrypt(key, b, s0);
    for (j = 0; j < 8; j++) mic[j] = x[j] ^ s0[j];
}

int wpa_ccmp_encrypt(const uint8_t *in, int in_len, uint8_t *out,
                     const uint8_t pn[6], const uint8_t key[16],
                     const uint8_t addr[6], uint8_t tid)
{
    uint8_t nonce[13];
    uint8_t mic[8];
    uint8_t aad[22];
    int i;

    ccmp_nonce(nonce, addr, pn, tid);

    mem_copy(aad, addr, 6);
    mem_copy(aad+6, addr, 6);
    aad[12] = tid;
    mem_set(aad+13, 0, 9);
    int aad_len = 22;

    if (out != in) mem_copy(out, in, in_len);
    aes_ctr(key, nonce, 1, in, out, in_len);

    ccmp_cbc_mac(key, nonce, aad, aad_len, in, in_len, mic);

    for (i = 0; i < 8; i++) out[in_len + i] = mic[i];

    return in_len + 8;
}

int wpa_ccmp_decrypt(const uint8_t *in, int in_len, uint8_t *out,
                     const uint8_t pn[6], const uint8_t key[16],
                     const uint8_t addr[6], uint8_t tid)
{
    uint8_t nonce[13];
    uint8_t mic_recv[8], mic_calc[8];
    uint8_t aad[22];
    int data_len, i;

    if (in_len < 8) return -1;
    data_len = in_len - 8;

    ccmp_nonce(nonce, addr, pn, tid);

    mem_copy(aad, addr, 6);
    mem_copy(aad+6, addr, 6);
    aad[12] = tid;
    mem_set(aad+13, 0, 9);
    int aad_len = 22;

    aes_ctr(key, nonce, 1, in, out, data_len);

    ccmp_cbc_mac(key, nonce, aad, aad_len, out, data_len, mic_calc);
    for (i = 0; i < 8; i++) mic_recv[i] = in[data_len + i];
    for (i = 0; i < 8; i++) {
        if (mic_recv[i] != mic_calc[i]) return -1;
    }

    return data_len;
}

const uint8_t *wpa_ptk_tk(void)  { return g_ptk + TK_OFF; }
const uint8_t *wpa_gtk(void)     { return g_gtk; }
