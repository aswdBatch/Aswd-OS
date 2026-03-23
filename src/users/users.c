#include "users/users.h"

#include <stddef.h>
#include <stdint.h>

#include "auth/auth_store.h"
#include "fs/vfs.h"
#include "lib/string.h"

#define USERS_MAX 8
#define USER_NAME_MAX 9

#define USERS_DIR_FILE   "USERS"
#define ACTIVE_USER_FILE "ACTIVE.USR"
#define ADMIN_USER_FILE  "ADMIN.USR"

/* User persistence is temporarily disabled on real hardware while the
   filesystem write path is being stabilized. Authentication continues to use
   the hardcoded Guest/devacc flow. */
#define USERS_PERSISTENCE_ENABLED 0

static char g_current_user[USER_NAME_MAX];
static char g_admin_user[USER_NAME_MAX];
static char g_users[USERS_MAX][USER_NAME_MAX];
static int g_user_count = 0;
static int g_needs_setup = 1;

static void users_set_current(const char *name) {
    str_copy(g_current_user, name ? name : "", sizeof(g_current_user));
}

static void users_set_admin(const char *name) {
    str_copy(g_admin_user, name ? name : "", sizeof(g_admin_user));
}

static int users_normalize_name(const char *src, char *out, size_t out_size) {
    int len = 0;

    if (!src || !src[0] || !out || out_size < USER_NAME_MAX) {
        if (out && out_size > 0) out[0] = '\0';
        return 0;
    }

    for (int i = 0; src[i]; i++) {
        char ch = src[i];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
            out[0] = '\0';
            return 0;
        }
        if (len + 1 >= USER_NAME_MAX) {
            out[0] = '\0';
            return 0;
        }
        out[len++] = ch;
    }

    if (len == 0) {
        out[0] = '\0';
        return 0;
    }

    out[len] = '\0';
    return 1;
}

static int users_restore_path(const char *path) {
    char segment[13];
    int seg_len = 0;

    if (!vfs_available()) {
        return 0;
    }
    if (!path || path[0] != '/') {
        return vfs_cd("/");
    }
    if (!vfs_cd("/")) {
        return 0;
    }
    if (path[1] == '\0') {
        return 1;
    }

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
            if (ch == '\0') {
                break;
            }
        } else if (seg_len + 1 < (int)sizeof(segment)) {
            segment[seg_len++] = ch;
        }
    }
    return 1;
}

static void users_record(const char *name) {
    if (!name || !name[0] || g_user_count >= USERS_MAX) return;
    for (int i = 0; i < g_user_count; i++) {
        if (str_eq(g_users[i], name)) return;
    }
    str_copy(g_users[g_user_count++], name, USER_NAME_MAX);
}

static int users_index_of(const char *name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < g_user_count; i++) {
        if (str_eq(g_users[i], name)) return i;
    }
    return -1;
}

static int users_read_root_file(const char *name, char *out, size_t out_size) {
    char saved[256];
    uint8_t buf[16];
    int read;

    if (!vfs_available() || !name || !out || out_size < USER_NAME_MAX) {
        if (out && out_size > 0) out[0] = '\0';
        return 0;
    }

    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) {
        out[0] = '\0';
        return 0;
    }

    read = vfs_cat(name, buf, (int)sizeof(buf) - 1);
    users_restore_path(saved);
    if (read <= 0) {
        out[0] = '\0';
        return 0;
    }

    buf[read] = '\0';
    return users_normalize_name((const char *)buf, out, out_size);
}

static int users_write_root_file(const char *name, const char *value) {
    char saved[256];
    char normalized[USER_NAME_MAX];

    if (!vfs_available() || !name || !value) return 0;
    if (!users_normalize_name(value, normalized, sizeof(normalized))) return 0;

    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return 0;
    if (vfs_write(name, (const uint8_t *)normalized, (uint32_t)str_len(normalized)) <= 0) {
        users_restore_path(saved);
        return 0;
    }
    users_restore_path(saved);
    return 1;
}

static void users_delete_root_file(const char *name) {
    char saved[256];
    if (!vfs_available() || !name) return;
    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (vfs_cd("/")) {
        (void)vfs_rm(name);
    }
    users_restore_path(saved);
}

static int users_ensure_root(void) {
    char saved[256];
    if (!vfs_available()) return 0;
    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return 0;
    if (!vfs_cd(USERS_DIR_FILE)) {
        if (vfs_mkdir(USERS_DIR_FILE) <= 0 && !vfs_cd(USERS_DIR_FILE)) {
            users_restore_path(saved);
            return 0;
        }
    }
    users_restore_path(saved);
    return 1;
}

static int users_ensure_home(const char *name) {
    char saved[256];
    if (!vfs_available() || !name || !name[0]) return 0;
    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return 0;
    if (!vfs_cd(USERS_DIR_FILE)) {
        if (vfs_mkdir(USERS_DIR_FILE) <= 0 && !vfs_cd(USERS_DIR_FILE)) {
            users_restore_path(saved);
            return 0;
        }
    }
    if (!vfs_cd(name)) {
        if (vfs_mkdir(name) <= 0 && !vfs_cd(name)) {
            users_restore_path(saved);
            return 0;
        }
    }
    users_restore_path(saved);
    return 1;
}

static int users_enter_home(const char *name) {
    if (!vfs_available() || !name || !name[0]) return 0;
    if (!vfs_cd("/")) return 0;
    if (!vfs_cd(USERS_DIR_FILE)) return 0;
    return vfs_cd(name);
}

static void users_refresh_list(void) {
    char saved[256];
    fat32_entry_t entries[32];
    int count;

    g_user_count = 0;
    if (!vfs_available()) {
        return;
    }

    str_copy(saved, vfs_cwd_path(), sizeof(saved));
    if (!vfs_cd("/")) return;
    if (!vfs_cd(USERS_DIR_FILE)) {
        users_restore_path(saved);
        return;
    }

    count = vfs_ls(entries, 32);
    if (count > 0) {
        for (int i = 0; i < count && g_user_count < USERS_MAX; i++) {
            if (entries[i].is_dir) {
                users_record(entries[i].name);
            }
        }
    }

    users_restore_path(saved);
}

static void users_enter_root_if_possible(void) {
    if (vfs_available()) {
        (void)vfs_cd("/");
    }
}

void users_init(void) {
    char active[USER_NAME_MAX];
    char admin[USER_NAME_MAX];

    g_user_count = 0;
    g_needs_setup = 0;
    users_set_current("");
    users_set_admin("");

    if (!USERS_PERSISTENCE_ENABLED) {
        return;
    }

    g_needs_setup = 1;

    if (!vfs_available()) {
        users_record("OFFLINE");
        users_set_current("OFFLINE");
        users_set_admin("OFFLINE");
        g_needs_setup = 0;
        return;
    }

    (void)users_ensure_root();
    users_refresh_list();

    if (users_read_root_file(ADMIN_USER_FILE, admin, sizeof(admin)) &&
        users_index_of(admin) >= 0) {
        users_set_admin(admin);
        g_needs_setup = 0;
    }

    if (g_needs_setup || g_user_count == 0) {
        users_delete_root_file(ACTIVE_USER_FILE);
        users_set_current("");
        users_enter_root_if_possible();
        return;
    }

    if (users_read_root_file(ACTIVE_USER_FILE, active, sizeof(active)) &&
        users_index_of(active) >= 0) {
        users_set_current(active);
        (void)users_enter_home(active);
        return;
    }

    users_set_current("");
    users_enter_root_if_possible();
}

const char *users_current(void) {
    if (!g_current_user[0] && auth_session_active()) {
        return auth_session_username();
    }
    return g_current_user[0] ? g_current_user : "None";
}

int users_count(void) {
    return g_user_count;
}

const char *users_name_at(int index) {
    if (index < 0 || index >= g_user_count) return 0;
    return g_users[index];
}

int users_has_active(void) {
    return g_current_user[0] != '\0' || auth_session_active();
}

int users_needs_setup(void) {
    return g_needs_setup;
}

int users_current_is_admin(void) {
    if (!USERS_PERSISTENCE_ENABLED) {
        return 0;
    }
    return g_current_user[0] && g_admin_user[0] && str_eq(g_current_user, g_admin_user);
}

int users_create(const char *name, int make_admin) {
    char normalized[USER_NAME_MAX];
    int existed;

    if (!USERS_PERSISTENCE_ENABLED) return 0;
    if (!vfs_available()) return 0;
    if (!users_normalize_name(name, normalized, sizeof(normalized))) return 0;
    if (!users_ensure_root()) return 0;

    users_refresh_list();
    existed = users_index_of(normalized) >= 0;
    if (!existed) {
        if (g_user_count >= USERS_MAX) return 0;
        if (!users_ensure_home(normalized)) return 0;
        users_refresh_list();
    } else if (!make_admin && g_admin_user[0]) {
        return 0;
    }

    if (make_admin || !g_admin_user[0]) {
        if (!users_write_root_file(ADMIN_USER_FILE, normalized)) return 0;
        users_set_admin(normalized);
        g_needs_setup = 0;
    }

    return users_index_of(normalized) >= 0;
}

int users_switch(const char *name) {
    char normalized[USER_NAME_MAX];

    if (!name || !name[0]) return 0;
    if (!users_normalize_name(name, normalized, sizeof(normalized))) return 0;

    if (!USERS_PERSISTENCE_ENABLED) {
        users_set_current(normalized);
        return 1;
    }

    if (!vfs_available()) {
        users_set_current(normalized);
        users_refresh_list();
        return 1;
    }

    users_refresh_list();
    if (users_index_of(normalized) < 0) {
        if (!users_ensure_home(normalized)) return 0;
        users_refresh_list();
    }
    if (users_index_of(normalized) < 0) return 0;
    if (!users_write_root_file(ACTIVE_USER_FILE, normalized)) return 0;
    if (!users_enter_home(normalized)) return 0;

    users_set_current(normalized);
    return 1;
}

int users_create_next(void) {
    char name[USER_NAME_MAX];
    char num[4];

    if (!USERS_PERSISTENCE_ENABLED) return 0;

    for (int i = 1; i < 100; i++) {
        str_copy(name, "USER", sizeof(name));
        u32_to_dec((uint32_t)i, num, sizeof(num));
        str_cat(name, num, sizeof(name));
        if (users_create(name, 0)) {
            return 1;
        }
    }

    return 0;
}

void users_logout(void) {
    users_set_current("");
    if (!USERS_PERSISTENCE_ENABLED) return;
    if (!vfs_available()) return;
    users_delete_root_file(ACTIVE_USER_FILE);
    users_enter_root_if_possible();
}

void users_home_path(char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!g_current_user[0]) {
        str_copy(out, "/", out_size);
        return;
    }
    out[0] = '\0';
    str_copy(out, "/USERS/", out_size);
    str_cat(out, g_current_user, out_size);
}
