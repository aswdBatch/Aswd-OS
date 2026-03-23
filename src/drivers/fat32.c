#include "drivers/fat32.h"

#include <stdint.h>

#include "boot/bootui.h"
#include "cpu/bugcheck.h"
#include "console/console.h"
#include "drivers/disk.h"
#include "lib/string.h"

/* ── State ──────────────────────────────────────────────────────────── */
static uint32_t g_part_start;
static uint32_t g_fat_start;
static uint32_t g_data_start;
static uint16_t g_bytes_per_sec;
static uint16_t g_reserved_sectors;
static uint8_t  g_sec_per_clus;
static uint8_t  g_fat_count;
static uint32_t g_fat_size;           /* sectors per FAT */
static uint32_t g_root_cluster_fat;   /* FAT32 root dir cluster (=2) */
static uint32_t g_root_cluster_user;  /* cluster of root/ subfolder */
static uint32_t g_cluster_limit;      /* first invalid cluster number */

static uint8_t  g_sec_buf[512];
static uint8_t  g_fat_cache[512];
static uint32_t g_fat_cache_lba = (uint32_t)-1;
static uint8_t  g_fat_dirty = 0;

/* ── Little-endian helpers ──────────────────────────────────────────── */
static uint16_t u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void w32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void w16le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

/* ── Cluster ↔ LBA ──────────────────────────────────────────────────── */
static uint32_t cluster_to_lba(uint32_t c) {
    return g_data_start + (c - 2) * (uint32_t)g_sec_per_clus;
}

static uint32_t fat_read_entry(uint32_t cluster);
static int fat_write_entry(uint32_t cluster, uint32_t value);
static int write_dir_entry(uint32_t dir_cluster, const uint8_t entry[32]);

static int cluster_valid(uint32_t c) {
    return c >= 2u && c < g_cluster_limit;
}

static int fat_chain_next(uint32_t cluster, uint32_t *next_out) {
    uint32_t next;

    if (!cluster_valid(cluster)) {
        return -1;
    }

    next = fat_read_entry(cluster);
    if (next == 0xFFFFFFFFu) {
        return -1;
    }
    if (next >= 0x0FFFFFF8u) {
        if (next_out) {
            *next_out = 0;
        }
        return 0;
    }

    if (!cluster_valid(next)) {
        return -1;
    }

    if (next_out) {
        *next_out = next;
    }
    return 1;
}

/* ── FAT cache ──────────────────────────────────────────────────────── */
static int fat_flush(void) {
    if (!g_fat_dirty) return 0;
    if (disk_write_sectors(g_fat_cache_lba, 1, g_fat_cache) != 0) return -1;
    /* write FAT2 as well */
    if (disk_write_sectors(g_fat_cache_lba + g_fat_size, 1, g_fat_cache) != 0) return -1;
    g_fat_dirty = 0;
    return 0;
}

static int fat_load(uint32_t lba) {
    if (g_fat_cache_lba == lba) return 0;
    if (fat_flush() != 0) return -1;
    if (disk_read_sectors(lba, 1, g_fat_cache) != 0) return -1;
    g_fat_cache_lba = lba;
    return 0;
}

static uint32_t fat_read_entry(uint32_t cluster) {
    if (!cluster_valid(cluster)) {
        return 0xFFFFFFFFu;
    }
    uint32_t byte_off = cluster * 4;
    uint32_t lba = g_fat_start + byte_off / 512;
    uint32_t off = byte_off % 512;
    if (fat_load(lba) != 0) return 0xFFFFFFFFu;
    return u32le(g_fat_cache + off) & 0x0FFFFFFFu;
}

static int fat_write_entry(uint32_t cluster, uint32_t value) {
    if (!cluster_valid(cluster)) {
        return -1;
    }
    uint32_t byte_off = cluster * 4;
    uint32_t lba = g_fat_start + byte_off / 512;
    uint32_t off = byte_off % 512;
    if (fat_load(lba) != 0) return -1;
    uint32_t cur = u32le(g_fat_cache + off);
    cur = (cur & 0xF0000000u) | (value & 0x0FFFFFFFu);
    w32le(g_fat_cache + off, cur);
    g_fat_dirty = 1;
    return 0;
}

/* Find a free cluster starting from cluster 3 */
static uint32_t fat_alloc_cluster(void) {
    for (uint32_t c = 3; c < g_cluster_limit; c++) {
        if (fat_read_entry(c) == 0) {
            if (fat_write_entry(c, 0x0FFFFFFF) == 0) {
                return c;
            }
            return 0;
        }
    }
    return 0; /* out of space */
}

static int free_cluster_chain(uint32_t first_cluster) {
    uint32_t cluster = first_cluster;
    uint32_t hops = 0;

    while (cluster_valid(cluster)) {
        uint32_t next = 0;
        int next_r;

        if (hops++ > g_cluster_limit) {
            return -1;
        }

        next_r = fat_chain_next(cluster, &next);
        fat_write_entry(cluster, 0);
        if (next_r <= 0) {
            break;
        }
        cluster = next;
    }

    return fat_flush();
}

static int is_name_char(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_' || c == '-') return 1;
    return 0;
}

static int validate_83_name(const char *name) {
    int base_len = 0;
    int ext_len = 0;
    int seen_dot = 0;

    if (!name || !name[0]) {
        return 0;
    }

    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') {
            if (seen_dot || base_len == 0) {
                return 0;
            }
            seen_dot = 1;
            continue;
        }

        if (!is_name_char(name[i])) {
            return 0;
        }

        if (!seen_dot) {
            if (++base_len > 8) return 0;
        } else {
            if (++ext_len > 3) return 0;
        }
    }

    if (base_len == 0) {
        return 0;
    }
    if (seen_dot && ext_len == 0) {
        return 0;
    }
    return 1;
}

static int entry_is_dot(const uint8_t *ent) {
    return ent[0] == '.' && ent[1] == ' ';
}

static int entry_is_dotdot(const uint8_t *ent) {
    return ent[0] == '.' && ent[1] == '.';
}

/* ── 8.3 name helpers ───────────────────────────────────────────────── */

/* Convert a null-terminated name to raw 11-byte 8.3 form (space-padded) */
static void to_83(const char *name, uint8_t out[11]) {
    mem_set(out, ' ', 11);
    int i = 0;
    int o = 0;
    /* base */
    while (name[i] && name[i] != '.' && o < 8) {
        char c = name[i++];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[o++] = (uint8_t)c;
    }
    if (name[i] == '.') {
        i++;
        int e = 0;
        while (name[i] && e < 3) {
            char c = name[i++];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[8 + e++] = (uint8_t)c;
        }
    }
}

/* Parse raw 11-byte 8.3 name to "BASE.EXT\0" or "BASE\0" */
static void parse_83(const uint8_t raw[11], char out[13]) {
    int o = 0;
    for (int i = 0; i < 8 && raw[i] != ' '; i++) {
        out[o++] = (char)raw[i];
    }
    int has_ext = 0;
    for (int i = 8; i < 11; i++) {
        if (raw[i] != ' ') { has_ext = 1; break; }
    }
    if (has_ext) {
        out[o++] = '.';
        for (int i = 8; i < 11 && raw[i] != ' '; i++) {
            out[o++] = (char)raw[i];
        }
    }
    out[o] = '\0';
}

/* Compare name against raw 11-byte entry (case-insensitive) */
static int match_83(const uint8_t raw[11], const char *name) {
    uint8_t cmp[11];
    if (!validate_83_name(name)) return 0;
    to_83(name, cmp);
    for (int i = 0; i < 11; i++) {
        if (raw[i] != cmp[i]) return 0;
    }
    return 1;
}

/* ── Directory helpers ──────────────────────────────────────────────── */

typedef int (*dir_cb_t)(uint8_t *entry, void *ctx);

/* Walk directory cluster chain, calling cb for each 32-byte entry.
   cb returns: 0 = continue, 1 = found/done, -1 = error */
static int walk_dir(uint32_t dir_cluster, dir_cb_t cb, void *ctx) {
    uint32_t clus = dir_cluster;
    uint32_t hops = 0;
    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;
        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (int e = 0; e < 512 / 32; e++) {
                uint8_t *ent = g_sec_buf + e * 32;
                if (ent[0] == 0x00) return 0; /* end */
                int r = cb(ent, ctx);
                if (r != 0) return r;
            }
        }
        uint32_t next = 0;
        int next_r = fat_chain_next(clus, &next);
        if (next_r < 0) return -1;
        if (next_r == 0) return 0;
        clus = next;
    }
    return -1;
}

/* ── List dir ───────────────────────────────────────────────────────── */
typedef struct { fat32_entry_t *entries; int max; int count; } list_ctx_t;

static int list_cb(uint8_t *ent, void *ctx) {
    list_ctx_t *lc = (list_ctx_t*)ctx;
    if (ent[0] == 0xE5) return 0;                  /* deleted */
    uint8_t attr = ent[11];
    if (attr == 0x0F) return 0;                    /* LFN */
    if (attr == 0x08) return 0;                    /* volume label */
    if (lc->count >= lc->max) return 0;
    fat32_entry_t *fe = &lc->entries[lc->count];
    parse_83(ent, fe->name);
    fe->cluster = ((uint32_t)u16le(ent + 20) << 16) | u16le(ent + 26);
    fe->size    = u32le(ent + 28);
    fe->attr    = attr;
    fe->is_dir  = (attr & 0x10) ? 1 : 0;
    /* skip . and .. */
    if (fe->name[0] == '.') return 0;
    lc->count++;
    return 0;
}

int fat32_list_dir(uint32_t dir_cluster, fat32_entry_t *entries, int max) {
    list_ctx_t lc = { entries, max, 0 };
    if (walk_dir(dir_cluster, list_cb, &lc) < 0) return -1;
    return lc.count;
}

/* ── Find entry ─────────────────────────────────────────────────────── */
typedef struct { const char *name; fat32_entry_t *out; int found; } find_ctx_t;

static int find_cb(uint8_t *ent, void *ctx) {
    find_ctx_t *fc = (find_ctx_t*)ctx;
    if (ent[0] == 0xE5) return 0;
    uint8_t attr = ent[11];
    if (attr == 0x0F || attr == 0x08) return 0;
    if (!match_83(ent, fc->name)) return 0;
    parse_83(ent, fc->out->name);
    fc->out->cluster = ((uint32_t)u16le(ent + 20) << 16) | u16le(ent + 26);
    fc->out->size    = u32le(ent + 28);
    fc->out->attr    = attr;
    fc->out->is_dir  = (attr & 0x10) ? 1 : 0;
    fc->found = 1;
    return 1; /* stop walking */
}

int fat32_find_entry(uint32_t dir_cluster, const char *name, fat32_entry_t *out) {
    if (!validate_83_name(name)) return -1;
    find_ctx_t fc = { name, out, 0 };
    if (walk_dir(dir_cluster, find_cb, &fc) < 0) return -1;
    return fc.found;
}

/* ── Read file ──────────────────────────────────────────────────────── */
int fat32_read_file(const fat32_entry_t *entry, uint8_t *buf, int max) {
    uint32_t clus = entry->cluster;
    int total = 0;
    uint32_t remaining = entry->size;
    uint32_t hops = 0;
    while (cluster_valid(clus) && remaining > 0) {
        if (hops++ > g_cluster_limit) return -1;
        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus && remaining > 0; s++) {
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            uint32_t chunk = remaining < 512 ? remaining : 512;
            if (total >= max) return total;
            if (total + (int)chunk > max) chunk = (uint32_t)(max - total);
            if (chunk == 0) return total;
            mem_copy(buf + total, g_sec_buf, chunk);
            total += (int)chunk;
            remaining -= chunk;
        }
        uint32_t next = 0;
        int next_r = fat_chain_next(clus, &next);
        if (next_r < 0) return -1;
        if (next_r == 0) break; /* EOC = legitimate end of file */
        clus = next;
    }
    /* Return bytes read; treat short cluster chain as EOF, not an error */
    return total;
}

/* ── Find existing dir entry by name and update cluster+size in-place,
      or write a brand-new entry if not found ──────────────────────── */
static int update_or_create_dir_entry(uint32_t dir_cluster, const char *name,
                                       uint32_t first_clus, uint32_t size) {
    uint32_t clus = dir_cluster;
    uint32_t hops = 0;
    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;
        uint32_t lba = cluster_to_lba(clus);
        uint8_t s;
        for (s = 0; s < g_sec_per_clus; s++) {
            int e;
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (e = 0; e < 16; e++) { /* 512/32 = 16 entries per sector */
                uint8_t *ent = g_sec_buf + e * 32;
                uint8_t first_byte = ent[0];
                uint8_t attr;
                if (first_byte == 0x00) goto not_found; /* end of directory */
                if (first_byte == 0xE5) continue;        /* deleted slot */
                attr = ent[11];
                if (attr == 0x0F || attr == 0x08) continue; /* LFN / volume label */
                if (!match_83(ent, name)) continue;
                /* Found — update cluster and size in-place */
                w16le(ent + 20, (uint16_t)(first_clus >> 16));
                w16le(ent + 26, (uint16_t)(first_clus & 0xFFFF));
                w32le(ent + 28, size);
                return disk_write_sectors(lba + s, 1, g_sec_buf);
            }
        }
        {
            uint32_t next = 0;
            int next_r = fat_chain_next(clus, &next);
            if (next_r < 0) return -1;
            if (next_r == 0) break;
            clus = next;
        }
    }
not_found:
    {
        uint8_t entry[32];
        mem_set(entry, 0, 32);
        to_83(name, entry);
        entry[11] = 0x20; /* archive */
        w16le(entry + 20, (uint16_t)(first_clus >> 16));
        w16le(entry + 26, (uint16_t)(first_clus & 0xFFFF));
        w32le(entry + 28, size);
        return write_dir_entry(dir_cluster, entry);
    }
}

/* ── Write a 32-byte dir entry into the first free slot in dir_cluster ── */
static int write_dir_entry(uint32_t dir_cluster, const uint8_t entry[32]) {
    uint32_t clus = dir_cluster;
    uint32_t hops = 0;
    /* First pass: find free slot */
    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;
        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (int e = 0; e < 512 / 32; e++) {
                uint8_t first = g_sec_buf[e * 32];
                if (first == 0x00 || first == 0xE5) {
                    mem_copy(g_sec_buf + e * 32, entry, 32);
                    return disk_write_sectors(lba + s, 1, g_sec_buf);
                }
            }
        }
        uint32_t next = 0;
        int next_r = fat_chain_next(clus, &next);
        if (next_r < 0) {
            return -1;
        }
        if (next_r == 0) {
            /* Extend chain */
            uint32_t nc = fat_alloc_cluster();
            if (!nc) return -1;
            if (fat_write_entry(clus, nc) != 0) return -1;
            if (fat_flush() != 0) return -1;
            /* Zero the new cluster */
            mem_set(g_sec_buf, 0, 512);
            uint32_t nlba = cluster_to_lba(nc);
            for (uint8_t s = 0; s < g_sec_per_clus; s++) {
                if (disk_write_sectors(nlba + s, 1, g_sec_buf) != 0) return -1;
            }
            clus = nc;
        } else {
            clus = next;
        }
    }
    return -1;
}

/* ── Write file (safe: write new chain first, swap dir entry, free old) ─ */
int fat32_write_file(uint32_t dir_cluster, const char *name,
                     const uint8_t *buf, uint32_t size) {
    if (!validate_83_name(name)) return -1;
    if (size > 0 && !buf) return -1;

    /* Step 1: remember old file's start cluster so we can free it later */
    uint32_t old_first_clus = 0;
    {
        fat32_entry_t existing;
        int found = fat32_find_entry(dir_cluster, name, &existing);
        if (found < 0) {
            return -1;
        }
        if (found > 0 && existing.is_dir) {
            return -1;
        }
        if (found > 0 && !existing.is_dir) {
            old_first_clus = existing.cluster;
        }
    }

    /* Step 2: allocate new cluster chain and write data */
    uint32_t first_clus = 0;
    uint32_t prev_clus  = 0;
    uint32_t written    = 0;
    uint32_t clus_size  = (uint32_t)g_sec_per_clus * 512;

    while (written < size) {
        uint32_t nc = fat_alloc_cluster();
        if (!nc) return -1;
        if (first_clus == 0) first_clus = nc;
        if (prev_clus && fat_write_entry(prev_clus, nc) != 0) return -1;

        uint32_t lba = cluster_to_lba(nc);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            uint32_t off = written + s * 512;
            if (off >= size) {
                mem_set(g_sec_buf, 0, 512);
            } else {
                uint32_t chunk = size - off;
                if (chunk > 512) chunk = 512;
                mem_copy(g_sec_buf, buf + off, chunk);
                if (chunk < 512) mem_set(g_sec_buf + chunk, 0, 512 - chunk);
            }
            if (disk_write_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
        }
        written += clus_size;
        prev_clus = nc;
        if (written >= size) break;
    }

    /* Step 3: flush new cluster chain to disk */
    if (fat_flush() != 0) return -1;

    /* Step 4: update existing dir entry or create a new one */
    if (update_or_create_dir_entry(dir_cluster, name, first_clus, size) != 0)
        return -1;

    /* Step 5: free old cluster chain (if any) — safe because dir already points to new */
    if (old_first_clus && cluster_valid(old_first_clus)) {
        free_cluster_chain(old_first_clus); /* best-effort; failure just leaks clusters */
    }

    return (int)size;
}

/* ── Delete entry (safe order: mark dir entry first, then free chain) ── */
int fat32_delete_entry(uint32_t dir_cluster, const char *name) {
    if (!validate_83_name(name)) return -1;
    uint32_t clus = dir_cluster;
    uint32_t hops = 0;
    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;
        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            int e;
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (e = 0; e < 16; e++) {
                uint8_t *ent = g_sec_buf + e * 32;
                uint32_t fc;
                if (ent[0] == 0x00) return 0; /* end of directory — not found */
                if (ent[0] == 0xE5) continue;
                {
                    uint8_t attr = ent[11];
                    if (attr == 0x0F || attr == 0x08) continue;
                }
                if (!match_83(ent, name)) continue;
                if (ent[11] & 0x10) return -1;

                /* Step 1: save cluster, mark entry deleted, write sector back */
                fc = ((uint32_t)u16le(ent + 20) << 16) | u16le(ent + 26);
                ent[0] = 0xE5;
                if (disk_write_sectors(lba + s, 1, g_sec_buf) != 0) return -1;

                /* Step 2: free cluster chain — failure leaves lost chains, not corruption */
                free_cluster_chain(fc);
                return 1;
            }
        }
        {
            uint32_t next = 0;
            int next_r = fat_chain_next(clus, &next);
            if (next_r < 0) return -1;
            if (next_r == 0) return 0;
            clus = next;
        }
    }
    return -1;
}

static int dir_is_effectively_empty(uint32_t dir_cluster) {
    uint32_t clus = dir_cluster;
    uint32_t hops = 0;

    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;

        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (int e = 0; e < 16; e++) {
                uint8_t *ent = g_sec_buf + e * 32;
                uint8_t attr;

                if (ent[0] == 0x00) return 1;
                if (ent[0] == 0xE5) continue;

                attr = ent[11];
                if (attr == 0x0F || attr == 0x08) continue;
                if (entry_is_dot(ent) || entry_is_dotdot(ent)) continue;
                return 0;
            }
        }

        {
            uint32_t next = 0;
            int next_r = fat_chain_next(clus, &next);
            if (next_r < 0) return -1;
            if (next_r == 0) return 1;
            clus = next;
        }
    }

    return -1;
}

/* ── mkdir ──────────────────────────────────────────────────────────── */
int fat32_mkdir(uint32_t dir_cluster, const char *name) {
    if (!validate_83_name(name)) return -1;

    {
        fat32_entry_t existing;
        int found = fat32_find_entry(dir_cluster, name, &existing);
        if (found > 0) return existing.is_dir ? 0 : -1;
        if (found < 0) return -1;
    }

    uint32_t nc = fat_alloc_cluster();
    if (!nc) return -1;
    if (fat_flush() != 0) return -1;

    /* Zero the cluster */
    mem_set(g_sec_buf, 0, 512);
    uint32_t lba = cluster_to_lba(nc);
    for (uint8_t s = 0; s < g_sec_per_clus; s++) {
        if (disk_write_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
    }

    /* Write '.' entry */
    uint8_t dot[32];
    mem_set(dot, 0, 32);
    mem_set(dot, ' ', 11);
    dot[0] = '.';
    dot[11] = 0x10;
    w16le(dot + 20, (uint16_t)(nc >> 16));
    w16le(dot + 26, (uint16_t)(nc & 0xFFFF));

    /* Write '..' entry */
    uint8_t dotdot[32];
    mem_set(dotdot, 0, 32);
    mem_set(dotdot, ' ', 11);
    dotdot[0] = '.'; dotdot[1] = '.';
    dotdot[11] = 0x10;
    w16le(dotdot + 20, (uint16_t)(dir_cluster >> 16));
    w16le(dotdot + 26, (uint16_t)(dir_cluster & 0xFFFF));

    /* Write both entries to first sector of new cluster */
    if (disk_read_sectors(lba, 1, g_sec_buf) != 0) return -1;
    mem_copy(g_sec_buf,      dot,    32);
    mem_copy(g_sec_buf + 32, dotdot, 32);
    if (disk_write_sectors(lba, 1, g_sec_buf) != 0) return -1;

    /* Create dir entry in parent */
    uint8_t entry[32];
    mem_set(entry, 0, 32);
    to_83(name, entry);
    entry[11] = 0x10; /* directory */
    w16le(entry + 20, (uint16_t)(nc >> 16));
    w16le(entry + 26, (uint16_t)(nc & 0xFFFF));
    if (write_dir_entry(dir_cluster, entry) != 0) return -1;
    return 1;
}

/* ── fat32_get_root_cluster ─────────────────────────────────────────── */
int fat32_rmdir(uint32_t dir_cluster, const char *name) {
    if (!validate_83_name(name)) return -1;

    uint32_t clus = dir_cluster;
    uint32_t hops = 0;
    while (cluster_valid(clus)) {
        if (hops++ > g_cluster_limit) return -1;

        uint32_t lba = cluster_to_lba(clus);
        for (uint8_t s = 0; s < g_sec_per_clus; s++) {
            if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
            for (int e = 0; e < 16; e++) {
                uint8_t *ent = g_sec_buf + e * 32;
                uint32_t target_cluster;
                uint8_t attr;

                if (ent[0] == 0x00) return 0;
                if (ent[0] == 0xE5) continue;

                attr = ent[11];
                if (attr == 0x0F || attr == 0x08) continue;
                if (!match_83(ent, name)) continue;
                if (!(attr & 0x10)) return -1;

                target_cluster = ((uint32_t)u16le(ent + 20) << 16) | u16le(ent + 26);
                if (!cluster_valid(target_cluster)) return -1;
                if (dir_is_effectively_empty(target_cluster) != 1) return -1;

                if (disk_read_sectors(lba + s, 1, g_sec_buf) != 0) return -1;
                ent = g_sec_buf + e * 32;
                ent[0] = 0xE5;
                if (disk_write_sectors(lba + s, 1, g_sec_buf) != 0) return -1;

                return free_cluster_chain(target_cluster) == 0 ? 1 : -1;
            }
        }

        {
            uint32_t next = 0;
            int next_r = fat_chain_next(clus, &next);
            if (next_r < 0) return -1;
            if (next_r == 0) return 0;
            clus = next;
        }
    }

    return -1;
}

uint32_t fat32_get_root_cluster(void) {
    return g_root_cluster_user;
}

/* ── fat32_init ─────────────────────────────────────────────────────── */
int fat32_get_info(fat32_info_t *out) {
    if (!out || g_root_cluster_user == 0 || g_data_start == 0) {
        return 0;
    }

    out->bytes_per_sector = g_bytes_per_sec;
    out->sectors_per_cluster = g_sec_per_clus;
    out->reserved_sectors = g_reserved_sectors;
    out->fat_count = g_fat_count;
    out->fat_size_sectors = g_fat_size;
    out->root_cluster = g_root_cluster_user;
    out->partition_start_lba = g_part_start;
    out->fat_start_lba = g_fat_start;
    out->data_start_lba = g_data_start;
    return 1;
}

int fat32_init(void) {
    if (!disk_available()) return 0;

    g_part_start = disk_partition_start();
    g_fat_start = 0;
    g_data_start = 0;
    g_bytes_per_sec = 0;
    g_reserved_sectors = 0;
    g_sec_per_clus = 0;
    g_fat_count = 0;
    g_fat_size = 0;
    g_root_cluster_fat = 0;
    g_root_cluster_user = 0;
    g_cluster_limit = 0;
    g_fat_cache_lba = (uint32_t)-1;
    g_fat_dirty = 0;

    boot_loading_step("Reading filesystem boot sector");

    /* Read BPB (sector 0 of partition) */
    if (disk_read_sectors(g_part_start, 1, g_sec_buf) != 0) {
        return 0;
    }

    boot_loading_step("Parsing filesystem metadata");

    /* Verify boot signature */
    if (g_sec_buf[510] != 0x55 || g_sec_buf[511] != 0xAA) {
        return 0;
    }

    g_bytes_per_sec = u16le(g_sec_buf + 11);
    if (g_bytes_per_sec != 512) {
        return 0;
    }

    g_sec_per_clus       = g_sec_buf[13];
    g_reserved_sectors   = u16le(g_sec_buf + 14);
    g_fat_count          = g_sec_buf[16];
    g_fat_size           = u32le(g_sec_buf + 36);
    g_root_cluster_fat   = u32le(g_sec_buf + 44);

    if (g_sec_per_clus == 0 || g_fat_count == 0 || g_fat_size == 0) {
        return 0;
    }

    g_fat_start  = g_part_start + g_reserved_sectors;
    g_data_start = g_fat_start + (uint32_t)g_fat_count * g_fat_size;

    uint32_t total_sectors = u32le(g_sec_buf + 32);
    if (total_sectors == 0) {
        total_sectors = (uint32_t)u16le(g_sec_buf + 19);
    }
    if (total_sectors <= g_reserved_sectors + (uint32_t)g_fat_count * g_fat_size) {
        return 0;
    }

    uint32_t data_sectors =
        total_sectors - g_reserved_sectors - (uint32_t)g_fat_count * g_fat_size;
    g_cluster_limit = 2u + (data_sectors / g_sec_per_clus);
    if (g_cluster_limit < 3u) {
        return 0;
    }
    if (!cluster_valid(g_root_cluster_fat)) {
        return 0;
    }

    /* Use the FAT32 root directory directly.
       The USB image stores files in the root cluster already. */
    g_root_cluster_user = g_root_cluster_fat;

    boot_loading_step("Filesystem metadata ready");

    return 1;
}
