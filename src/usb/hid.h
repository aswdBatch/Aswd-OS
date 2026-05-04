#pragma once

#include <stdint.h>

void usb_hid_init(void);

void usb_hid_parse_boot_mouse(const uint8_t *rpt, unsigned len,
                              int *out_dx, int *out_dy,
                              uint8_t *out_btns, int8_t *out_wheel);

/** Boot-protocol HID keyboard report (8 bytes). Updates prev[6] from keys in report. */
void usb_hid_boot_keyboard_process(const uint8_t *rpt, uint8_t prev[6]);
