#pragma once
#include <stdint.h>

int      ata_init(void);
int      ata_available(void);
uint32_t ata_partition_start(void);
int      ata_read_sectors(uint32_t lba, uint16_t count, void *buf);
int      ata_write_sectors(uint32_t lba, uint16_t count, const void *buf);
