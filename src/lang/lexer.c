#include "lang/lang_priv.h"
#include "lib/string.h"

/* ---- Keyword table ---- */
static const struct { const char *kw; tok_t type; } g_keywords[] = {
    {"let",    TOK_LET},
    {"if",     TOK_IF},
    {"else",   TOK_ELSE},
    {"while",  TOK_WHILE},
    {"fn",     TOK_FN},
    {"return", TOK_RETURN},
    {"print",  TOK_PRINT},
    {"sys",    TOK_SYS},
    {"input",  TOK_INPUT},
    {"true",   TOK_TRUE},
    {"false",  TOK_FALSE},
    {0, (tok_t)0},
};

static tok_t check_keyword(const char *s, int len) {
    int i;
    for (i = 0; g_keywords[i].kw; i++) {
        int klen = (int)str_len(g_keywords[i].kw);
        if (klen == len && str_ncmp(s, g_keywords[i].kw, (size_t)len) == 0)
            return g_keywords[i].type;
    }
    return TOK_IDENT;
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void add_tok(tok_t type, int32_t ival, uint16_t sval, uint16_t line) {
    if (g_tok_count >= LANG_TOK_MAX) { g_lang_error = 1; return; }
    g_toks[g_tok_count].type = type;
    g_toks[g_tok_count].ival = ival;
    g_toks[g_tok_count].sval = sval;
    g_toks[g_tok_count].line = line;
    g_tok_count++;
}

void lang_lex(const char *src, int len) {
    int i = 0;
    uint16_t line = 1;

    while (i < len && !g_lang_error) {
        char c = src[i];

        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        /* Comments: // to end of line */
        if (c == '/' && i + 1 < len && src[i + 1] == '/') {
            while (i < len && src[i] != '\n') i++;
            continue;
        }

        /* Newline — statement separator */
        if (c == '\n') {
            /* Suppress consecutive newlines */
            if (g_tok_count > 0 &&
                g_toks[g_tok_count - 1].type != TOK_NEWLINE &&
                g_toks[g_tok_count - 1].type != TOK_LBRACE)
                add_tok(TOK_NEWLINE, 0, 0, line);
            line++;
            i++;
            continue;
        }

        /* String literal */
        if (c == '"') {
            int start = i + 1;
            i++;
            while (i < len && src[i] != '"' && src[i] != '\n') i++;
            uint16_t sv = sp_add(src + start, i - start);
            if (i < len && src[i] == '"') i++;
            add_tok(TOK_STR, 0, sv, line);
            continue;
        }

        /* Integer literal */
        if (c >= '0' && c <= '9') {
            int32_t v = 0;
            while (i < len && src[i] >= '0' && src[i] <= '9') {
                v = v * 10 + (int32_t)(src[i] - '0');
                i++;
            }
            add_tok(TOK_INT, v, 0, line);
            continue;
        }

        /* Identifier or keyword */
        if (is_ident_start(c)) {
            int start = i;
            while (i < len && is_ident_char(src[i])) i++;
            tok_t kw = check_keyword(src + start, i - start);
            uint16_t sv = 0;
            if (kw == TOK_IDENT) sv = sp_add(src + start, i - start);
            add_tok(kw, 0, sv, line);
            continue;
        }

        /* Two-char operators */
        if (c == '=' && i+1 < len && src[i+1] == '=') { add_tok(TOK_EQ,  0,0,line); i+=2; continue; }
        if (c == '!' && i+1 < len && src[i+1] == '=') { add_tok(TOK_NEQ, 0,0,line); i+=2; continue; }
        if (c == '<' && i+1 < len && src[i+1] == '=') { add_tok(TOK_LEQ, 0,0,line); i+=2; continue; }
        if (c == '>' && i+1 < len && src[i+1] == '=') { add_tok(TOK_GEQ, 0,0,line); i+=2; continue; }
        if (c == '&' && i+1 < len && src[i+1] == '&') { add_tok(TOK_AND, 0,0,line); i+=2; continue; }
        if (c == '|' && i+1 < len && src[i+1] == '|') { add_tok(TOK_OR,  0,0,line); i+=2; continue; }

        /* Single-char tokens */
        switch (c) {
            case '=': add_tok(TOK_ASSIGN,  0,0,line); break;
            case '+': add_tok(TOK_PLUS,    0,0,line); break;
            case '-': add_tok(TOK_MINUS,   0,0,line); break;
            case '*': add_tok(TOK_STAR,    0,0,line); break;
            case '/': add_tok(TOK_SLASH,   0,0,line); break;
            case '%': add_tok(TOK_PERCENT, 0,0,line); break;
            case '<': add_tok(TOK_LT,      0,0,line); break;
            case '>': add_tok(TOK_GT,      0,0,line); break;
            case '!': add_tok(TOK_NOT,     0,0,line); break;
            case '(': add_tok(TOK_LPAREN,  0,0,line); break;
            case ')': add_tok(TOK_RPAREN,  0,0,line); break;
            case '{': add_tok(TOK_LBRACE,  0,0,line); break;
            case '}': add_tok(TOK_RBRACE,  0,0,line); break;
            case ',': add_tok(TOK_COMMA,   0,0,line); break;
            default: break; /* ignore unknown chars */
        }
        i++;
    }

    add_tok(TOK_EOF, 0, 0, line);
}
