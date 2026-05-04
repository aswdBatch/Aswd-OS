#include "usb/hid.h"

void usb_hid_init(void) {
}

void usb_hid_parse_boot_mouse(const uint8_t *rpt, unsigned len,
                              int *out_dx, int *out_dy,
                              uint8_t *out_btns, int8_t *out_wheel) {
    if (!rpt || !out_dx || !out_dy || !out_btns || !out_wheel) {
        return;
    }
    *out_dx = 0;
    *out_dy = 0;
    *out_btns = 0;
    *out_wheel = 0;
    if (len < 3u) {
        return;
    }
    *out_btns = rpt[0] & 0x07u;
    *out_dx = (int)(int8_t)rpt[1];
    *out_dy = (int)(int8_t)rpt[2];
    if (len >= 4u) {
        *out_wheel = (int8_t)rpt[3];
    }
}
