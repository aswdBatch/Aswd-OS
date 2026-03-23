#pragma once

#include <stdint.h>

void multiboot_init(uint32_t magic, uint32_t addr);
uint32_t multiboot_mem_lower_kb(void);
uint32_t multiboot_mem_upper_kb(void);
int multiboot_has_mem_info(void);

int      multiboot_has_framebuffer(void);
uint32_t multiboot_fb_addr(void);
uint32_t multiboot_fb_pitch(void);
uint32_t multiboot_fb_width(void);
uint32_t multiboot_fb_height(void);
uint8_t  multiboot_fb_bpp(void);

