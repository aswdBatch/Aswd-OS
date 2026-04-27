#ifndef CRYPTO_SHA1_H
#define CRYPTO_SHA1_H

#include <stdint.h>

void sha1(const uint8_t *data, uint32_t len, uint8_t digest[20]);

void hmac_sha1(const uint8_t *key,  uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t mac[20]);

void hmac_sha1_vector(const uint8_t *key, uint32_t key_len,
                      int num_elem,
                      const uint8_t **elems, const uint32_t *elem_lens,
                      uint8_t mac[20]);

void pbkdf2_sha1(const uint8_t *passphrase, uint32_t pass_len,
                 const uint8_t *salt,       uint32_t salt_len,
                 uint32_t iterations,
                 uint8_t *out, uint32_t out_len);

void prf_sha1(const uint8_t *key,    uint32_t key_len,
              const uint8_t *label,  uint32_t label_len,
              const uint8_t *data,   uint32_t data_len,
              uint8_t *out, uint32_t out_len);

#endif
