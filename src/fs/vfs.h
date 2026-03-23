#pragma once

#include <stdint.h>

#include "drivers/fat32.h"

int         vfs_init(void);
int         vfs_available(void);
const char *vfs_cwd_path(void);
int         vfs_cwd_is_writable(void);
int         vfs_enter_root_workspace(void);
int         vfs_ls(fat32_entry_t *entries, int max);
int         vfs_cd(const char *path);
int         vfs_cat(const char *name, uint8_t *buf, int max);
int         vfs_write(const char *name, const uint8_t *data, uint32_t size);
int         vfs_rm(const char *name);
int         vfs_mkdir(const char *name);
int         vfs_rmdir(const char *name);
