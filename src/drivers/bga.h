#pragma once
#include <stdint.h>

typedef struct {
    uint32_t lfb_addr;
    uint32_t pitch_bytes;
    uint16_t xres;
    uint16_t yres;
    uint16_t virt_width;
    uint16_t virt_height;
    uint8_t  bpp;
} bga_mode_info_t;

int bga_detect(void);
int bga_set_mode(uint16_t width, uint16_t height, uint16_t bpp, bga_mode_info_t *out_mode);
