#include "drivers/ata.h"

#include <stdint.h>

#include "cpu/ports.h"

#define ATA_DATA  0x1F0
#define ATA_ERR   0x1F1
#define ATA_CNT   0x1F2
#define ATA_LBA0  0x1F3
#define ATA_LBA1  0x1F4
#define ATA_LBA2  0x1F5
#define ATA_SEL   0x1F6
#define ATA_CMD   0x1F7

#define ATA_BSY   0x80
#define ATA_DRQ   0x08
#define ATA_ERR_B 0x01

static int      g_available  = 0;
static uint32_t g_part_start = 0;

static int wait_not_busy(void) {
    int i;
    for (i = 0; i < 100000; i++)
        if (!(inb(ATA_CMD) & ATA_BSY)) return 1;
    return 0;
}

static int wait_drq(void) {
    int i;
    for (i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_CMD);
        if (s & ATA_ERR_B) return 0;
        if (s & ATA_DRQ)   return 1;
    }
    return 0;
}

static int ata_read_one(uint32_t lba, void *buf) {
    uint16_t *d;
    int i;
    if (!wait_not_busy()) return -1;
    outb(ATA_SEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_CNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_CMD, 0x20);
    if (!wait_drq()) return -1;
    d = (uint16_t *)buf;
    for (i = 0; i < 256; i++) d[i] = inw(ATA_DATA);
    return 0;
}

static int ata_write_one(uint32_t lba, const void *buf) {
    const uint16_t *s;
    int i;
    if (!wait_not_busy()) return -1;
    outb(ATA_SEL, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_CNT, 1);
    outb(ATA_LBA0, (uint8_t)lba);
    outb(ATA_LBA1, (uint8_t)(lba >> 8));
    outb(ATA_LBA2, (uint8_t)(lba >> 16));
    outb(ATA_CMD, 0x30);
    if (!wait_drq()) return -1;
    s = (const uint16_t *)buf;
    for (i = 0; i < 256; i++) outw(ATA_DATA, s[i]);
    outb(ATA_CMD, 0xE7); /* FLUSH CACHE */
    wait_not_busy();
    return 0;
}

int ata_init(void) {
    static uint8_t mbr[512];
    uint8_t status;
    int i;

    /* Soft-reset the primary bus before probing */
    outb(0x3F6u, 0x04u); /* SRST bit */
    for (i = 0; i < 10000; i++) (void)inb(0x3F6u);
    outb(0x3F6u, 0x00u); /* clear SRST */
    for (i = 0; i < 10000; i++) (void)inb(0x3F6u);

    /* Quick presence check – only 0xFF means floating (no device) */
    outb(ATA_SEL, 0xE0);
    status = inb(ATA_CMD);
    if (status == 0xFF) return 0;

    if (ata_read_one(0, mbr) != 0) return 0;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;

    for (i = 0; i < 4; i++) {
        uint8_t *e = mbr + 446 + i * 16;
        uint8_t type = e[4];
        if (type == 0x0B || type == 0x0C || type == 0x06 || type == 0x0E) {
            uint32_t lba_start = (uint32_t)e[8]
                               | ((uint32_t)e[9]  << 8)
                               | ((uint32_t)e[10] << 16)
                               | ((uint32_t)e[11] << 24);
            if (lba_start > 0) {
                g_part_start = lba_start;
                g_available  = 1;
                return 1;
            }
        }
    }
    return 0;
}

int ata_available(void) {
    return g_available;
}

uint32_t ata_partition_start(void) {
    return g_part_start;
}

int ata_read_sectors(uint32_t lba, uint16_t count, void *buf) {
    uint8_t *dst = (uint8_t *)buf;
    uint16_t i;
    for (i = 0; i < count; i++) {
        if (ata_read_one(lba + i, dst + (uint32_t)i * 512u) != 0) return -1;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint16_t count, const void *buf) {
    const uint8_t *src = (const uint8_t *)buf;
    uint16_t i;
    for (i = 0; i < count; i++) {
        if (ata_write_one(lba + i, src + (uint32_t)i * 512u) != 0) return -1;
    }
    return 0;
}
