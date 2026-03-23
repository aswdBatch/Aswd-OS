#pragma once
#include <stdint.h>

int bga_detect(void);
int bga_set_mode(uint16_t width, uint16_t height, uint16_t bpp,
                 uint32_t *out_lfb, uint32_t *out_pitch);
