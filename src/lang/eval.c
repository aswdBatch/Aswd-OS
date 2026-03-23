#include "lang/lang_priv.h"

#include <stdint.h>

#include "console/console.h"
#include "input/input.h"
#include "lib/string.h"
#include "shell/commands.h"

/* ---- Value type ---- */
typedef enum { VAL_NIL = 0, VAL_INT, VAL_STR, VAL_BOOL } val_t;

typedef struct {
    val_t    type;
    int32_t  i;      /* VAL_INT value or VAL_BOOL (0/1) */
    uint16_t s;      /* string pool offset for VAL_STR */
} lang_val_t;

static lang_val_t make_int(int32_t i)  { lang_val_t v; v.type=VAL_INT;  v.i=i; v.s=0; return v; }
static lang_val_t make_str(uint16_t s) { lang_val_t v; v.type=VAL_STR;  v.i=0; v.s=s; return v; }
static lang_val_t make_bool(int i)     { lang_val_t v; v.type=VAL_BOOL; v.i=i; v.s=0; return v; }
static lang_val_t make_nil(void)       { lang_val_t v; v.type=VAL_NIL;  v.i=0; v.s=0; return v; }

/* ---- Variable table ---- */
#define VAR_MAX 128

typedef struct {
    uint16_t   name;
    lang_val_t val;
    uint8_t    depth;
    uint8_t    active;
} var_entry_t;

static var_entry_t g_vars[VAR_MAX];
static int         g_scope_depth = 0;

static void var_table_clear(void) {
    int i;
    for (i = 0; i < VAR_MAX; i++) g_vars[i].active = 0;
    g_scope_depth = 0;
}

static void scope_enter(void) { g_scope_depth++; }

static void scope_leave(void) {
    int i;
    for (i = 0; i < VAR_MAX; i++)
        if (g_vars[i].active && g_vars[i].depth == (uint8_t)g_scope_depth)
            g_vars[i].active = 0;
    if (g_scope_depth > 0) g_scope_depth--;
}

static var_entry_t *var_find(uint16_t name) {
    int i;
    for (i = 0; i < VAR_MAX; i++)
        if (g_vars[i].active && g_vars[i].name == name)
            return &g_vars[i];
    return 0;
}

static void var_let(uint16_t name, lang_val_t val) {
    int i;
    for (i = 0; i < VAR_MAX; i++) {
        if (!g_vars[i].active) {
            g_vars[i].active = 1;
            g_vars[i].name   = name;
            g_vars[i].val    = val;
            g_vars[i].depth  = (uint8_t)g_scope_depth;
            return;
        }
    }
    lang_error(0, "variable table full");
}

static void var_assign(uint16_t name, lang_val_t val) {
    var_entry_t *v = var_find(name);
    if (v) { v->val = val; return; }
    lang_error(0, "undefined variable");
}

/* ---- Function table ---- */
#define FN_MAX 32

typedef struct {
    uint16_t name;
    int16_t  params;  /* first N_PARAM node index */
    int16_t  body;    /* N_BLOCK head */
} fn_entry_t;

static fn_entry_t g_fns[FN_MAX];
static int        g_fn_count = 0;

static fn_entry_t *fn_find(uint16_t name) {
    int i;
    for (i = 0; i < g_fn_count; i++)
        if (g_fns[i].name == name) return &g_fns[i];
    return 0;
}

static void fn_define(uint16_t name, int16_t params, int16_t body) {
    fn_entry_t *existing = fn_find(name);
    if (existing) { existing->params = params; existing->body = body; return; }
    if (g_fn_count >= FN_MAX) { lang_error(0, "too many functions"); return; }
    g_fns[g_fn_count].name   = name;
    g_fns[g_fn_count].params = params;
    g_fns[g_fn_count].body   = body;
    g_fn_count++;
}

/* ---- Return / call depth ---- */
static int        g_return_flag  = 0;
static lang_val_t g_return_val;
static int        g_call_depth   = 0;

#define MAX_CALL_DEPTH 32

/* ---- String helpers ---- */
#define STR_TMP_SIZE 512
static char g_str_tmp[STR_TMP_SIZE];

static uint16_t val_to_spool(lang_val_t v) {
    if (v.type == VAL_STR)  return v.s;
    if (v.type == VAL_BOOL) return sp_add(v.i ? "true" : "false", v.i ? 4 : 5);
    if (v.type == VAL_NIL)  return sp_add("nil", 3);
    /* VAL_INT */
    {
        int neg = v.i < 0;
        u32_to_dec((uint32_t)(neg ? -v.i : v.i), g_str_tmp, sizeof(g_str_tmp));
        if (neg) {
            int len = (int)str_len(g_str_tmp);
            int j;
            for (j = len; j >= 0; j--) g_str_tmp[j+1] = g_str_tmp[j];
            g_str_tmp[0] = '-';
        }
    }
    return sp_add(g_str_tmp, (int)str_len(g_str_tmp));
}

static uint16_t str_concat(uint16_t a, uint16_t b) {
    const char *sa = sp_get(a);
    const char *sb = sp_get(b);
    str_copy(g_str_tmp, sa, sizeof(g_str_tmp));
    str_cat(g_str_tmp, sb, sizeof(g_str_tmp));
    return sp_add(g_str_tmp, (int)str_len(g_str_tmp));
}

static void print_val(lang_val_t v) {
    if (v.type == VAL_STR)  { console_writeln(sp_get(v.s)); return; }
    if (v.type == VAL_BOOL) { console_writeln(v.i ? "true" : "false"); return; }
    if (v.type == VAL_NIL)  { console_writeln("nil"); return; }
    /* VAL_INT */
    {
        int neg = v.i < 0;
        u32_to_dec((uint32_t)(neg ? -v.i : v.i), g_str_tmp, sizeof(g_str_tmp));
        if (neg) {
            int len = (int)str_len(g_str_tmp);
            int j;
            for (j = len; j >= 0; j--) g_str_tmp[j+1] = g_str_tmp[j];
            g_str_tmp[0] = '-';
        }
        console_writeln(g_str_tmp);
    }
}

/* ---- Main evaluator ---- */
static lang_val_t eval_node(int16_t idx);

static lang_val_t eval_node(int16_t idx) {
    lang_node_t *nd;
    lang_val_t lv, rv;

    if (g_lang_error || g_return_flag) return make_nil();
    if (idx < 0) return make_nil();

    nd = &g_nodes[idx];

    switch ((node_t)nd->type) {

    case N_INT:  return make_int(nd->ival);
    case N_STR:  return make_str(nd->sval);
    case N_BOOL: return make_bool(nd->ival);

    case N_IDENT: {
        var_entry_t *v = var_find(nd->sval);
        if (!v) { lang_error(0, "undefined variable"); return make_nil(); }
        return v->val;
    }

    case N_NEG: {
        lv = eval_node(nd->left);
        if (lv.type != VAL_INT) { lang_error(0, "'-' requires integer"); return make_nil(); }
        return make_int(-lv.i);
    }

    case N_NOT: {
        lv = eval_node(nd->left);
        return make_bool(!lv.i);
    }

    case N_BINOP: {
        tok_t op = (tok_t)nd->ival;
        lv = eval_node(nd->left);
        if (g_lang_error) return make_nil();
        rv = eval_node(nd->right);
        if (g_lang_error) return make_nil();

        /* Logical short-circuit already happened if we used && / ||:
         * We can't short-circuit here because both sides were already evaluated.
         * That's acceptable for a simple interpreter. */

        if (op == TOK_AND) return make_bool(lv.i && rv.i);
        if (op == TOK_OR)  return make_bool(lv.i || rv.i);

        /* String concatenation: if either side is a string, concat */
        if (op == TOK_PLUS &&
            (lv.type == VAL_STR || rv.type == VAL_STR)) {
            uint16_t sa = val_to_spool(lv);
            uint16_t sb = val_to_spool(rv);
            return make_str(str_concat(sa, sb));
        }

        /* Integer arithmetic */
        if (op == TOK_PLUS)    return make_int(lv.i + rv.i);
        if (op == TOK_MINUS)   return make_int(lv.i - rv.i);
        if (op == TOK_STAR)    return make_int(lv.i * rv.i);
        if (op == TOK_SLASH) {
            if (rv.i == 0) { lang_error(0, "division by zero"); return make_nil(); }
            return make_int(lv.i / rv.i);
        }
        if (op == TOK_PERCENT) {
            if (rv.i == 0) { lang_error(0, "division by zero"); return make_nil(); }
            return make_int(lv.i % rv.i);
        }

        /* Comparisons */
        if (op == TOK_EQ)  return make_bool(lv.i == rv.i);
        if (op == TOK_NEQ) return make_bool(lv.i != rv.i);
        if (op == TOK_LT)  return make_bool(lv.i <  rv.i);
        if (op == TOK_GT)  return make_bool(lv.i >  rv.i);
        if (op == TOK_LEQ) return make_bool(lv.i <= rv.i);
        if (op == TOK_GEQ) return make_bool(lv.i >= rv.i);

        return make_nil();
    }

    case N_LET: {
        lv = eval_node(nd->left);
        if (!g_lang_error) var_let(nd->sval, lv);
        return make_nil();
    }

    case N_ASSIGN: {
        lv = eval_node(nd->left);
        if (!g_lang_error) var_assign(nd->sval, lv);
        return make_nil();
    }

    case N_PRINT: {
        lv = eval_node(nd->left);
        if (!g_lang_error) print_val(lv);
        return make_nil();
    }

    case N_SYS: {
        char cmd_buf[256];
        char *argv_arr[16];
        int argc;
        lv = eval_node(nd->left);
        if (g_lang_error) return make_nil();
        if (lv.type != VAL_STR) {
            lang_error(0, "sys requires a string");
            return make_nil();
        }
        str_copy(cmd_buf, sp_get(lv.s), sizeof(cmd_buf));
        argc = split_args(cmd_buf, argv_arr, 16);
        if (argc > 0) commands_dispatch(argc, argv_arr);
        return make_nil();
    }

    case N_INPUT: {
        char in_buf[256];
        in_buf[0] = '\0';
        input_readline(in_buf, sizeof(in_buf));
        return make_str(sp_add(in_buf, (int)str_len(in_buf)));
    }

    case N_CALL: {
        fn_entry_t *fn = fn_find(nd->sval);
        int16_t arg_node;
        int16_t param_node;
        int save_depth;
        lang_val_t ret;

        if (!fn) { lang_error(0, "undefined function"); return make_nil(); }
        if (g_call_depth >= MAX_CALL_DEPTH) { lang_error(0, "call stack overflow"); return make_nil(); }

        /* Evaluate arguments before entering new scope */
        arg_node   = nd->left;
        param_node = fn->params;

        g_call_depth++;
        save_depth = g_scope_depth;
        scope_enter();

        while (param_node >= 0 && arg_node >= 0 && !g_lang_error) {
            lv = eval_node(g_nodes[arg_node].left);
            if (g_lang_error) break;
            var_let(g_nodes[param_node].sval, lv);
            param_node = g_nodes[param_node].right;
            arg_node   = g_nodes[arg_node].right;
        }

        if (!g_lang_error) lang_eval(fn->body);

        /* Collect return value */
        ret = g_return_val;
        if (!g_return_flag) ret = make_nil();
        g_return_flag = 0;
        g_return_val  = make_nil();

        scope_leave();
        g_scope_depth = save_depth;
        g_call_depth--;

        return ret;
    }

    case N_IF: {
        lv = eval_node(nd->left);
        if (g_lang_error) return make_nil();
        if (lv.i) {
            lang_eval(nd->right);
        } else if (nd->extra >= 0) {
            lang_eval(nd->extra);
        }
        return make_nil();
    }

    case N_WHILE: {
        int limit = 1000000;
        while (!g_lang_error && !g_return_flag && limit-- > 0) {
            lv = eval_node(nd->left);
            if (g_lang_error) break;
            if (!lv.i) break;
            lang_eval(nd->right);
        }
        return make_nil();
    }

    case N_FNDEF:
        fn_define(nd->sval, nd->left, nd->right);
        return make_nil();

    case N_RETURN:
        if (nd->left >= 0) g_return_val = eval_node(nd->left);
        else               g_return_val = make_nil();
        g_return_flag = 1;
        return g_return_val;

    case N_BLOCK:
    case N_ARG:
    case N_PARAM:
    default:
        return make_nil();
    }
}

/* ---- Block evaluator (walks the N_BLOCK linked list) ---- */
void lang_eval(int16_t block) {
    int16_t cur = block;
    while (cur >= 0 && !g_lang_error && !g_return_flag) {
        lang_node_t *bn = &g_nodes[cur];
        if ((node_t)bn->type != N_BLOCK) break;
        eval_node(bn->left);
        cur = bn->right;
    }
}

/* Reset evaluator globals before each run; called from lang.c. */
void lang_eval_reset(void) {
    var_table_clear();
    g_fn_count    = 0;
    g_return_flag = 0;
    g_return_val  = make_nil();
    g_call_depth  = 0;
}
