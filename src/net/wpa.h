#ifndef NET_WPA_H
#define NET_WPA_H

#include <stdint.h>

#define EAPOL_KEY_DESC_RSN          2

#define EAPOL_KEY_TYPE_PAIRWISE     0x0008
#define EAPOL_KEY_ACK               0x0080
#define EAPOL_KEY_MIC               0x0100
#define EAPOL_KEY_SECURE            0x0200
#define EAPOL_KEY_ENCRYPTED         0x1000
#define EAPOL_KEY_SMK               0x2000

typedef enum {
    WPA_STATE_IDLE = 0,
    WPA_STATE_WAIT_MSG1,
    WPA_STATE_WAIT_MSG3,
    WPA_STATE_CONNECTED,
    WPA_STATE_FAILED,
} wpa_state_t;

void wpa_init(const char *ssid, const char *passphrase, const uint8_t *ap_mac);

int wpa_rx_eapol(const uint8_t *frame, int frame_len,
                 uint8_t *out_buf, int *out_len);

wpa_state_t wpa_state(void);

int  wpa_ccmp_encrypt(const uint8_t *in,  int in_len,
                      uint8_t       *out,
                      const uint8_t  pn[6],
                      const uint8_t  key[16],
                      const uint8_t  addr[6],
                      uint8_t        tid);

int  wpa_ccmp_decrypt(const uint8_t *in,  int in_len,
                      uint8_t       *out,
                      const uint8_t  pn[6],
                      const uint8_t  key[16],
                      const uint8_t  addr[6],
                      uint8_t        tid);

const uint8_t *wpa_ptk_tk(void);

const uint8_t *wpa_gtk(void);

#endif
