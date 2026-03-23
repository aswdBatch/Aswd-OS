#include "lang/lang.h"
#include "lang/lang_priv.h"

#include <stdint.h>

#include "console/console.h"
#include "fs/vfs.h"
#include "lib/string.h"

/* ---- Global state ---- */
char     g_sp[LANG_STR_POOL_SIZE];
uint16_t g_sp_end;

lang_token_t g_toks[LANG_TOK_MAX];
int          g_tok_count;

lang_node_t g_nodes[LANG_NODE_MAX];
int         g_node_count;

int g_lang_error;

/* ---- String pool ---- */
uint16_t sp_add(const char *s, int len) {
    uint16_t off;
    int i;
    if ((int)g_sp_end + len + 1 >= LANG_STR_POOL_SIZE) {
        g_lang_error = 1;
        return 0;
    }
    off = g_sp_end;
    for (i = 0; i < len; i++) g_sp[g_sp_end + i] = s[i];
    g_sp[g_sp_end + len] = '\0';
    g_sp_end = (uint16_t)(g_sp_end + len + 1);
    return off;
}

const char *sp_get(uint16_t off) {
    return g_sp + off;
}

/* ---- Node allocator ---- */
int16_t node_alloc(node_t type) {
    int16_t idx;
    if (g_node_count >= LANG_NODE_MAX) {
        g_lang_error = 1;
        return -1;
    }
    idx = (int16_t)g_node_count++;
    g_nodes[idx].type  = (uint8_t)type;
    g_nodes[idx].left  = -1;
    g_nodes[idx].right = -1;
    g_nodes[idx].extra = -1;
    g_nodes[idx].ival  = 0;
    g_nodes[idx].sval  = 0;
    return idx;
}

/* ---- Error reporting ---- */
static void print_int(int v) {
    char buf[12];
    int neg = v < 0;
    if (neg) v = -v;
    u32_to_dec((uint32_t)v, buf, sizeof(buf));
    if (neg) console_write("-");
    console_write(buf);
}

void lang_error(int line, const char *msg) {
    g_lang_error = 1;
    console_write("[ax] line ");
    print_int(line);
    console_write(": ");
    console_writeln(msg);
}

/* ---- Source buffer ---- */
#define LANG_SRC_MAX 16384
static uint8_t g_src_buf[LANG_SRC_MAX];

/* ---- Entry points ---- */
void lang_run_str(const char *src, int len) {
    int16_t prog;

    g_sp_end     = 1;   /* offset 0 = empty/null string */
    g_sp[0]      = '\0';
    g_tok_count  = 0;
    g_node_count = 0;
    g_lang_error = 0;
    lang_eval_reset();

    lang_lex(src, len);
    if (g_lang_error) return;

    prog = lang_parse();
    if (g_lang_error) return;

    lang_eval(prog);
}

void lang_run_file(const char *path) {
    int n = vfs_cat(path, g_src_buf, LANG_SRC_MAX - 1);
    if (n < 0) {
        console_write("[ax] cannot open: ");
        console_writeln(path);
        return;
    }
    g_src_buf[n] = '\0';
    lang_run_str((const char *)g_src_buf, n);
}
