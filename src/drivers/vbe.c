#include "drivers/vbe.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/serial.h"
#include "lib/string.h"

enum {
    TRAMPOLINE_ADDR  = 0x8000u,
    TRAMPOLINE_PARAM = 0x1400u,
    BOUNCE_BUF       = 0x10000u,
};

typedef void (*tramp_fn_t)(void);

static void vbe_serial_hex32(uint32_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++)
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    buf[10] = '\0';
    serial_write(buf);
}

static int vbe_trampoline_call(uint8_t op, uint16_t mode_val) {
    tramp_fn_t tramp = (tramp_fn_t)TRAMPOLINE_ADDR;

    /* Clear param block */
    mem_set((void *)TRAMPOLINE_PARAM, 0, 64);

    /* op goes at offset +1 */
    *(volatile uint8_t  *)(TRAMPOLINE_PARAM + 1) = op;
    /* mode/param at offset +2 (reuse count field) */
    *(volatile uint16_t *)(TRAMPOLINE_PARAM + 2) = mode_val;
    /* pre-set result to sentinel */
    *(volatile uint32_t *)(TRAMPOLINE_PARAM + 12) = 0xFFFFFFFFu;

    cpu_cli();
    tramp();
    cpu_sti();

    /* VBE return code stored at offset +12 by trampoline */
    uint32_t rc;
    mem_copy(&rc, (const void *)(TRAMPOLINE_PARAM + 12), sizeof(rc));
    return (int)rc;
}

int vbe_get_info(vbe_info_t *out) {
    /* Write "VBE2" signature into bounce buffer so VBE returns 3.0 info */
    char *sig = (char *)BOUNCE_BUF;
    sig[0] = 'V'; sig[1] = 'B'; sig[2] = 'E'; sig[3] = '2';

    int rc = vbe_trampoline_call(3, 0);
    if (rc != 0x004F) {
        serial_write("VBE: get_info rc=");
        vbe_serial_hex32((uint32_t)rc);
        serial_write("\n");
        return 0;
    }

    mem_copy(out, (const void *)BOUNCE_BUF, sizeof(vbe_info_t));
    return 1;
}

int vbe_get_mode_info(uint16_t mode, vbe_mode_info_t *out) {
    int rc = vbe_trampoline_call(4, mode);
    if (rc != 0x004F) return 0;

    mem_copy(out, (const void *)BOUNCE_BUF, sizeof(vbe_mode_info_t));
    return 1;
}

int vbe_set_mode(uint16_t mode) {
    /* Set bit 14 for linear framebuffer */
    int rc = vbe_trampoline_call(5, mode | 0x4000u);
    serial_write("VBE: set_mode ");
    vbe_serial_hex32(mode);
    serial_write(" rc=");
    vbe_serial_hex32((uint32_t)rc);
    serial_write("\n");
    return (rc == 0x004F) ? 1 : 0;
}

int vbe_find_mode(uint16_t w, uint16_t h, uint8_t bpp,
                  uint16_t *out_mode, vbe_mode_info_t *out_info) {
    vbe_info_t info;
    if (!vbe_get_info(&info)) {
        serial_write("VBE: get_info failed\n");
        return 0;
    }

    /* Validate VBE signature — should be "VESA" after trampoline call */
    if (info.signature[0] != 'V' || info.signature[1] != 'E' ||
        info.signature[2] != 'S' || info.signature[3] != 'A') {
        serial_write("VBE: bad sig\n");
        return 0;
    }

    serial_write("VBE: v=");
    vbe_serial_hex32(info.version);
    serial_write(" modeptr=");
    vbe_serial_hex32(info.mode_list_ptr);
    serial_write("\n");

    /* Convert far pointer: seg:off → physical = (seg << 4) + off */
    uint16_t off = (uint16_t)(info.mode_list_ptr & 0xFFFFu);
    uint16_t seg = (uint16_t)((info.mode_list_ptr >> 16) & 0xFFFFu);
    uint32_t phys = ((uint32_t)seg << 4) + off;

    /* Sanity: mode list must be in real-mode accessible memory */
    if (phys == 0 || phys >= 0x100000u) {
        serial_write("VBE: modeptr OOB=");
        vbe_serial_hex32(phys);
        serial_write("\n");
        return 0;
    }

    uint16_t *modes = (uint16_t *)phys;

    for (int i = 0; i < 256; i++) {
        uint16_t m = modes[i];
        if (m == 0xFFFFu) break;

        vbe_mode_info_t mi;
        if (!vbe_get_mode_info(m, &mi)) continue;

        /* LFB support: attribute bit 7 */
        if (!(mi.attributes & (1u << 7))) continue;

        /* Direct-color model (6), single plane */
        if (mi.memory_model != 6 || mi.planes != 1) continue;

        if (mi.width != w || mi.height != h || mi.bpp != bpp) continue;

        if (mi.framebuffer == 0) {
            serial_write("VBE: mode ");
            vbe_serial_hex32(m);
            serial_write(" fb=0, skipping\n");
            continue;
        }

        serial_write("VBE: mode ");
        vbe_serial_hex32(m);
        serial_write(" fb=");
        vbe_serial_hex32(mi.framebuffer);
        serial_write(" pitch=");
        vbe_serial_hex32(mi.pitch);
        serial_write("\n");

        *out_mode = m;
        mem_copy(out_info, &mi, sizeof(mi));
        return 1;
    }

    serial_write("VBE: no suitable mode\n");
    return 0;
}
