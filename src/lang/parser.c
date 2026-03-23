#include "lang/lang_priv.h"
#include "lib/string.h"

static int g_pos = 0;

static lang_token_t *cur(void)          { return &g_toks[g_pos]; }
static void          advance(void)      { if (g_toks[g_pos].type != TOK_EOF) g_pos++; }
static int           check(tok_t t)     { return cur()->type == t; }
static int           match(tok_t t)     { if (check(t)) { advance(); return 1; } return 0; }

static void expect(tok_t t, const char *msg) {
    if (!match(t)) lang_error(cur()->line, msg);
}

static void skip_newlines(void) {
    while (check(TOK_NEWLINE)) advance();
}

/* Forward declarations */
static int16_t parse_expr(void);
static int16_t parse_block(void);
static int16_t parse_stmt(void);
static int16_t parse_stmt_list(void);

/* ---- Expression parsing (recursive descent by precedence) ---- */

static int16_t parse_call_args(uint16_t name) {
    /* '(' already consumed by caller; parses args and ')' */
    int16_t n = node_alloc(N_CALL);
    int16_t first = -1, last = -1;
    if (n < 0) return -1;
    g_nodes[n].sval = name;

    skip_newlines();
    if (!check(TOK_RPAREN)) {
        do {
            int16_t expr;
            int16_t an;
            skip_newlines();
            expr = parse_expr();
            if (g_lang_error) return -1;
            an = node_alloc(N_ARG);
            if (an < 0) return -1;
            g_nodes[an].left  = expr;
            g_nodes[an].right = -1;
            if (first < 0) first = an;
            if (last >= 0) g_nodes[last].right = an;
            last = an;
            skip_newlines();
        } while (match(TOK_COMMA) && !g_lang_error);
    }
    expect(TOK_RPAREN, "expected ')'");
    g_nodes[n].left = first;
    return n;
}

static int16_t parse_primary(void) {
    lang_token_t *t = cur();
    int16_t n;

    if (g_lang_error) return -1;

    if (t->type == TOK_INT) {
        n = node_alloc(N_INT);
        if (n < 0) return -1;
        g_nodes[n].ival = t->ival;
        advance();
        return n;
    }
    if (t->type == TOK_STR) {
        n = node_alloc(N_STR);
        if (n < 0) return -1;
        g_nodes[n].sval = t->sval;
        advance();
        return n;
    }
    if (t->type == TOK_TRUE) {
        n = node_alloc(N_BOOL);
        if (n < 0) return -1;
        g_nodes[n].ival = 1;
        advance();
        return n;
    }
    if (t->type == TOK_FALSE) {
        n = node_alloc(N_BOOL);
        if (n < 0) return -1;
        g_nodes[n].ival = 0;
        advance();
        return n;
    }
    if (t->type == TOK_INPUT) {
        advance();
        expect(TOK_LPAREN, "expected '(' after input");
        expect(TOK_RPAREN, "expected ')'");
        n = node_alloc(N_INPUT);
        return n;
    }
    if (t->type == TOK_IDENT) {
        uint16_t name = t->sval;
        advance();
        if (check(TOK_LPAREN)) {
            advance();
            return parse_call_args(name);
        }
        n = node_alloc(N_IDENT);
        if (n < 0) return -1;
        g_nodes[n].sval = name;
        return n;
    }
    if (t->type == TOK_LPAREN) {
        advance();
        n = parse_expr();
        expect(TOK_RPAREN, "expected ')'");
        return n;
    }

    lang_error(t->line, "unexpected token in expression");
    return -1;
}

static int16_t parse_unary(void) {
    if (g_lang_error) return -1;
    if (check(TOK_MINUS)) {
        int16_t n, op;
        advance();
        op = parse_unary();
        if (g_lang_error) return -1;
        n = node_alloc(N_NEG);
        if (n < 0) return -1;
        g_nodes[n].left = op;
        return n;
    }
    if (check(TOK_NOT)) {
        int16_t n, op;
        advance();
        op = parse_unary();
        if (g_lang_error) return -1;
        n = node_alloc(N_NOT);
        if (n < 0) return -1;
        g_nodes[n].left = op;
        return n;
    }
    return parse_primary();
}

static int16_t parse_mul(void) {
    int16_t left = parse_unary();
    while (!g_lang_error &&
           (check(TOK_STAR) || check(TOK_SLASH) || check(TOK_PERCENT))) {
        int16_t n, right;
        tok_t op = cur()->type;
        advance();
        right = parse_unary();
        if (g_lang_error) break;
        n = node_alloc(N_BINOP);
        if (n < 0) break;
        g_nodes[n].left  = left;
        g_nodes[n].right = right;
        g_nodes[n].ival  = (int32_t)op;
        left = n;
    }
    return left;
}

static int16_t parse_add(void) {
    int16_t left = parse_mul();
    while (!g_lang_error && (check(TOK_PLUS) || check(TOK_MINUS))) {
        int16_t n, right;
        tok_t op = cur()->type;
        advance();
        right = parse_mul();
        if (g_lang_error) break;
        n = node_alloc(N_BINOP);
        if (n < 0) break;
        g_nodes[n].left  = left;
        g_nodes[n].right = right;
        g_nodes[n].ival  = (int32_t)op;
        left = n;
    }
    return left;
}

static int16_t parse_cmp(void) {
    int16_t left = parse_add();
    if (!g_lang_error &&
        (check(TOK_EQ)  || check(TOK_NEQ) || check(TOK_LT) ||
         check(TOK_GT)  || check(TOK_LEQ) || check(TOK_GEQ))) {
        int16_t n, right;
        tok_t op = cur()->type;
        advance();
        right = parse_add();
        if (!g_lang_error) {
            n = node_alloc(N_BINOP);
            if (n >= 0) {
                g_nodes[n].left  = left;
                g_nodes[n].right = right;
                g_nodes[n].ival  = (int32_t)op;
                left = n;
            }
        }
    }
    return left;
}

static int16_t parse_and(void) {
    int16_t left = parse_cmp();
    while (!g_lang_error && check(TOK_AND)) {
        int16_t n, right;
        advance();
        right = parse_cmp();
        if (g_lang_error) break;
        n = node_alloc(N_BINOP);
        if (n < 0) break;
        g_nodes[n].left  = left;
        g_nodes[n].right = right;
        g_nodes[n].ival  = (int32_t)TOK_AND;
        left = n;
    }
    return left;
}

static int16_t parse_or(void) {
    int16_t left = parse_and();
    while (!g_lang_error && check(TOK_OR)) {
        int16_t n, right;
        advance();
        right = parse_and();
        if (g_lang_error) break;
        n = node_alloc(N_BINOP);
        if (n < 0) break;
        g_nodes[n].left  = left;
        g_nodes[n].right = right;
        g_nodes[n].ival  = (int32_t)TOK_OR;
        left = n;
    }
    return left;
}

static int16_t parse_expr(void) {
    return parse_or();
}

/* ---- Block parsing: '{' stmts '}' ---- */
static int16_t parse_block(void) {
    int16_t body;
    expect(TOK_LBRACE, "expected '{'");
    skip_newlines();
    body = -1;
    if (!check(TOK_RBRACE) && !check(TOK_EOF)) {
        body = parse_stmt_list();
    }
    skip_newlines();
    expect(TOK_RBRACE, "expected '}'");
    return body;
}

/* ---- Statement parsing ---- */
static int16_t parse_stmt(void) {
    lang_token_t *t;
    int16_t n;

    if (g_lang_error) return -1;
    skip_newlines();
    t = cur();
    if (t->type == TOK_EOF || t->type == TOK_RBRACE) return -1;

    /* let <ident> = <expr> */
    if (t->type == TOK_LET) {
        int16_t val;
        uint16_t name;
        advance();
        if (!check(TOK_IDENT)) {
            lang_error(cur()->line, "expected identifier after 'let'");
            return -1;
        }
        name = cur()->sval;
        advance();
        expect(TOK_ASSIGN, "expected '='");
        val = parse_expr();
        if (g_lang_error) return -1;
        n = node_alloc(N_LET);
        if (n < 0) return -1;
        g_nodes[n].sval = name;
        g_nodes[n].left = val;
        return n;
    }

    /* print <expr> */
    if (t->type == TOK_PRINT) {
        int16_t val;
        advance();
        val = parse_expr();
        if (g_lang_error) return -1;
        n = node_alloc(N_PRINT);
        if (n < 0) return -1;
        g_nodes[n].left = val;
        return n;
    }

    /* sys <expr> */
    if (t->type == TOK_SYS) {
        int16_t val;
        advance();
        val = parse_expr();
        if (g_lang_error) return -1;
        n = node_alloc(N_SYS);
        if (n < 0) return -1;
        g_nodes[n].left = val;
        return n;
    }

    /* if <expr> { ... } [else { ... }] */
    if (t->type == TOK_IF) {
        int16_t cond, then_b, else_b;
        int save_pos;
        advance();
        cond   = parse_expr();
        if (g_lang_error) return -1;
        then_b = parse_block();
        if (g_lang_error) return -1;
        else_b = -1;
        save_pos = g_pos;
        skip_newlines();
        if (check(TOK_ELSE)) {
            advance();
            skip_newlines();
            else_b = parse_block();
        } else {
            g_pos = save_pos;
        }
        n = node_alloc(N_IF);
        if (n < 0) return -1;
        g_nodes[n].left  = cond;
        g_nodes[n].right = then_b;
        g_nodes[n].extra = else_b;
        return n;
    }

    /* while <expr> { ... } */
    if (t->type == TOK_WHILE) {
        int16_t cond, body;
        advance();
        cond = parse_expr();
        if (g_lang_error) return -1;
        body = parse_block();
        if (g_lang_error) return -1;
        n = node_alloc(N_WHILE);
        if (n < 0) return -1;
        g_nodes[n].left  = cond;
        g_nodes[n].right = body;
        return n;
    }

    /* fn <ident>(<params>) { ... } */
    if (t->type == TOK_FN) {
        uint16_t name;
        int16_t first_param = -1, last_param = -1, body;
        advance();
        if (!check(TOK_IDENT)) {
            lang_error(cur()->line, "expected function name");
            return -1;
        }
        name = cur()->sval;
        advance();
        expect(TOK_LPAREN, "expected '('");
        while (!check(TOK_RPAREN) && !check(TOK_EOF) && !g_lang_error) {
            int16_t pn;
            if (first_param >= 0) expect(TOK_COMMA, "expected ','");
            if (!check(TOK_IDENT)) {
                lang_error(cur()->line, "expected parameter name");
                return -1;
            }
            pn = node_alloc(N_PARAM);
            if (pn < 0) return -1;
            g_nodes[pn].sval  = cur()->sval;
            g_nodes[pn].right = -1;
            advance();
            if (first_param < 0) first_param = pn;
            if (last_param  >= 0) g_nodes[last_param].right = pn;
            last_param = pn;
        }
        expect(TOK_RPAREN, "expected ')'");
        if (g_lang_error) return -1;
        body = parse_block();
        if (g_lang_error) return -1;
        n = node_alloc(N_FNDEF);
        if (n < 0) return -1;
        g_nodes[n].sval  = name;
        g_nodes[n].left  = first_param;
        g_nodes[n].right = body;
        return n;
    }

    /* return [<expr>] */
    if (t->type == TOK_RETURN) {
        int16_t val = -1;
        advance();
        if (!check(TOK_NEWLINE) && !check(TOK_EOF) && !check(TOK_RBRACE))
            val = parse_expr();
        n = node_alloc(N_RETURN);
        if (n < 0) return -1;
        g_nodes[n].left = val;
        return n;
    }

    /* <ident> = <expr>  or  <ident>(<args>) */
    if (t->type == TOK_IDENT) {
        uint16_t name = t->sval;
        advance();
        if (check(TOK_ASSIGN)) {
            int16_t val;
            advance();
            val = parse_expr();
            if (g_lang_error) return -1;
            n = node_alloc(N_ASSIGN);
            if (n < 0) return -1;
            g_nodes[n].sval = name;
            g_nodes[n].left = val;
            return n;
        }
        if (check(TOK_LPAREN)) {
            advance();
            return parse_call_args(name);
        }
        lang_error(t->line, "expected '=' or '(' after identifier");
        return -1;
    }

    lang_error(t->line, "unexpected statement");
    return -1;
}

/* ---- Statement list ---- */
static int16_t parse_stmt_list(void) {
    int16_t first = -1, last = -1;
    skip_newlines();
    while (!g_lang_error && !check(TOK_EOF) && !check(TOK_RBRACE)) {
        int16_t s, bn;
        s = parse_stmt();
        if (g_lang_error || s < 0) break;
        bn = node_alloc(N_BLOCK);
        if (bn < 0) break;
        g_nodes[bn].left  = s;
        g_nodes[bn].right = -1;
        if (first < 0) first = bn;
        if (last  >= 0) g_nodes[last].right = bn;
        last = bn;
        skip_newlines();
    }
    return first;
}

/* ---- Entry point ---- */
int16_t lang_parse(void) {
    g_pos = 0;
    return parse_stmt_list();
}
