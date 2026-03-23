#pragma once

/* Internal types and shared state for the Ax interpreter.
 * Included by lang.c, lexer.c, parser.c, and eval.c. */

#include <stdint.h>

/* ---- String pool ---- */
#define LANG_STR_POOL_SIZE 16384

extern char     g_sp[];
extern uint16_t g_sp_end;

uint16_t    sp_add(const char *s, int len);
const char *sp_get(uint16_t off);

/* ---- Error state ---- */
extern int g_lang_error;

void lang_error(int line, const char *msg);

/* ---- Tokens ---- */
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT, TOK_INT, TOK_STR,
    TOK_TRUE, TOK_FALSE,
    TOK_LET, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FN, TOK_RETURN,
    TOK_PRINT, TOK_SYS, TOK_INPUT,
    TOK_ASSIGN,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_COMMA,
    TOK_NEWLINE,
} tok_t;

typedef struct {
    tok_t    type;
    int32_t  ival;
    uint16_t sval;
    uint16_t line;
} lang_token_t;

#define LANG_TOK_MAX 4096

extern lang_token_t g_toks[];
extern int          g_tok_count;

/* ---- AST node types ---- */
typedef enum {
    N_INT = 0, N_STR, N_BOOL,
    N_IDENT,
    N_BINOP,   /* left, right, ival = tok_t operator */
    N_NEG,     /* unary minus: left */
    N_NOT,     /* logical not: left */
    N_BLOCK,   /* left = stmt, right = next N_BLOCK */
    N_LET,     /* sval = name, left = init expr */
    N_ASSIGN,  /* sval = name, left = value expr */
    N_PRINT,   /* left = expr */
    N_SYS,     /* left = expr (shell command string) */
    N_INPUT,   /* input() — no children */
    N_CALL,    /* sval = fn_name, left = first N_ARG */
    N_ARG,     /* left = expr, right = next N_ARG */
    N_IF,      /* left = cond, right = then, extra = else */
    N_WHILE,   /* left = cond, right = body */
    N_FNDEF,   /* sval = name, left = first N_PARAM, right = body */
    N_PARAM,   /* sval = name, right = next N_PARAM */
    N_RETURN,  /* left = expr (-1 for bare return) */
} node_t;

typedef struct {
    uint8_t  type;
    int16_t  left;
    int16_t  right;
    int16_t  extra;
    int32_t  ival;
    uint16_t sval;
} lang_node_t;

#define LANG_NODE_MAX 2048

extern lang_node_t g_nodes[];
extern int         g_node_count;

int16_t node_alloc(node_t type);

/* ---- Stage function declarations ---- */
void    lang_lex(const char *src, int len);
int16_t lang_parse(void);
void    lang_eval(int16_t block);
void    lang_eval_reset(void);  /* reset evaluator state between runs */
