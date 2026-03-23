#pragma once

#include <stdint.h>

typedef struct {
    char     name[13];   /* "README.TXT\0" */
    uint32_t cluster;
    uint32_t size;
    uint8_t  attr;
    uint8_t  is_dir;
} fat32_entry_t;

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t partition_start_lba;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
} fat32_info_t;

int      fat32_init(void);
int      fat32_list_dir(uint32_t dir_cluster, fat32_entry_t *entries, int max);
int      fat32_find_entry(uint32_t dir_cluster, const char *name, fat32_entry_t *out);
int      fat32_read_file(const fat32_entry_t *entry, uint8_t *buf, int max);
int      fat32_write_file(uint32_t dir_cluster, const char *name, const uint8_t *buf, uint32_t size);
int      fat32_delete_entry(uint32_t dir_cluster, const char *name);
int      fat32_mkdir(uint32_t dir_cluster, const char *name);
int      fat32_rmdir(uint32_t dir_cluster, const char *name);
uint32_t fat32_get_root_cluster(void);
int      fat32_get_info(fat32_info_t *out);
