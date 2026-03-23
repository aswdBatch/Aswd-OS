#pragma once

#include <stdint.h>

typedef enum {
    DISK_BACKEND_NONE = 0,
    DISK_BACKEND_ATA = 1,
    DISK_BACKEND_BIOS_TRAMPOLINE = 2,
} disk_backend_t;

typedef enum {
    DISK_OP_NONE = 0,
    DISK_OP_READ = 1,
    DISK_OP_WRITE = 2,
} disk_op_t;

int      disk_init(void);
int      disk_available(void);
uint32_t disk_partition_start(void);
int      disk_read_sectors(uint32_t lba, uint16_t count, void *buf);
int      disk_write_sectors(uint32_t lba, uint16_t count, const void *buf);

disk_backend_t disk_backend(void);
uint8_t        disk_bios_boot_drive(void);
disk_op_t      disk_last_op(void);
uint8_t        disk_last_bios_status(void);
uint8_t        disk_last_retry_count(void);
uint32_t       disk_last_error_lba(void);
