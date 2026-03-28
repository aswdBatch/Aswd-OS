#include "drivers/bga.h"

#include <stdint.h>

#include "cpu/ports.h"
#include "drivers/serial.h"
#include "lib/string.h"

/* BGA I/O ports */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

/* BGA register indices */
#define VBE_DISPI_INDEX_ID          0x00
#define VBE_DISPI_INDEX_XRES        0x01
#define VBE_DISPI_INDEX_YRES        0x02
#define VBE_DISPI_INDEX_BPP         0x03
#define VBE_DISPI_INDEX_ENABLE      0x04
#define VBE_DISPI_INDEX_BANK        0x05
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x06
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x07
#define VBE_DISPI_INDEX_X_OFFSET    0x08
#define VBE_DISPI_INDEX_Y_OFFSET    0x09
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0x0A

/* Enable flags */
#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED  0x01
#define VBE_DISPI_LFB_ENABLED 0x40

/* PCI config ports */
#define PCI_CONFIG_ADDR 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC

/* Bochs VGA PCI IDs */
#define BGA_PCI_VENDOR 0x1234
#define BGA_PCI_DEVICE 0x1111

static uint32_t g_bga_bar0;

/* --- helpers --- */

static void serial_write_hex32(uint32_t val) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--)
        buf[2 + (7 - i)] = hex[(val >> (i * 4)) & 0xF];
    buf[10] = '\0';
    serial_write(buf);
}

static void serial_write_dec32(uint32_t val) {
    char buf[16];
    u32_to_dec(val, buf, sizeof(buf));
    serial_write(buf);
}

static void bga_write(uint16_t reg, uint16_t val) {
    outw(VBE_DISPI_IOPORT_INDEX, reg);
    outw(VBE_DISPI_IOPORT_DATA, val);
}

static uint16_t bga_read(uint16_t reg) {
    outw(VBE_DISPI_IOPORT_INDEX, reg);
    return inw(VBE_DISPI_IOPORT_DATA);
}

/* Read PCI config dword at bus/dev/func/offset */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t addr = (1u << 31)
                  | ((uint32_t)bus  << 16)
                  | ((uint32_t)dev  << 11)
                  | ((uint32_t)func << 8)
                  | (off & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

/* Scan PCI for Bochs VGA and return BAR0 */
static int pci_find_bga(uint32_t *bar0_out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32((uint8_t)bus, dev, 0, 0x00);
            if ((id & 0xFFFF) == BGA_PCI_VENDOR &&
                ((id >> 16) & 0xFFFF) == BGA_PCI_DEVICE) {
                *bar0_out = pci_read32((uint8_t)bus, dev, 0, 0x10) & 0xFFFFFFF0;
                return 1;
            }
        }
    }
    return 0;
}

/* --- public API --- */

int bga_detect(void) {
    serial_write("BGA: probing...\n");

    uint16_t id = bga_read(VBE_DISPI_INDEX_ID);
    serial_write("BGA: ID=");
    serial_write_hex32(id);
    serial_write("\n");

    if (id < 0xB0C0 || id > 0xB0C5) {
        serial_write("BGA: not detected\n");
        return 0;
    }

    if (!pci_find_bga(&g_bga_bar0)) {
        serial_write("BGA: PCI device not found\n");
        return 0;
    }

    serial_write("BGA: detected, BAR0=");
    serial_write_hex32(g_bga_bar0);
    serial_write("\n");
    return 1;
}

int bga_set_mode(uint16_t width, uint16_t height, uint16_t bpp, bga_mode_info_t *out_mode) {
    bga_mode_info_t mode;

    if (!out_mode) {
        serial_write("BGA: missing mode out param\n");
        return 0;
    }

    /* Check VRAM is sufficient */
    uint32_t vram_64k = bga_read(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
    uint32_t vram_bytes = vram_64k * 65536u;
    uint32_t needed = (uint32_t)width * height * (bpp / 8);

    serial_write("BGA: VRAM=");
    serial_write_hex32(vram_bytes);
    serial_write(" need=");
    serial_write_hex32(needed);
    serial_write("\n");

    if (needed > vram_bytes) {
        serial_write("BGA: insufficient VRAM\n");
        return 0;
    }

    /* Disable display before changing mode */
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    /* Set resolution and depth */
    bga_write(VBE_DISPI_INDEX_XRES, width);
    bga_write(VBE_DISPI_INDEX_YRES, height);
    bga_write(VBE_DISPI_INDEX_BPP, bpp);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);

    /* Enable with LFB */
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    /* Verify enable took effect */
    uint16_t en = bga_read(VBE_DISPI_INDEX_ENABLE);
    if (!(en & VBE_DISPI_ENABLED)) {
        serial_write("BGA: enable failed\n");
        return 0;
    }

    mode.lfb_addr = g_bga_bar0;
    mode.xres = bga_read(VBE_DISPI_INDEX_XRES);
    mode.yres = bga_read(VBE_DISPI_INDEX_YRES);
    mode.virt_width = bga_read(VBE_DISPI_INDEX_VIRT_WIDTH);
    mode.virt_height = bga_read(VBE_DISPI_INDEX_VIRT_HEIGHT);
    mode.bpp = (uint8_t)bga_read(VBE_DISPI_INDEX_BPP);
    if (mode.virt_width == 0) mode.virt_width = mode.xres;
    if (mode.virt_height == 0) mode.virt_height = mode.yres;
    mode.pitch_bytes = (uint32_t)mode.virt_width * (uint32_t)(mode.bpp / 8u);

    if ((uint32_t)mode.virt_width * (uint32_t)mode.virt_height * (uint32_t)(mode.bpp / 8u) > vram_bytes) {
        serial_write("BGA: rounded mode exceeds VRAM\n");
        return 0;
    }

    *out_mode = mode;

    serial_write("BGA: req ");
    serial_write_dec32(width);
    serial_write("x");
    serial_write_dec32(height);
    serial_write("x");
    serial_write_dec32(bpp);
    serial_write(" -> act ");
    serial_write_dec32(mode.xres);
    serial_write("x");
    serial_write_dec32(mode.yres);
    serial_write(" virt ");
    serial_write_dec32(mode.virt_width);
    serial_write("x");
    serial_write_dec32(mode.virt_height);
    serial_write(" pitch=");
    serial_write_dec32(mode.pitch_bytes);
    serial_write(" LFB=");
    serial_write_hex32(mode.lfb_addr);
    serial_write("\n");
    return 1;
}
