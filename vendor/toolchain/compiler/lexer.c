#include "lexer.h"
#include <errno.h>
#include <math.h>

static bool is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool is_ident(char c) {
    return is_ident_start(c) || isdigit((unsigned char)c);
}

static Token make_token(Lexer *lx, TokenKind kind, ForgeStr lexeme) {
  Token t = { kind, lexeme, 0, 0.0, lx->line, lx->col };
  return t;
}

static void skip_ws(Lexer *lx) {
    while (lx->pos < lx->len) {
        char c = lx->src[lx->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lx->pos++;
            lx->col++;
        } else if (c == '\n') {
            lx->pos++;
            lx->line++;
            lx->col = 1;
        } else if (c == '/' && lx->pos + 1 < lx->len && lx->src[lx->pos + 1] == '/') {
            lx->pos += 2;
            lx->col += 2;
            while (lx->pos < lx->len && lx->src[lx->pos] != '\n') {
                lx->pos++;
                lx->col++;
            }
        } else {
            break;
        }
    }
}

static TokenKind keyword_kind(ForgeStr s) {
    struct { const char *kw; TokenKind kind; } kws[] = {
        {"process", TOK_KW_PROCESS}, {"coroutine", TOK_KW_COROUTINE},
        {"supervisor", TOK_KW_SUPERVISOR}, {"spawn", TOK_KW_SPAWN},
        {"send", TOK_KW_SEND}, {"recv", TOK_KW_RECV}, {"yield", TOK_KW_YIELD},
        {"on", TOK_KW_ON}, {"receive", TOK_KW_RECEIVE},
        {"let", TOK_KW_LET}, {"mut", TOK_KW_MUT},
        {"fn", TOK_KW_FN}, {"return", TOK_KW_RETURN},
        {"if", TOK_KW_IF}, {"else", TOK_KW_ELSE}, {"while", TOK_KW_WHILE},
        {"for", TOK_KW_FOR}, {"break", TOK_KW_BREAK}, {"continue", TOK_KW_CONTINUE},
        {"struct", TOK_KW_STRUCT}, {"enum", TOK_KW_ENUM},
        {"native", TOK_KW_NATIVE}, {"extern", TOK_KW_EXTERN},
        {"true", TOK_KW_TRUE}, {"false", TOK_KW_FALSE},
        {"int", TOK_KW_INT}, {"float", TOK_KW_FLOAT},
        {"bool", TOK_KW_BOOL}, {"string", TOK_KW_STRING}, {"void", TOK_KW_VOID},
        {"ptr", TOK_KW_PTR},
        {"restart", TOK_KW_RESTART},
        {"import", TOK_KW_IMPORT},
        {"library", TOK_KW_LIBRARY},
        {"export", TOK_KW_EXPORT},
        {"own", TOK_KW_OWN}, {"move", TOK_KW_MOVE}, {"await", TOK_KW_AWAIT},
        {"match", TOK_KW_MATCH}, {"const", TOK_KW_CONST},
    };
    for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++) {
        ForgeStr kw = forge_str(kws[i].kw);
        if (forge_str_eq(s, kw)) return kws[i].kind;
    }
    return TOK_IDENT;
}

void lexer_init(Lexer *lx, const char *src, size_t len) {
    lx->src = src;
    lx->pos = 0;
    lx->len = len;
    lx->line = 1;
    lx->col = 1;
    lx->has_current = false;
}

static Token read_number(Lexer *lx) {
    size_t start = lx->pos;
    int col = lx->col;
    bool is_float = false;
    while (lx->pos < lx->len && isdigit((unsigned char)lx->src[lx->pos])) {
        lx->pos++;
        lx->col++;
    }
    if (lx->pos < lx->len && lx->src[lx->pos] == '.') {
        is_float = true;
        lx->pos++;
        lx->col++;
        while (lx->pos < lx->len && isdigit((unsigned char)lx->src[lx->pos])) {
            lx->pos++;
            lx->col++;
        }
    }
    ForgeStr lex = { (char *)lx->src + start, lx->pos - start };
    char *buf = forge_strdup(lex);
    if (!buf) forge_die("out of memory");
    Token t = make_token(lx, is_float ? TOK_FLOAT : TOK_INT, lex);
    t.line = lx->line;
    t.col = col;
    errno = 0;
    if (is_float) t.float_val = strtod(buf, NULL);
    else t.int_val = strtoll(buf, NULL, 10);
    free(buf);
    if (errno == ERANGE || (is_float && !isfinite(t.float_val))) {
        fprintf(stderr, "forge:%d:%d: numeric literal out of range\n", t.line, t.col);
        exit(1);
    }
    return t;
}

static Token read_string(Lexer *lx) {
    int line = lx->line, col = lx->col;
    lx->pos++;
    lx->col++;
    size_t start = lx->pos;
    while (lx->pos < lx->len && lx->src[lx->pos] != '"') {
        if (lx->src[lx->pos] == '\\' && lx->pos + 1 < lx->len) {
            lx->pos += 2;
            lx->col += 2;
            continue;
        }
        if (lx->src[lx->pos] == '\n') {
            lx->line++;
            lx->col = 1;
        } else {
            lx->col++;
        }
        lx->pos++;
    }
    if (lx->pos >= lx->len) forge_die("unterminated string");
    ForgeStr lex = { (char *)lx->src + start, lx->pos - start };
    lx->pos++;
    lx->col++;
    Token t = make_token(lx, TOK_STRING, lex);
    t.line = line;
    t.col = col;
    return t;
}

static Token read_ident(Lexer *lx) {
    size_t start = lx->pos;
    int line = lx->line, col = lx->col;
    while (lx->pos < lx->len && is_ident(lx->src[lx->pos])) {
        lx->pos++;
        lx->col++;
    }
    ForgeStr lex = { (char *)lx->src + start, lx->pos - start };
    TokenKind kind = keyword_kind(lex);
    Token t = make_token(lx, kind, lex);
    t.line = line;
    t.col = col;
    return t;
}

Token lexer_next(Lexer *lx) {
    if (lx->has_current) {
        lx->has_current = false;
        return lx->current;
    }
    skip_ws(lx);
    if (lx->pos >= lx->len) return make_token(lx, TOK_EOF, forge_str(""));

    char c = lx->src[lx->pos];
    int line = lx->line, col = lx->col;

    if (isdigit((unsigned char)c)) return read_number(lx);
    if (is_ident_start(c)) return read_ident(lx);
    if (c == '"') return read_string(lx);

    lx->pos++;
    lx->col++;

    switch (c) {
    case '(': return make_token(lx, TOK_LPAREN, forge_str("("));
    case ')': return make_token(lx, TOK_RPAREN, forge_str(")"));
    case '{': return make_token(lx, TOK_LBRACE, forge_str("{"));
    case '}': return make_token(lx, TOK_RBRACE, forge_str("}"));
    case '[': return make_token(lx, TOK_LBRACKET, forge_str("["));
    case ']': return make_token(lx, TOK_RBRACKET, forge_str("]"));
    case ',': return make_token(lx, TOK_COMMA, forge_str(","));
    case ':': return make_token(lx, TOK_COLON, forge_str(":"));
    case ';': return make_token(lx, TOK_SEMI, forge_str(";"));
    case '.': return make_token(lx, TOK_DOT, forge_str("."));
    case '+':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_PLUSEQ, forge_str("+="));
        }
        return make_token(lx, TOK_PLUS, forge_str("+"));
    case '-':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_MINUSEQ, forge_str("-="));
        }
        return make_token(lx, TOK_MINUS, forge_str("-"));
    case '*':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_STAREQ, forge_str("*="));
        }
        return make_token(lx, TOK_STAR, forge_str("*"));
    case '/':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_SLASHEQ, forge_str("/="));
        }
        return make_token(lx, TOK_SLASH, forge_str("/"));
    case '%':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_PERCENTEQ, forge_str("%="));
        }
        return make_token(lx, TOK_PERCENT, forge_str("%"));
    case '!':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_NE, forge_str("!="));
        }
        return make_token(lx, TOK_BANG, forge_str("!"));
    case '=':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_EQEQ, forge_str("=="));
        }
        if (lx->pos < lx->len && lx->src[lx->pos] == '>') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_FAT_ARROW, forge_str("=>"));
        }
        return make_token(lx, TOK_EQ, forge_str("="));
    case '<':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_LE, forge_str("<="));
        }
        return make_token(lx, TOK_LT, forge_str("<"));
    case '>':
        if (lx->pos < lx->len && lx->src[lx->pos] == '=') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_GE, forge_str(">="));
        }
        return make_token(lx, TOK_GT, forge_str(">"));
    case '&':
        if (lx->pos < lx->len && lx->src[lx->pos] == '&') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_ANDAND, forge_str("&&"));
        }
        break;
    case '|':
        if (lx->pos < lx->len && lx->src[lx->pos] == '|') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_OROR, forge_str("||"));
        }
        if (lx->pos < lx->len && lx->src[lx->pos] == '>') {
            lx->pos++; lx->col++;
            return make_token(lx, TOK_PIPE, forge_str("|>"));
        }
        break;
    default:
        break;
    }
    fprintf(stderr, "forge: unexpected character '%c' at %d:%d\n", c, line, col);
    exit(1);
}

Token lexer_peek(Lexer *lx) {
    if (!lx->has_current) {
        lx->current = lexer_next(lx);
        lx->has_current = true;
    }
    return lx->current;
}

bool lexer_match(Lexer *lx, TokenKind kind) {
    Token t = lexer_peek(lx);
    if (t.kind == kind) {
        lexer_next(lx);
        return true;
    }
    return false;
}
