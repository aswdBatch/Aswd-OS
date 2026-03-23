#include "drivers/disk.h"

#include <stdint.h>

#include "console/console.h"
#include "drivers/ata.h"
#include "lib/string.h"

/*
 * USB-capable BIOS disk backend.
 *
 * The kernel copies a tiny real-mode trampoline into low memory and uses it
 * to call INT 13h for reads and writes. That keeps the storage path working
 * on actual USB boots where a pure ATA/IDE backend cannot see the device.
 */

enum {
    DISK_TRAMPOLINE_ADDR = 0x8000u,
    DISK_TRAMPOLINE_GDT  = 0x1460u,
    DISK_TRAMPOLINE_BUF  = 0x10000u,
    DISK_TRAMPOLINE_PARAM = 0x1400u,
};

enum {
    TRAMP_OP_READ = 0,
    TRAMP_OP_WRITE = 1,
    TRAMP_OP_KEYBOARD = 2,
    TRAMP_OP_VBE_INFO = 3,
    TRAMP_OP_VBE_MODE_INFO = 4,
    TRAMP_OP_VBE_SET_MODE = 5,
    TRAMP_OP_EDD_PROBE = 6,
    TRAMP_OP_DISK_RESET = 7,
};

enum {
    DISK_MAX_RETRIES = 3,
};

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} gdtr_t;

extern uint8_t _binary_obj_usbboot_trampoline_bin_start[];
extern uint8_t _binary_obj_usbboot_trampoline_bin_end[];

typedef void (*disk_trampoline_fn_t)(void);

static int g_available = 0;
static disk_backend_t g_backend = DISK_BACKEND_NONE;
static uint8_t g_drive = 0;
static uint32_t g_part_lba = 0;
static disk_op_t g_last_op = DISK_OP_NONE;
static uint8_t g_last_bios_status = 0;
static uint8_t g_last_retry_count = 0;
static uint32_t g_last_error_lba = 0;

static uint32_t trampoline_size(void) {
    return (uint32_t)(_binary_obj_usbboot_trampoline_bin_end -
                      _binary_obj_usbboot_trampoline_bin_start);
}

static void trampoline_copy(void) {
    mem_copy((void*)DISK_TRAMPOLINE_ADDR,
             _binary_obj_usbboot_trampoline_bin_start,
             trampoline_size());
}

static void trampoline_save_gdtr(void) {
    gdtr_t gdtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr));
    mem_copy((void*)DISK_TRAMPOLINE_GDT, &gdtr, sizeof(gdtr));
}

static void disk_set_last_result(disk_op_t op, uint8_t status, uint8_t retries,
                                 uint32_t error_lba) {
    g_last_op = op;
    g_last_bios_status = status;
    g_last_retry_count = retries;
    g_last_error_lba = error_lba;
}

static void trampoline_prepare(uint8_t op, uint32_t lba, uint16_t count) {
    mem_set((void*)DISK_TRAMPOLINE_PARAM, 0, 64);

    *(volatile uint8_t*)(DISK_TRAMPOLINE_PARAM + 0) = g_drive;
    *(volatile uint8_t*)(DISK_TRAMPOLINE_PARAM + 1) = op;
    *(volatile uint16_t*)(DISK_TRAMPOLINE_PARAM + 2) = count;
    *(volatile uint32_t*)(DISK_TRAMPOLINE_PARAM + 4) = lba;
    *(volatile uint32_t*)(DISK_TRAMPOLINE_PARAM + 8) = 0;
    *(volatile uint32_t*)(DISK_TRAMPOLINE_PARAM + 12) = 0xFFFFFFFFu;
}

static uint32_t trampoline_result(void) {
    uint32_t result = 0xFFFFFFFFu;
    mem_copy(&result, (const void*)(DISK_TRAMPOLINE_PARAM + 12), sizeof(result));
    return result;
}

static uint32_t disk_trampoline_call(uint8_t op, uint32_t lba, void *buf) {
    disk_trampoline_fn_t tramp = (disk_trampoline_fn_t)DISK_TRAMPOLINE_ADDR;
    uint32_t status;

    trampoline_prepare(op, lba, 1);

    if (op == TRAMP_OP_WRITE && buf) {
        mem_copy((void*)DISK_TRAMPOLINE_BUF, buf, 512);
    }

    tramp();
    status = trampoline_result();

    if (status == 0 && op == TRAMP_OP_READ && buf) {
        mem_copy(buf, (const void*)DISK_TRAMPOLINE_BUF, 512);
    }

    return status;
}

/* Returns 1 when BIOS USB handoff was initialized successfully, 0 when there
   was no BIOS boot-drive handoff, and -1 when the handoff existed but EDD
   services were unavailable. */
static int disk_init_bios_boot_drive(void) {
    uint8_t drive = 0;
    uint32_t part_lba = 0;
    uint32_t probe_status;

    mem_copy(&drive, (const void*)0x0500, sizeof(drive));
    mem_copy(&part_lba, (const void*)0x0504, sizeof(part_lba));

    if (drive < 0x80u || part_lba == 0) {
        return 0;
    }

    g_drive = drive;
    g_part_lba = part_lba;

    trampoline_save_gdtr();
    trampoline_copy();

    probe_status = disk_trampoline_call(TRAMP_OP_EDD_PROBE, 0, 0);
    if (probe_status != 0) {
        disk_set_last_result(DISK_OP_NONE, (uint8_t)probe_status, 0, 0);
        g_backend = DISK_BACKEND_NONE;
        g_available = 0;
        return -1;
    }

    g_backend = DISK_BACKEND_BIOS_TRAMPOLINE;
    g_available = 1;
    disk_set_last_result(DISK_OP_NONE, 0, 0, 0);
    return 1;
}

static int disk_bios_reset(void) {
    return disk_trampoline_call(TRAMP_OP_DISK_RESET, 0, 0) == 0 ? 0 : -1;
}

static int disk_bios_transfer(disk_op_t op, uint32_t lba, void *buf) {
    uint8_t retries = 0;

    for (;;) {
        uint32_t status = disk_trampoline_call(
            op == DISK_OP_READ ? TRAMP_OP_READ : TRAMP_OP_WRITE, lba, buf);
        if (status == 0) {
            disk_set_last_result(op, 0, retries, 0);
            return 0;
        }

        if (retries >= DISK_MAX_RETRIES) {
            disk_set_last_result(op, (uint8_t)status, retries, lba);
            return -1;
        }

        retries++;
        (void)disk_bios_reset();
    }
}

int disk_init(void) {
    int bios_result;

    g_available = 0;
    g_backend = DISK_BACKEND_NONE;
    g_drive = 0;
    g_part_lba = 0;
    disk_set_last_result(DISK_OP_NONE, 0, 0, 0);

    /* On BIOS USB boots, trust the exact drive that stage2 loaded from before
       probing generic ATA devices that may point at an unrelated internal disk. */
    bios_result = disk_init_bios_boot_drive();
    if (bios_result > 0) {
        return 1;
    }
    /* bios_result < 0: stage2 handoff present but EDD unavailable.
       g_part_lba was set from the handoff – fall through to ATA and
       keep that LBA rather than giving up entirely. */

    /* GRUB/QEMU path: no stage2 handoff (or EDD failed), use ATA/IDE. */
    if (ata_init()) {
        g_available = 1;
        g_backend = DISK_BACKEND_ATA;
        /* Prefer the stage2 handoff LBA; only use ATA scan when absent. */
        if (g_part_lba == 0) {
            g_part_lba = ata_partition_start();
        }
        disk_set_last_result(DISK_OP_NONE, 0, 0, 0);
        return 1;
    }

    return 0;
}

int disk_available(void) {
    return g_available;
}

uint32_t disk_partition_start(void) {
    return g_part_lba;
}

int disk_read_sectors(uint32_t lba, uint16_t count, void *buf) {
    if (!g_available) return -1;

    if (g_backend == DISK_BACKEND_ATA) {
        int rc = ata_read_sectors(lba, count, buf);
        disk_set_last_result(DISK_OP_READ, 0, 0, rc == 0 ? 0 : lba);
        return rc;
    }

    if (g_backend != DISK_BACKEND_BIOS_TRAMPOLINE) {
        return -1;
    }

    uint8_t *dst = (uint8_t*)buf;
    uint8_t total_retries = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (disk_bios_transfer(DISK_OP_READ, lba + i,
                               dst + (uint32_t)i * 512u) != 0) {
            console_writeln("[disk] read error");
            return -1;
        }
        total_retries = (uint8_t)(total_retries + g_last_retry_count);
    }
    disk_set_last_result(DISK_OP_READ, 0, total_retries, 0);
    return 0;
}

int disk_write_sectors(uint32_t lba, uint16_t count, const void *buf) {
    if (!g_available) return -1;

    if (g_backend == DISK_BACKEND_ATA) {
        int rc = ata_write_sectors(lba, count, buf);
        disk_set_last_result(DISK_OP_WRITE, 0, 0, rc == 0 ? 0 : lba);
        return rc;
    }

    if (g_backend != DISK_BACKEND_BIOS_TRAMPOLINE) {
        return -1;
    }

    const uint8_t *src = (const uint8_t*)buf;
    uint8_t total_retries = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (disk_bios_transfer(DISK_OP_WRITE, lba + i,
                               (void*)(src + (uint32_t)i * 512u)) != 0) {
            console_writeln("[disk] write error");
            return -1;
        }
        total_retries = (uint8_t)(total_retries + g_last_retry_count);
    }
    disk_set_last_result(DISK_OP_WRITE, 0, total_retries, 0);
    return 0;
}

disk_backend_t disk_backend(void) {
    return g_backend;
}

uint8_t disk_bios_boot_drive(void) {
    return g_drive;
}

disk_op_t disk_last_op(void) {
    return g_last_op;
}

uint8_t disk_last_bios_status(void) {
    return g_last_bios_status;
}

uint8_t disk_last_retry_count(void) {
    return g_last_retry_count;
}

uint32_t disk_last_error_lba(void) {
    return g_last_error_lba;
}
