
#include "crypto/sha1.h"

static uint32_t rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void put_be32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static void put_be64(uint8_t *p, uint64_t v) {
    put_be32(p,   (uint32_t)(v >> 32));
    put_be32(p+4, (uint32_t)(v));
}

static void sha1_block(uint32_t h[5], const uint8_t blk[64]) {
    uint32_t w[80], a, b, c, d, e, f, k, tmp;
    int i;

    for (i = 0;  i < 16; i++) w[i] = be32(blk + i*4);
    for (i = 16; i < 80; i++) w[i] = rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);

    a=h[0]; b=h[1]; c=h[2]; d=h[3]; e=h[4];

    for (i = 0; i < 80; i++) {
        if      (i < 20) { f=(b&c)|((~b)&d); k=0x5a827999; }
        else if (i < 40) { f=b^c^d;          k=0x6ed9eba1; }
        else if (i < 60) { f=(b&c)|(b&d)|(c&d); k=0x8f1bbcdc; }
        else             { f=b^c^d;          k=0xca62c1d6; }
        tmp = rol32(a,5)+f+e+k+w[i];
        e=d; d=c; c=rol32(b,30); b=a; a=tmp;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
}

void sha1(const uint8_t *data, uint32_t len, uint8_t digest[20]) {
    uint32_t h[5] = { 0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0 };
    uint8_t buf[64];
    uint32_t i, used = len & 63;
    uint64_t bits = (uint64_t)len * 8;

    for (i = 0; i + 64 <= len; i += 64) sha1_block(h, data + i);

    for (i = 0; i < used; i++) buf[i] = data[len - used + i];
    buf[used++] = 0x80;
    if (used > 56) {
        while (used < 64) buf[used++] = 0;
        sha1_block(h, buf);
        used = 0;
    }
    while (used < 56) buf[used++] = 0;
    put_be64(buf + 56, bits);
    sha1_block(h, buf);

    for (i = 0; i < 5; i++) put_be32(digest + i*4, h[i]);
}

void hmac_sha1_vector(const uint8_t *key, uint32_t key_len,
                      int num_elem,
                      const uint8_t **elems, const uint32_t *elem_lens,
                      uint8_t mac[20])
{
    uint8_t k[64], ipad[64], opad[64], inner[20];
    uint32_t i, j, total;

    if (key_len > 64) {
        sha1(key, key_len, k);
        key = k; key_len = 20;
    }

    for (i = 0; i < 64; i++) {
        uint8_t kb = (i < key_len) ? key[i] : 0;
        ipad[i] = kb ^ 0x36;
        opad[i] = kb ^ 0x5c;
    }

    {
        uint32_t h[5] = { 0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0 };
        uint8_t buf[64];
        uint32_t buf_used = 0;
        uint64_t processed = 64;

        sha1_block(h, ipad);

        total = 0;
        for (j = 0; j < (uint32_t)num_elem; j++) total += elem_lens[j];
        processed += total;

        for (j = 0; j < (uint32_t)num_elem; j++) {
            const uint8_t *src = elems[j];
            uint32_t rem = elem_lens[j];
            while (rem > 0) {
                uint32_t take = 64 - buf_used;
                if (take > rem) take = rem;
                for (i = 0; i < take; i++) buf[buf_used+i] = src[i];
                buf_used += take;
                src += take;
                rem -= take;
                if (buf_used == 64) { sha1_block(h, buf); buf_used = 0; }
            }
        }

        buf[buf_used++] = 0x80;
        if (buf_used > 56) {
            while (buf_used < 64) buf[buf_used++] = 0;
            sha1_block(h, buf);
            buf_used = 0;
        }
        while (buf_used < 56) buf[buf_used++] = 0;
        put_be64(buf+56, processed * 8);
        sha1_block(h, buf);
        for (i = 0; i < 5; i++) put_be32(inner + i*4, h[i]);
    }

    {
        const uint8_t *elems2[2] = { opad, inner };
        uint32_t lens2[2] = { 64, 20 };
        hmac_sha1_vector(NULL, 0, 2, elems2, lens2, mac);

    }

    {
        uint32_t h[5] = { 0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0 };
        uint8_t buf[64];
        sha1_block(h, opad);

        for (i = 0; i < 20; i++) buf[i] = inner[i];
        buf[20] = 0x80;
        for (i = 21; i < 56; i++) buf[i] = 0;
        put_be64(buf+56, (uint64_t)(64+20)*8);
        sha1_block(h, buf);
        for (i = 0; i < 5; i++) put_be32(mac + i*4, h[i]);
    }
}

void hmac_sha1(const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t mac[20])
{
    const uint8_t *elems[1] = { data };
    uint32_t lens[1] = { data_len };
    hmac_sha1_vector(key, key_len, 1, elems, lens, mac);
}

void pbkdf2_sha1(const uint8_t *passphrase, uint32_t pass_len,
                 const uint8_t *salt,       uint32_t salt_len,
                 uint32_t iterations,
                 uint8_t *out, uint32_t out_len)
{
    uint32_t block_num = 1;
    uint32_t offset = 0;
    uint8_t u[20], t[20], cnt_buf[4];
    uint32_t i, j, k;
    const uint8_t *elems[2];
    uint32_t   lens[2];

    while (offset < out_len) {

        put_be32(cnt_buf, block_num);
        elems[0] = salt;   lens[0] = salt_len;
        elems[1] = cnt_buf; lens[1] = 4;
        hmac_sha1_vector(passphrase, pass_len, 2, elems, lens, u);
        for (j = 0; j < 20; j++) t[j] = u[j];

        for (i = 1; i < iterations; i++) {
            hmac_sha1(passphrase, pass_len, u, 20, u);
            for (j = 0; j < 20; j++) t[j] ^= u[j];
        }

        k = out_len - offset;
        if (k > 20) k = 20;
        for (j = 0; j < k; j++) out[offset+j] = t[j];
        offset += k;
        block_num++;
    }
}

void prf_sha1(const uint8_t *key,    uint32_t key_len,
              const uint8_t *label,  uint32_t label_len,
              const uint8_t *data,   uint32_t data_len,
              uint8_t *out, uint32_t out_len)
{
    uint32_t offset = 0;
    uint8_t i = 0;
    uint8_t zero = 0;
    const uint8_t *elems[4];
    uint32_t lens[4];

    while (offset < out_len) {
        uint8_t mac[20];
        uint32_t take;
        elems[0] = label;  lens[0] = label_len;
        elems[1] = &zero;  lens[1] = 1;
        elems[2] = data;   lens[2] = data_len;
        elems[3] = &i;     lens[3] = 1;
        hmac_sha1_vector(key, key_len, 4, elems, lens, mac);
        take = out_len - offset;
        if (take > 20) take = 20;
        for (uint32_t j = 0; j < take; j++) out[offset+j] = mac[j];
        offset += take;
        i++;
    }
}
