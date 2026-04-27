#ifndef CRYPTO_AES_H
#define CRYPTO_AES_H

#include <stdint.h>

void aes128_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void aes128_decrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

int aes_key_unwrap(const uint8_t *kek, int kek_len,
                   const uint8_t *in,  int in_len,
                   uint8_t *out);

#endif
