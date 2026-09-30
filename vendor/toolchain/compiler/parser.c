#include "parser.h"

typedef struct {
    Lexer *lx;
} Parser;

static void parser_error(Parser *p, const char *msg) {
    Token t = lexer_peek(p->lx);
    fprintf(stderr, "forge: parse error at %d:%d: %s\n", t.line, t.col, msg);
    exit(1);
}

static void expect(Parser *p, TokenKind kind) {
    if (!lexer_match(p->lx, kind)) {
        parser_error(p, "unexpected token");
    }
}

static ForgeStr token_str(Token t) {
    return t.lexeme;
}

static ForgeType parse_type(Parser *p) {
    Token t = lexer_peek(p->lx);
    switch (t.kind) {
    case TOK_KW_INT: lexer_next(p->lx); return forge_type_int();
    case TOK_KW_FLOAT: lexer_next(p->lx); return forge_type_float();
    case TOK_KW_BOOL: lexer_next(p->lx); return forge_type_bool();
    case TOK_KW_STRING: lexer_next(p->lx); return forge_type_string();
    case TOK_KW_VOID: lexer_next(p->lx); return forge_type_void();
    case TOK_KW_PTR: lexer_next(p->lx); return forge_type_ptr();
    case TOK_IDENT:
        lexer_next(p->lx);
        return forge_type_struct(token_str(t));
    default:
        parser_error(p, "expected type");
        return forge_type_void();
    }
}

static Expr *parse_expr(Parser *p);
static Expr *parse_primary(Parser *p);
static Expr *parse_unary(Parser *p);
static Expr *parse_mul(Parser *p);
static Expr *parse_add(Parser *p);
static Expr *parse_cmp(Parser *p);
static Expr *parse_and(Parser *p);
static Expr *parse_or(Parser *p);
static Block *parse_block(Parser *p);
static Stmt *parse_stmt(Parser *p);

static Stmt *parse_assign(Parser *p, ForgeStr name) {
    if (lexer_match(p->lx, TOK_EQ)) {
        return stmt_assign(name, parse_expr(p));
    }
    TokenKind k = lexer_peek(p->lx).kind;
    BinOp op;
    switch (k) {
    case TOK_PLUSEQ: op = BIN_ADD; break;
    case TOK_MINUSEQ: op = BIN_SUB; break;
    case TOK_STAREQ: op = BIN_MUL; break;
    case TOK_SLASHEQ: op = BIN_DIV; break;
    case TOK_PERCENTEQ: op = BIN_MOD; break;
    default:
        parser_error(p, "expected assignment operator");
        return NULL;
    }
    lexer_next(p->lx);
    Expr *rhs = parse_expr(p);
    return stmt_assign(name, expr_binary(op, expr_ident(name), rhs));
}

static Expr *parse_postfix(Parser *p, Expr *e) {
    for (;;) {
        if (lexer_match(p->lx, TOK_DOT)) {
            Token field = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            e = expr_field(e, token_str(field));
        } else if (lexer_match(p->lx, TOK_LBRACKET)) {
            Expr *idx = parse_expr(p);
            expect(p, TOK_RBRACKET);
            e = expr_index(e, idx);
        } else {
            break;
        }
    }
    return e;
}

static Expr *parse_primary(Parser *p) {
    Token t = lexer_peek(p->lx);
    switch (t.kind) {
    case TOK_INT:
        lexer_next(p->lx);
        return expr_int(t.int_val);
    case TOK_FLOAT:
        lexer_next(p->lx);
        return expr_float(t.float_val);
    case TOK_KW_TRUE:
        lexer_next(p->lx);
        return expr_bool(true);
    case TOK_KW_FALSE:
        lexer_next(p->lx);
        return expr_bool(false);
    case TOK_STRING:
        lexer_next(p->lx);
        return expr_string(token_str(t));
    case TOK_IDENT: {
        ForgeStr name = token_str(t);
        lexer_next(p->lx);
        if (lexer_match(p->lx, TOK_DOT)) {
            Token fn = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            if (lexer_match(p->lx, TOK_LPAREN)) {
                Expr **args = NULL;
                size_t n = 0, cap = 0;
                if (!lexer_match(p->lx, TOK_RPAREN)) {
                    do {
                        if (n == cap) {
                            cap = cap ? cap * 2 : 4;
                            args = (Expr **)realloc(args, cap * sizeof(Expr *));
                        }
                        args[n++] = parse_expr(p);
                    } while (lexer_match(p->lx, TOK_COMMA));
                    expect(p, TOK_RPAREN);
                }
                return expr_qual_call(name, token_str(fn), args, n);
            }
            return expr_field(expr_ident(name), token_str(fn));
        }
        if (lexer_match(p->lx, TOK_LPAREN)) {
            Expr **args = NULL;
            size_t n = 0, cap = 0;
            if (!lexer_match(p->lx, TOK_RPAREN)) {
                do {
                    if (n == cap) {
                        cap = cap ? cap * 2 : 4;
                        args = (Expr **)realloc(args, cap * sizeof(Expr *));
                    }
                    args[n++] = parse_expr(p);
                } while (lexer_match(p->lx, TOK_COMMA));
                expect(p, TOK_RPAREN);
            }
            return expr_call(name, args, n);
        }
        return expr_ident(name);
    }
    case TOK_KW_RECV:
        lexer_next(p->lx);
        expect(p, TOK_LPAREN);
        expect(p, TOK_RPAREN);
        return expr_recv();
    case TOK_LPAREN: {
        lexer_next(p->lx);
        Expr *e = parse_expr(p);
        expect(p, TOK_RPAREN);
        return e;
    }
    default:
        parser_error(p, "expected expression");
        return expr_int(0);
    }
}

static Expr *parse_unary(Parser *p) {
    if (lexer_match(p->lx, TOK_KW_MOVE)) {
        return parse_postfix(p, expr_move(parse_unary(p)));
    }
    if (lexer_match(p->lx, TOK_MINUS)) {
        return parse_postfix(p, expr_binary(BIN_SUB, expr_int(0), parse_unary(p)));
    }
    if (lexer_match(p->lx, TOK_BANG)) {
        return parse_postfix(p, expr_binary(BIN_EQ, parse_unary(p), expr_bool(false)));
    }
    return parse_postfix(p, parse_primary(p));
}

static Expr *parse_mul(Parser *p) {
    Expr *left = parse_unary(p);
    for (;;) {
        if (lexer_match(p->lx, TOK_STAR)) left = expr_binary(BIN_MUL, left, parse_unary(p));
        else if (lexer_match(p->lx, TOK_SLASH)) left = expr_binary(BIN_DIV, left, parse_unary(p));
        else if (lexer_match(p->lx, TOK_PERCENT)) left = expr_binary(BIN_MOD, left, parse_unary(p));
        else break;
    }
    return left;
}

static Expr *parse_add(Parser *p) {
    Expr *left = parse_mul(p);
    for (;;) {
        if (lexer_match(p->lx, TOK_PLUS)) left = expr_binary(BIN_ADD, left, parse_mul(p));
        else if (lexer_match(p->lx, TOK_MINUS)) left = expr_binary(BIN_SUB, left, parse_mul(p));
        else break;
    }
    return left;
}

static Expr *parse_cmp(Parser *p) {
    Expr *left = parse_add(p);
    for (;;) {
        if (lexer_match(p->lx, TOK_EQEQ)) left = expr_binary(BIN_EQ, left, parse_add(p));
        else if (lexer_match(p->lx, TOK_NE)) left = expr_binary(BIN_NE, left, parse_add(p));
        else if (lexer_match(p->lx, TOK_LT)) left = expr_binary(BIN_LT, left, parse_add(p));
        else if (lexer_match(p->lx, TOK_LE)) left = expr_binary(BIN_LE, left, parse_add(p));
        else if (lexer_match(p->lx, TOK_GT)) left = expr_binary(BIN_GT, left, parse_add(p));
        else if (lexer_match(p->lx, TOK_GE)) left = expr_binary(BIN_GE, left, parse_add(p));
        else break;
    }
    return left;
}

static Expr *parse_and(Parser *p) {
    Expr *left = parse_cmp(p);
    while (lexer_match(p->lx, TOK_ANDAND)) {
        left = expr_binary(BIN_AND, left, parse_cmp(p));
    }
    return left;
}

static Expr *parse_or(Parser *p) {
    Expr *left = parse_and(p);
    while (lexer_match(p->lx, TOK_OROR)) {
        left = expr_binary(BIN_OR, left, parse_and(p));
    }
    return left;
}

static Expr *pipe_rhs_to_call(Expr *left, Expr *right, Parser *p) {
    if (right->kind == EXPR_CALL) {
        size_t n = right->as.call.arg_count + 1;
        Expr **args = (Expr **)calloc(n, sizeof(Expr *));
        if (!args) forge_die("out of memory");
        args[0] = left;
        for (size_t i = 1; i < n; i++) args[i] = right->as.call.args[i - 1];
        free(right->as.call.args);
        right->as.call.args = args;
        right->as.call.arg_count = n;
        return right;
    }
    if (right->kind == EXPR_QUAL_CALL) {
        size_t n = right->as.qual_call.arg_count + 1;
        Expr **args = (Expr **)calloc(n, sizeof(Expr *));
        if (!args) forge_die("out of memory");
        args[0] = left;
        for (size_t i = 1; i < n; i++) args[i] = right->as.qual_call.args[i - 1];
        free(right->as.qual_call.args);
        right->as.qual_call.args = args;
        right->as.qual_call.arg_count = n;
        return right;
    }
    if (right->kind == EXPR_IDENT) {
        return expr_call(right->as.ident, &left, 1);
    }
    parser_error(p, "pipe right-hand side must be a function call");
    return left;
}

static Expr *parse_pipe(Parser *p) {
    Expr *left = parse_or(p);
    while (lexer_match(p->lx, TOK_PIPE)) {
        left = pipe_rhs_to_call(left, parse_or(p), p);
    }
    return left;
}

static Expr *parse_expr(Parser *p) {
    return parse_pipe(p);
}

static Block *parse_block(Parser *p) {
    expect(p, TOK_LBRACE);
    Block *b = (Block *)calloc(1, sizeof(Block));
    while (!lexer_match(p->lx, TOK_RBRACE)) {
        block_append(b, parse_stmt(p));
    }
    return b;
}

static Stmt *parse_stmt(Parser *p) {
    if (lexer_match(p->lx, TOK_KW_BREAK)) {
        expect(p, TOK_SEMI);
        return stmt_break();
    }
    if (lexer_match(p->lx, TOK_KW_CONTINUE)) {
        expect(p, TOK_SEMI);
        return stmt_continue();
    }
    if (lexer_match(p->lx, TOK_KW_OWN)) {
        expect(p, TOK_KW_LET);
        Token name = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        ForgeType ty = forge_type_string();
        if (lexer_match(p->lx, TOK_COLON)) ty = parse_type(p);
        Expr *init = NULL;
        if (lexer_match(p->lx, TOK_EQ)) init = parse_expr(p);
        expect(p, TOK_SEMI);
        return stmt_let(false, true, token_str(name), ty, init);
    }
    if (lexer_match(p->lx, TOK_KW_LET) || lexer_match(p->lx, TOK_KW_MUT)) {
        bool mut = false;
        if (lexer_peek(p->lx).kind == TOK_KW_MUT) {
            mut = true;
            lexer_next(p->lx);
        }
        Token name = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        ForgeType ty = forge_type_int();
        if (lexer_match(p->lx, TOK_COLON)) ty = parse_type(p);
        Expr *init = NULL;
        if (lexer_match(p->lx, TOK_EQ)) init = parse_expr(p);
        expect(p, TOK_SEMI);
        return stmt_let(mut, false, token_str(name), ty, init);
    }
    if (lexer_match(p->lx, TOK_KW_RETURN)) {
        Expr *e = NULL;
        if (lexer_peek(p->lx).kind != TOK_SEMI) e = parse_expr(p);
        expect(p, TOK_SEMI);
        return stmt_return(e);
    }
    if (lexer_match(p->lx, TOK_KW_MATCH)) {
        Expr *scrutinee = parse_expr(p);
        expect(p, TOK_LBRACE);
        MatchArm *arms = NULL, *tail = NULL;
        while (!lexer_match(p->lx, TOK_RBRACE)) {
            MatchArm *arm = (MatchArm *)calloc(1, sizeof(MatchArm));
            if (lexer_peek(p->lx).kind == TOK_INT) {
                Token t = lexer_next(p->lx);
                arm->int_pat = t.int_val;
            } else {
                Token t = lexer_peek(p->lx);
                expect(p, TOK_IDENT);
                if (!forge_str_eq(token_str(t), forge_str("_"))) {
                    parser_error(p, "match pattern must be integer or _");
                }
                arm->wildcard = true;
            }
            expect(p, TOK_FAT_ARROW);
            Block *body = (Block *)calloc(1, sizeof(Block));
            if (lexer_peek(p->lx).kind == TOK_LBRACE) {
                body = parse_block(p);
            } else {
                block_append(body, parse_stmt(p));
            }
            arm->body = body;
            if (!arms) arms = tail = arm;
            else { tail->next = arm; tail = arm; }
        }
        return stmt_match(scrutinee, arms);
    }
    if (lexer_match(p->lx, TOK_KW_IF)) {
        expect(p, TOK_LPAREN);
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        Block *then_br = parse_block(p);
        Block *else_br = NULL;
        if (lexer_match(p->lx, TOK_KW_ELSE)) {
            if (lexer_peek(p->lx).kind == TOK_KW_IF) {
                else_br = (Block *)calloc(1, sizeof(Block));
                block_append(else_br, parse_stmt(p));
            } else {
                else_br = parse_block(p);
            }
        }
        return stmt_if(cond, then_br, else_br);
    }
    if (lexer_match(p->lx, TOK_KW_WHILE)) {
        expect(p, TOK_LPAREN);
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN);
        Block *body = parse_block(p);
        return stmt_while(cond, body);
    }
    if (lexer_match(p->lx, TOK_KW_FOR)) {
        expect(p, TOK_LPAREN);
        Stmt *init = NULL;
        if (lexer_peek(p->lx).kind == TOK_KW_LET || lexer_peek(p->lx).kind == TOK_KW_MUT || lexer_peek(p->lx).kind == TOK_KW_OWN) {
            bool owned = lexer_match(p->lx, TOK_KW_OWN);
            bool mut = false;
            if (!owned) mut = lexer_match(p->lx, TOK_KW_MUT);
            if (!owned && !mut) expect(p, TOK_KW_LET);
            else if (owned) expect(p, TOK_KW_LET);
            Token name = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            ForgeType ty = owned ? forge_type_string() : forge_type_int();
            if (lexer_match(p->lx, TOK_COLON)) ty = parse_type(p);
            Expr *init_expr = NULL;
            if (lexer_match(p->lx, TOK_EQ)) init_expr = parse_expr(p);
            init = stmt_let(mut, owned, token_str(name), ty, init_expr);
        } else if (lexer_peek(p->lx).kind == TOK_IDENT) {
            Token name = lexer_peek(p->lx);
            lexer_next(p->lx);
            init = parse_assign(p, token_str(name));
        }
        expect(p, TOK_SEMI);
        Expr *cond = parse_expr(p);
        expect(p, TOK_SEMI);
        Stmt *step = NULL;
        if (lexer_peek(p->lx).kind == TOK_IDENT) {
            Token name = lexer_peek(p->lx);
            lexer_next(p->lx);
            step = parse_assign(p, token_str(name));
        }
        expect(p, TOK_RPAREN);
        Block *body = parse_block(p);
        return stmt_for(init, cond, step, body);
    }
    if (lexer_match(p->lx, TOK_KW_SPAWN)) {
        Token name = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        expect(p, TOK_LPAREN);
        Expr **args = NULL;
        size_t n = 0, cap = 0;
        if (!lexer_match(p->lx, TOK_RPAREN)) {
            do {
                if (n == cap) {
                    cap = cap ? cap * 2 : 4;
                    args = (Expr **)realloc(args, cap * sizeof(Expr *));
                }
                args[n++] = parse_expr(p);
            } while (lexer_match(p->lx, TOK_COMMA));
            expect(p, TOK_RPAREN);
        }
        expect(p, TOK_SEMI);
        return stmt_spawn(token_str(name), args, n);
    }
    if (lexer_match(p->lx, TOK_KW_SEND)) {
        Expr *target = parse_expr(p);
        expect(p, TOK_COMMA);
        Token tag_tok = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        expect(p, TOK_COMMA);
        bool move_val = lexer_match(p->lx, TOK_KW_MOVE);
        Expr *value = parse_expr(p);
        expect(p, TOK_SEMI);
        int tag = 0;
        for (size_t i = 0; i < tag_tok.lexeme.len; i++) {
            tag = tag * 31 + (unsigned char)tag_tok.lexeme.data[i];
        }
        return stmt_send(target, tag, value, move_val);
    }
    if (lexer_match(p->lx, TOK_KW_YIELD)) {
        expect(p, TOK_SEMI);
        return stmt_yield();
    }
    if (lexer_match(p->lx, TOK_KW_AWAIT)) {
        Expr *e = parse_expr(p);
        expect(p, TOK_SEMI);
        return stmt_await(e);
    }
    if (lexer_peek(p->lx).kind == TOK_LBRACE) {
        Block *b = parse_block(p);
        return stmt_block(b);
    }
    if (lexer_peek(p->lx).kind == TOK_IDENT) {
        Lexer saved = *p->lx;
        Token name = lexer_next(p->lx);
        TokenKind next = lexer_peek(p->lx).kind;
        if (next == TOK_EQ || next == TOK_PLUSEQ || next == TOK_MINUSEQ ||
            next == TOK_STAREQ || next == TOK_SLASHEQ || next == TOK_PERCENTEQ) {
            Stmt *s = parse_assign(p, token_str(name));
            expect(p, TOK_SEMI);
            return s;
        }
        *p->lx = saved;
    }
    Expr *e = parse_expr(p);
    expect(p, TOK_SEMI);
    return stmt_expr(e);
}

static Param *parse_params(Parser *p) {
    Param *head = NULL, *tail = NULL;
    if (lexer_match(p->lx, TOK_RPAREN)) return NULL;
    do {
        Token name = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        expect(p, TOK_COLON);
        ForgeType ty = parse_type(p);
        Param *param = (Param *)calloc(1, sizeof(Param));
        param->name = token_str(name);
        param->type = ty;
        if (!head) head = tail = param;
        else { tail->next = param; tail = param; }
    } while (lexer_match(p->lx, TOK_COMMA));
    expect(p, TOK_RPAREN);
    return head;
}

static FnDecl parse_fn_body(Parser *p, bool is_extern) {
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LPAREN);
    Param *params = parse_params(p);
    ForgeType ret = forge_type_void();
    if (lexer_match(p->lx, TOK_COLON)) ret = parse_type(p);
    FnDecl fn = { token_str(name), params, ret, block_new(), is_extern };
    if (is_extern) {
        expect(p, TOK_SEMI);
    } else {
        Block *body = parse_block(p);
        fn.body = *body;
        free(body);
    }
    return fn;
}

static FnDecl parse_fn(Parser *p) {
    expect(p, TOK_KW_FN);
    return parse_fn_body(p, false);
}

static FnDecl parse_extern_fn(Parser *p) {
    expect(p, TOK_KW_EXTERN);
    expect(p, TOK_KW_FN);
    return parse_fn_body(p, true);
}

static CoroDecl parse_coroutine(Parser *p) {
    expect(p, TOK_KW_COROUTINE);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LPAREN);
    Param *params = parse_params(p);
    Block *body = parse_block(p);
    CoroDecl coro = { token_str(name), params, *body };
    free(body);
    (void)params;
    return coro;
}

static ProcessDecl parse_process(Parser *p) {
    expect(p, TOK_KW_PROCESS);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);

    ProcessDecl proc = {0};
    proc.name = token_str(name);
    proc.body = block_new();

    CoroDecl *coros = NULL;
    size_t coro_count = 0, coro_cap = 0;

    while (lexer_peek(p->lx).kind != TOK_RBRACE) {
        if (lexer_peek(p->lx).kind == TOK_KW_COROUTINE) {
            if (coro_count == coro_cap) {
                coro_cap = coro_cap ? coro_cap * 2 : 4;
                coros = (CoroDecl *)realloc(coros, coro_cap * sizeof(CoroDecl));
            }
            coros[coro_count++] = parse_coroutine(p);
        } else if (lexer_match(p->lx, TOK_KW_ON)) {
            expect(p, TOK_KW_RECEIVE);
            expect(p, TOK_LPAREN);
            Token param = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            expect(p, TOK_COLON);
            ForgeType ty = parse_type(p);
            expect(p, TOK_RPAREN);
            proc.has_receive = true;
            proc.receive_param.name = token_str(param);
            proc.receive_param.type = ty;
            proc.on_receive = parse_block(p);
        } else {
            block_append(&proc.body, parse_stmt(p));
        }
    }
    expect(p, TOK_RBRACE);
    proc.coros = coros;
    proc.coro_count = coro_count;
    return proc;
}

static SupervisorDecl parse_supervisor(Parser *p) {
    expect(p, TOK_KW_SUPERVISOR);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);

    SupervisorDecl sup = {0};
    sup.name = token_str(name);
    sup.policy = SUP_RESTART_PROCESS;

    while (lexer_peek(p->lx).kind != TOK_RBRACE) {
        if (lexer_match(p->lx, TOK_KW_RESTART)) {
            expect(p, TOK_COLON);
            Token pol = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            if (forge_str_eq(token_str(pol), forge_str("coro"))) sup.policy = SUP_RESTART_CORO;
            else if (forge_str_eq(token_str(pol), forge_str("all"))) sup.policy = SUP_RESTART_ALL;
            else sup.policy = SUP_RESTART_PROCESS;
            expect(p, TOK_SEMI);
        } else {
            Token child_tok = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            ForgeStr child = token_str(child_tok);
            sup.child_count++;
            sup.children = (ForgeStr *)realloc(sup.children, sup.child_count * sizeof(ForgeStr));
            sup.children[sup.child_count - 1] = child;
            expect(p, TOK_SEMI);
        }
    }
    expect(p, TOK_RBRACE);
    return sup;
}

static LibraryDecl parse_library(Parser *p) {
    expect(p, TOK_KW_LIBRARY);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);

    LibraryDecl lib = {0};
    lib.name = token_str(name);
    lib.present = true;

    while (lexer_peek(p->lx).kind != TOK_RBRACE) {
        if (lexer_match(p->lx, TOK_KW_IMPORT)) {
            Token mod = lexer_peek(p->lx);
            expect(p, TOK_IDENT);
            expect(p, TOK_SEMI);
            lib.import_count++;
            lib.imports = (ForgeStr *)realloc(lib.imports, lib.import_count * sizeof(ForgeStr));
            lib.imports[lib.import_count - 1] = token_str(mod);
        } else if (lexer_match(p->lx, TOK_KW_EXPORT)) {
            expect(p, TOK_KW_FN);
            FnDecl fn = parse_fn_body(p, false);
            lib.fn_count++;
            lib.functions = (FnDecl *)realloc(lib.functions, lib.fn_count * sizeof(FnDecl));
            lib.functions[lib.fn_count - 1] = fn;
        } else {
            parser_error(p, "expected import or export in library");
        }
    }
    expect(p, TOK_RBRACE);
    return lib;
}

static StructDecl parse_struct(Parser *p) {
    expect(p, TOK_KW_STRUCT);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);

    StructDecl sd = { token_str(name), NULL };
    Field *tail = NULL;
    while (lexer_peek(p->lx).kind != TOK_RBRACE) {
        Token field_name = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        expect(p, TOK_COLON);
        ForgeType ty = parse_type(p);
        expect(p, TOK_SEMI);
        Field *f = (Field *)calloc(1, sizeof(Field));
        f->name = token_str(field_name);
        f->type = ty;
        if (!sd.fields) sd.fields = tail = f;
        else { tail->next = f; tail = f; }
    }
    expect(p, TOK_RBRACE);
    return sd;
}

static EnumDecl parse_enum(Parser *p) {
    expect(p, TOK_KW_ENUM);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    expect(p, TOK_LBRACE);

    EnumDecl ed = { token_str(name), NULL };
    EnumVariant *tail = NULL;
    int64_t next_val = 0;
    while (lexer_peek(p->lx).kind != TOK_RBRACE) {
        Token variant = lexer_peek(p->lx);
        expect(p, TOK_IDENT);
        int64_t val = next_val;
        if (lexer_match(p->lx, TOK_EQ)) {
            Token v = lexer_peek(p->lx);
            expect(p, TOK_INT);
            val = v.int_val;
            next_val = val + 1;
        } else {
            next_val++;
        }
        expect(p, TOK_SEMI);
        EnumVariant *ev = (EnumVariant *)calloc(1, sizeof(EnumVariant));
        ev->name = token_str(variant);
        ev->value = val;
        if (!ed.variants) ed.variants = tail = ev;
        else { tail->next = ev; tail = ev; }
    }
    expect(p, TOK_RBRACE);
    return ed;
}

static NativeDecl parse_native(Parser *p) {
    expect(p, TOK_KW_NATIVE);
    Token name = lexer_peek(p->lx);
    expect(p, TOK_IDENT);
    Block *body = parse_block(p);
    NativeDecl nd = { token_str(name), *body };
    free(body);
    return nd;
}

Program parse_program(Lexer *lx) {
    Parser p = { lx };
    Program prog = {0};

    while (lexer_peek(p.lx).kind != TOK_EOF) {
        Token t = lexer_peek(p.lx);
        if (t.kind == TOK_KW_IMPORT) {
            lexer_next(p.lx);
            if (lexer_peek(p.lx).kind == TOK_STRING) {
                Token path_tok = lexer_peek(p.lx);
                lexer_next(p.lx);
                expect(&p, TOK_SEMI);
                prog.path_import_count++;
                prog.path_imports = (ForgeStr *)realloc(prog.path_imports,
                    prog.path_import_count * sizeof(ForgeStr));
                prog.path_imports[prog.path_import_count - 1] = token_str(path_tok);
            } else {
                Token mod = lexer_peek(p.lx);
                expect(&p, TOK_IDENT);
                expect(&p, TOK_SEMI);
                prog.import_count++;
                prog.imports = (ForgeStr *)realloc(prog.imports, prog.import_count * sizeof(ForgeStr));
                prog.imports[prog.import_count - 1] = token_str(mod);
            }
        } else if (t.kind == TOK_KW_LIBRARY) {
            if (prog.library.present) parser_error(&p, "only one library per file");
            prog.library = parse_library(&p);
        } else if (t.kind == TOK_KW_STRUCT) {
            prog.struct_count++;
            prog.structs = (StructDecl *)realloc(prog.structs, prog.struct_count * sizeof(StructDecl));
            prog.structs[prog.struct_count - 1] = parse_struct(&p);
        } else if (t.kind == TOK_KW_ENUM) {
            prog.enum_count++;
            prog.enums = (EnumDecl *)realloc(prog.enums, prog.enum_count * sizeof(EnumDecl));
            prog.enums[prog.enum_count - 1] = parse_enum(&p);
        } else if (t.kind == TOK_KW_NATIVE) {
            prog.native_count++;
            prog.natives = (NativeDecl *)realloc(prog.natives, prog.native_count * sizeof(NativeDecl));
            prog.natives[prog.native_count - 1] = parse_native(&p);
        } else if (t.kind == TOK_KW_EXTERN) {
            prog.fn_count++;
            prog.functions = (FnDecl *)realloc(prog.functions, prog.fn_count * sizeof(FnDecl));
            prog.functions[prog.fn_count - 1] = parse_extern_fn(&p);
        } else if (t.kind == TOK_KW_PROCESS) {
            prog.process_count++;
            prog.processes = (ProcessDecl *)realloc(prog.processes, prog.process_count * sizeof(ProcessDecl));
            prog.processes[prog.process_count - 1] = parse_process(&p);
        } else if (t.kind == TOK_KW_CONST) {
            lexer_next(p.lx);
            Token name = lexer_peek(p.lx);
            expect(&p, TOK_IDENT);
            expect(&p, TOK_EQ);
            Expr *val = parse_expr(&p);
            expect(&p, TOK_SEMI);
            prog.const_count++;
            prog.consts = (ConstDecl *)realloc(prog.consts, prog.const_count * sizeof(ConstDecl));
            prog.consts[prog.const_count - 1] = (ConstDecl){ token_str(name), val };
        } else if (t.kind == TOK_KW_FN) {
            prog.fn_count++;
            prog.functions = (FnDecl *)realloc(prog.functions, prog.fn_count * sizeof(FnDecl));
            prog.functions[prog.fn_count - 1] = parse_fn(&p);
        } else if (t.kind == TOK_KW_SUPERVISOR) {
            prog.supervisor_count++;
            prog.supervisors = (SupervisorDecl *)realloc(prog.supervisors, prog.supervisor_count * sizeof(SupervisorDecl));
            prog.supervisors[prog.supervisor_count - 1] = parse_supervisor(&p);
        } else {
            parser_error(&p, "expected top-level declaration");
        }
    }
    return prog;
}
