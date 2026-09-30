#ifndef FORGE_LEXER_H
#define FORGE_LEXER_H

#include "common.h"

typedef enum {
    TOK_EOF,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_COLON, TOK_SEMI,
    TOK_DOT,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ, TOK_PERCENTEQ,
    TOK_EQ, TOK_EQEQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_ANDAND, TOK_OROR,
    TOK_PIPE,
    TOK_FAT_ARROW,
    TOK_BANG,
    TOK_KW_PROCESS, TOK_KW_COROUTINE, TOK_KW_SUPERVISOR,
    TOK_KW_SPAWN, TOK_KW_SEND, TOK_KW_RECV, TOK_KW_YIELD,
    TOK_KW_ON, TOK_KW_RECEIVE, TOK_KW_LET, TOK_KW_MUT,
    TOK_KW_FN, TOK_KW_RETURN, TOK_KW_IF, TOK_KW_ELSE, TOK_KW_WHILE,
    TOK_KW_FOR, TOK_KW_BREAK, TOK_KW_CONTINUE,
    TOK_KW_STRUCT, TOK_KW_ENUM, TOK_KW_NATIVE, TOK_KW_EXTERN,
    TOK_KW_TRUE, TOK_KW_FALSE,
    TOK_KW_INT, TOK_KW_FLOAT, TOK_KW_BOOL, TOK_KW_STRING, TOK_KW_VOID, TOK_KW_PTR,
    TOK_KW_RESTART,
    TOK_KW_IMPORT,
    TOK_KW_LIBRARY,
    TOK_KW_EXPORT,
    TOK_KW_OWN, TOK_KW_MOVE, TOK_KW_AWAIT,
    TOK_KW_MATCH, TOK_KW_CONST
} TokenKind;

typedef struct {
    TokenKind kind;
    ForgeStr lexeme;
    int64_t int_val;
    double float_val;
    int line;
    int col;
} Token;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;
    int col;
    Token current;
    bool has_current;
} Lexer;

void lexer_init(Lexer *lx, const char *src, size_t len);
Token lexer_next(Lexer *lx);
Token lexer_peek(Lexer *lx);
bool lexer_match(Lexer *lx, TokenKind kind);

#endif
