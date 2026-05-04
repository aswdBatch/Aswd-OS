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

static inline uint32_t bootvid_read_u32(uintptr_t addr) {
    unsigned int value;
    __asm__ volatile("movl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return (uint32_t)value;
}

static inline uint16_t bootvid_read_u16(uintptr_t addr) {
    unsigned int value;
    __asm__ volatile("movzwl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return (uint16_t)value;
}

static inline uint8_t bootvid_read_u8(uintptr_t addr) {
    unsigned int value;
    __asm__ volatile("movzbl (%1), %0" : "=r"(value) : "r"(addr) : "memory");
    return (uint8_t)value;
}

static inline uint32_t bootvid_fb_addr(void) {
    return bootvid_read_u32((uintptr_t)0x0510u);
}

static inline uint32_t bootvid_pitch(void) {
    return bootvid_read_u32((uintptr_t)0x0514u);
}

static inline uint16_t bootvid_width(void) {
    return bootvid_read_u16((uintptr_t)0x0518u);
}

static inline uint16_t bootvid_height(void) {
    return bootvid_read_u16((uintptr_t)0x051Au);
}

static inline uint8_t bootvid_bpp(void) {
    return bootvid_read_u8((uintptr_t)0x051Cu);
}

static inline int bootvid_available(void) {
    return bootvid_read_u8((uintptr_t)0x051Du) == 0x01u;
}
