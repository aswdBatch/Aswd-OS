#include "net/site_allow.h"

#include <stdint.h>

#include "fs/vfs.h"
#include "lib/string.h"

#define SITE_ALLOW_FILE "TLSALLOW.CFG"
#define SITE_ALLOW_BUF_SIZE (SITE_ALLOW_MAX * (SITE_ALLOW_HOST_MAX + 2))

static char g_hosts[SITE_ALLOW_MAX][SITE_ALLOW_HOST_MAX];
static int  g_count = 0;
static int  g_loaded = 0;

static int site_allow_restore_path(const char *path) {
    char segment[13];
    int seg_len = 0;

    if (!vfs_available()) return 0;
    if (!path || path[0] != '/') return vfs_cd("/");
    if (!vfs_cd("/")) return 0;
    if (path[1] == '\0') return 1;

    for (int i = 1; ; i++) {
        char ch = path[i];
        if (ch == '/' || ch == '\0') {
            if (seg_len > 0) {
                segment[seg_len] = '\0';
                if (!vfs_cd(segment)) {
                    return 0;
                }
                seg_len = 0;
            }
            if (ch == '\0') break;
        } else if (seg_len + 1 < (int)sizeof(segment)) {
            segment[seg_len++] = ch;
        }
    }
    return 1;
}

static int site_allow_normalize_host(const char *input, char *out, size_t out_size) {
    const char *p;
    int len = 0;
    int label_len = 0;
    int dot_pending = 0;

    if (!input || !out || out_size < 2) {
        if (out && out_size > 0) out[0] = '\0';
        return 0;
    }

    p = input;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (str_ncmp(p, "http://", 7) == 0) p += 7;
    else if (str_ncmp(p, "https://", 8) == 0) p += 8;

    {
        const char *scan = p;
        const char *host_start = p;
        while (*scan && *scan != '/' && *scan != '?' && *scan != '#') {
            if (*scan == '@') host_start = scan + 1;
            scan++;
        }
        p = host_start;
    }

    while (*p) {
        char ch = *p++;
        if (ch == '/' || ch == '?' || ch == '#' || ch == ':') break;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (dot_pending) {
                if (len == 0 || len + 1 >= (int)out_size) {
                    out[0] = '\0';
                    return 0;
                }
                out[len++] = '.';
                dot_pending = 0;
                label_len = 0;
            }
            if (len + 1 >= (int)out_size) {
                out[0] = '\0';
                return 0;
            }
            out[len++] = ch;
            label_len++;
            if (label_len > 63) {
                out[0] = '\0';
                return 0;
            }
            continue;
        }
        if (ch == '.') {
            if (len == 0 || dot_pending || label_len == 0) {
                out[0] = '\0';
                return 0;
            }
            dot_pending = 1;
            continue;
        }
        if (ch == '-') {
            if (len == 0 || dot_pending || label_len == 0 || len + 1 >= (int)out_size) {
                out[0] = '\0';
                return 0;
            }
            out[len++] = ch;
            label_len++;
            continue;
        }
        out[0] = '\0';
        return 0;
    }

    if (len == 0 || dot_pending || out[len - 1] == '-') {
        out[0] = '\0';
        return 0;
    }

    out[len] = '\0';
    return 1;
}

static int site_allow_find(const char *host) {
    for (int i = 0; i < g_count; i++) {
        if (str_eq(g_hosts[i], host)) return i;
    }
    return -1;
}

static int site_allow_save(void) {
    char saved[256];
    char buf[SITE_ALLOW_BUF_SIZE];
    int pos = 0;

    if (!vfs_available()) return 1;

    buf[0] = '\0';
    for (int i = 0; i < g_count; i++) {
        int len = (int)str_len(g_hosts[i]);
        if (pos + len + 2 >= (int)sizeof(buf)) break;
        mem_copy(buf + pos, g_hosts[i], (size_t)len);
        pos += len;
        buf[pos++] = '\n';
    }

    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return 0;
    if (g_count == 0) {
        (void)vfs_rm(SITE_ALLOW_FILE);
        site_allow_restore_path(saved);
        return 1;
    }
    if (vfs_write(SITE_ALLOW_FILE, (const uint8_t *)buf, (uint32_t)pos) <= 0) {
        site_allow_restore_path(saved);
        return 0;
    }
    site_allow_restore_path(saved);
    return 1;
}

static void site_allow_load(void) {
    char saved[256];
    uint8_t buf[SITE_ALLOW_BUF_SIZE];
    int read;
    int start = 0;

    if (g_loaded) return;
    g_loaded = 1;
    g_count = 0;

    if (!vfs_available()) return;

    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return;
    read = vfs_cat(SITE_ALLOW_FILE, buf, (int)sizeof(buf) - 1);
    site_allow_restore_path(saved);
    if (read <= 0) return;

    buf[read] = '\0';
    for (int i = 0; i <= read && g_count < SITE_ALLOW_MAX; i++) {
        if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\0') {
            if (i > start) {
                char host[SITE_ALLOW_HOST_MAX];
                char normalized[SITE_ALLOW_HOST_MAX];
                int len = i - start;
                if (len >= (int)sizeof(host)) len = (int)sizeof(host) - 1;
                mem_copy(host, buf + start, (size_t)len);
                host[len] = '\0';
                if (site_allow_normalize_host(host, normalized, sizeof(normalized)) &&
                    site_allow_find(normalized) < 0) {
                    str_copy(g_hosts[g_count], normalized, sizeof(g_hosts[g_count]));
                    g_count++;
                }
            }
            while (i + 1 <= read && (buf[i + 1] == '\n' || buf[i + 1] == '\r')) i++;
            start = i + 1;
        }
    }
}

void site_allow_init(void) {
    site_allow_load();
}

int site_allow_count(void) {
    site_allow_load();
    return g_count;
}

const char *site_allow_host_at(int index) {
    site_allow_load();
    if (index < 0 || index >= g_count) return 0;
    return g_hosts[index];
}

int site_allow_add(const char *input) {
    char host[SITE_ALLOW_HOST_MAX];

    site_allow_load();
    if (!site_allow_normalize_host(input, host, sizeof(host))) return SITE_ALLOW_ERR_INVALID;
    if (site_allow_find(host) >= 0) return SITE_ALLOW_ERR_EXISTS;
    if (g_count >= SITE_ALLOW_MAX) return SITE_ALLOW_ERR_FULL;

    str_copy(g_hosts[g_count], host, sizeof(g_hosts[g_count]));
    g_count++;
    if (!site_allow_save()) {
        g_count--;
        g_hosts[g_count][0] = '\0';
        return SITE_ALLOW_ERR_STORE;
    }
    return SITE_ALLOW_OK;
}

int site_allow_remove(const char *input) {
    char host[SITE_ALLOW_HOST_MAX];
    int idx;

    site_allow_load();
    if (!site_allow_normalize_host(input, host, sizeof(host))) return SITE_ALLOW_ERR_INVALID;
    idx = site_allow_find(host);
    if (idx < 0) return SITE_ALLOW_ERR_NOT_FOUND;

    for (int i = idx; i + 1 < g_count; i++) {
        str_copy(g_hosts[i], g_hosts[i + 1], sizeof(g_hosts[i]));
    }
    g_count--;
    g_hosts[g_count][0] = '\0';
    if (!site_allow_save()) {
        return SITE_ALLOW_ERR_STORE;
    }
    return SITE_ALLOW_OK;
}

int site_allow_matches(const char *host) {
    char normalized[SITE_ALLOW_HOST_MAX];

    site_allow_load();
    if (g_count == 0) return 1;
    if (!site_allow_normalize_host(host, normalized, sizeof(normalized))) return 0;

    for (int i = 0; i < g_count; i++) {
        int host_len = (int)str_len(normalized);
        int item_len = (int)str_len(g_hosts[i]);
        if (host_len == item_len && str_eq(normalized, g_hosts[i])) return 1;
        if (host_len > item_len &&
            normalized[host_len - item_len - 1] == '.' &&
            str_eq(normalized + host_len - item_len, g_hosts[i])) {
            return 1;
        }
    }
    return 0;
}

int site_allow_enabled(void) {
    return site_allow_count() > 0;
}

int site_allow_persistent_available(void) {
    return vfs_available();
}
