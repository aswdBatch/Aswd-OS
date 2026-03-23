#pragma once
#include <stdint.h>

/*
 * Stage2 bootloader saves VBE framebuffer info at fixed addresses in low
 * memory before entering protected mode.  The kernel reads these to pick
 * up the framebuffer without needing a real-mode trampoline.
 *
 *   0x0510  uint32_t  framebuffer physical address
 *   0x0514  uint32_t  pitch (bytes per scan line)
 *   0x0518  uint16_t  width
 *   0x051A  uint16_t  height
 *   0x051C  uint8_t   bpp
 *   0x051D  uint8_t   valid flag (0x01 = info present)
 */

#define BOOTVID_FB_ADDR   (*(volatile uint32_t *)0x0510)
#define BOOTVID_PITCH     (*(volatile uint32_t *)0x0514)
#define BOOTVID_WIDTH     (*(volatile uint16_t *)0x0518)
#define BOOTVID_HEIGHT    (*(volatile uint16_t *)0x051A)
#define BOOTVID_BPP       (*(volatile uint8_t  *)0x051C)
#define BOOTVID_VALID     (*(volatile uint8_t  *)0x051D)

static inline int bootvid_available(void) {
    return BOOTVID_VALID == 0x01;
}
