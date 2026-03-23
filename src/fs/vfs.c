#include "fs/vfs.h"

#include "drivers/fat32.h"
#include "lib/string.h"

static int      g_ready = 0;
static uint32_t g_cwd_cluster;
static char     g_cwd_path[256];
static int      g_write_guard_enabled = 0;

#define VFS_MAX_DEPTH 8
#define VFS_WORKSPACE_NAME "ROOT"
#define VFS_WORKSPACE_PATH "/ROOT"

typedef struct {
    uint32_t cluster;
    char     name[13];
} vfs_stack_entry_t;

typedef struct {
    uint32_t cluster;
    char     path[256];
    vfs_stack_entry_t stack[VFS_MAX_DEPTH];
    int      depth;
} vfs_dir_state_t;

static vfs_stack_entry_t g_stack[VFS_MAX_DEPTH];
static int g_depth = 0;

static void rebuild_path_from_stack(const vfs_stack_entry_t *stack, int depth,
                                    char *out, uint32_t out_size) {
    int i;

    if (out_size == 0) {
        return;
    }

    out[0] = '/';
    out[1] = '\0';

    for (i = 0; i < depth; i++) {
        str_cat(out, stack[i].name, out_size);
        if (i + 1 < depth) {
            str_cat(out, "/", out_size);
        }
    }
}

static void dir_set_root(vfs_dir_state_t *dir) {
    dir->cluster = fat32_get_root_cluster();
    dir->path[0] = '/';
    dir->path[1] = '\0';
    dir->depth = 0;
}

static void dir_from_current(vfs_dir_state_t *dir) {
    dir->cluster = g_cwd_cluster;
    str_copy(dir->path, g_cwd_path, sizeof(dir->path));
    mem_copy(dir->stack, g_stack, sizeof(g_stack));
    dir->depth = g_depth;
}

static void dir_apply(const vfs_dir_state_t *dir) {
    g_cwd_cluster = dir->cluster;
    str_copy(g_cwd_path, dir->path, sizeof(g_cwd_path));
    mem_copy(g_stack, dir->stack, sizeof(g_stack));
    g_depth = dir->depth;
}

static void dir_rebuild(vfs_dir_state_t *dir) {
    dir->cluster = (dir->depth > 0)
        ? dir->stack[dir->depth - 1].cluster
        : fat32_get_root_cluster();
    rebuild_path_from_stack(dir->stack, dir->depth, dir->path, sizeof(dir->path));
}

static int dir_push(vfs_dir_state_t *dir, const fat32_entry_t *entry) {
    if (dir->depth >= VFS_MAX_DEPTH) {
        return 0;
    }

    dir->stack[dir->depth].cluster = entry->cluster;
    str_copy(dir->stack[dir->depth].name, entry->name,
             sizeof(dir->stack[dir->depth].name));
    dir->depth++;
    dir_rebuild(dir);
    return 1;
}

static void dir_pop(vfs_dir_state_t *dir) {
    if (dir->depth > 0) {
        dir->depth--;
    }
    dir_rebuild(dir);
}

static int segment_eq(const char *start, int len, const char *lit) {
    int i;

    for (i = 0; i < len && lit[i]; i++) {
        if (start[i] != lit[i]) {
            return 0;
        }
    }
    return i == len && lit[i] == '\0';
}

static int copy_segment(char *dst, int dst_size, const char *start, int len) {
    int i;

    if (len <= 0 || len >= dst_size) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        dst[i] = start[i];
    }
    dst[len] = '\0';
    return 1;
}

static int resolve_directory(const char *path, vfs_dir_state_t *out) {
    vfs_dir_state_t dir;
    const char *p;

    if (!path || !out) {
        return 0;
    }

    if (path[0] == '/') {
        dir_set_root(&dir);
        while (*path == '/') {
            path++;
        }
    } else {
        dir_from_current(&dir);
    }

    p = path;
    while (*p) {
        const char *seg_start;
        int seg_len;

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }

        seg_start = p;
        while (*p && *p != '/') {
            p++;
        }
        seg_len = (int)(p - seg_start);

        if (segment_eq(seg_start, seg_len, ".")) {
            continue;
        }

        if (segment_eq(seg_start, seg_len, "..")) {
            dir_pop(&dir);
            continue;
        }

        {
            char segment[13];
            fat32_entry_t entry;
            int found;

            if (!copy_segment(segment, sizeof(segment), seg_start, seg_len)) {
                return 0;
            }

            found = fat32_find_entry(dir.cluster, segment, &entry);
            if (found <= 0 || !entry.is_dir) {
                return 0;
            }
            if (!dir_push(&dir, &entry)) {
                return 0;
            }
        }
    }

    *out = dir;
    return 1;
}

static int split_parent_leaf(const char *path, vfs_dir_state_t *parent,
                             char *leaf, int leaf_size) {
    int len;
    int slash = -1;

    if (!path || !path[0] || !parent || !leaf || leaf_size <= 0) {
        return 0;
    }

    len = (int)str_len(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    if (len <= 0) {
        return 0;
    }

    for (int i = 0; i < len; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    if (slash < 0) {
        dir_from_current(parent);
        return copy_segment(leaf, leaf_size, path, len);
    }

    if (slash == 0) {
        dir_set_root(parent);
        return copy_segment(leaf, leaf_size, path + 1, len - 1);
    }

    {
        char parent_path[256];

        if (!copy_segment(parent_path, sizeof(parent_path), path, slash)) {
            return 0;
        }
        if (!copy_segment(leaf, leaf_size, path + slash + 1, len - slash - 1)) {
            return 0;
        }
        return resolve_directory(parent_path, parent);
    }
}

static int path_is_under_workspace(const char *path) {
    size_t prefix_len;

    if (!path) {
        return 0;
    }

    if (str_eq(path, VFS_WORKSPACE_PATH)) {
        return 1;
    }

    prefix_len = str_len(VFS_WORKSPACE_PATH);
    return str_ncmp(path, VFS_WORKSPACE_PATH, prefix_len) == 0 &&
           path[prefix_len] == '/';
}

static int ensure_root_workspace_exists(void) {
    fat32_entry_t entry;
    int found = fat32_find_entry(fat32_get_root_cluster(), VFS_WORKSPACE_NAME, &entry);

    if (found < 0) {
        return 0;
    }
    if (found > 0) {
        return entry.is_dir;
    }
    if (g_write_guard_enabled) {
        return 0;
    }
    return fat32_mkdir(fat32_get_root_cluster(), VFS_WORKSPACE_NAME) >= 0;
}

int vfs_init(void) {
    if (!fat32_init()) {
        g_ready = 0;
        return 0;
    }

    g_cwd_cluster = fat32_get_root_cluster();
    g_cwd_path[0] = '/';
    g_cwd_path[1] = '\0';
    g_depth = 0;
    g_ready = 1;
    g_write_guard_enabled = 0;

    if (!vfs_enter_root_workspace()) {
        g_ready = 0;
        return 0;
    }

    g_write_guard_enabled = 1;
    return 1;
}

int vfs_available(void) {
    return g_ready;
}

const char *vfs_cwd_path(void) {
    return g_cwd_path;
}

int vfs_cwd_is_writable(void) {
    if (!g_ready) {
        return 0;
    }
    return path_is_under_workspace(g_cwd_path);
}

int vfs_enter_root_workspace(void) {
    if (!g_ready) {
        return 0;
    }
    if (!ensure_root_workspace_exists()) {
        return 0;
    }
    return vfs_cd(VFS_WORKSPACE_PATH);
}

int vfs_ls(fat32_entry_t *entries, int max) {
    if (!g_ready) {
        return -1;
    }
    return fat32_list_dir(g_cwd_cluster, entries, max);
}

int vfs_cd(const char *path) {
    vfs_dir_state_t dir;

    if (!g_ready || !path) {
        return -1;
    }

    if (path[0] == '\0') {
        return 1;
    }

    if (!resolve_directory(path, &dir)) {
        return 0;
    }

    dir_apply(&dir);
    return 1;
}

int vfs_cat(const char *name, uint8_t *buf, int max) {
    vfs_dir_state_t parent;
    fat32_entry_t entry;
    char leaf[13];
    int found;

    if (!g_ready) {
        return -1;
    }
    if (!split_parent_leaf(name, &parent, leaf, sizeof(leaf))) {
        return -1;
    }

    found = fat32_find_entry(parent.cluster, leaf, &entry);
    if (found <= 0 || entry.is_dir) {
        return -1;
    }
    return fat32_read_file(&entry, buf, max);
}

int vfs_write(const char *name, const uint8_t *data, uint32_t size) {
    vfs_dir_state_t parent;
    char leaf[13];

    if (!g_ready) {
        return -1;
    }
    if (!split_parent_leaf(name, &parent, leaf, sizeof(leaf))) {
        return -1;
    }
    if (g_write_guard_enabled && !path_is_under_workspace(parent.path)) {
        return -1;
    }
    return fat32_write_file(parent.cluster, leaf, data, size);
}

int vfs_rm(const char *name) {
    vfs_dir_state_t parent;
    char leaf[13];

    if (!g_ready) {
        return -1;
    }
    if (!split_parent_leaf(name, &parent, leaf, sizeof(leaf))) {
        return -1;
    }
    if (g_write_guard_enabled && !path_is_under_workspace(parent.path)) {
        return -1;
    }
    return fat32_delete_entry(parent.cluster, leaf);
}

int vfs_mkdir(const char *name) {
    vfs_dir_state_t parent;
    char leaf[13];

    if (!g_ready) {
        return -1;
    }
    if (!split_parent_leaf(name, &parent, leaf, sizeof(leaf))) {
        return -1;
    }
    if (g_write_guard_enabled && !path_is_under_workspace(parent.path)) {
        return -1;
    }
    return fat32_mkdir(parent.cluster, leaf);
}

int vfs_rmdir(const char *name) {
    vfs_dir_state_t parent;
    char leaf[13];

    if (!g_ready) {
        return -1;
    }
    if (!split_parent_leaf(name, &parent, leaf, sizeof(leaf))) {
        return -1;
    }
    if (g_write_guard_enabled && !path_is_under_workspace(parent.path)) {
        return -1;
    }
    return fat32_rmdir(parent.cluster, leaf);
}
