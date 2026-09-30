#include "ast.h"

ForgeType forge_type_void(void) { return (ForgeType){ .kind = TY_VOID }; }
ForgeType forge_type_int(void) { return (ForgeType){ .kind = TY_INT }; }
ForgeType forge_type_float(void) { return (ForgeType){ .kind = TY_FLOAT }; }
ForgeType forge_type_bool(void) { return (ForgeType){ .kind = TY_BOOL }; }
ForgeType forge_type_string(void) { return (ForgeType){ .kind = TY_STRING }; }
ForgeType forge_type_ptr(void) { return (ForgeType){ .kind = TY_PTR }; }
ForgeType forge_type_struct(ForgeStr name) { return (ForgeType){ .kind = TY_STRUCT, .struct_name = name }; }

ForgeType forge_type_from_name(ForgeStr name) {
    if (forge_str_eq(name, forge_str("int"))) return forge_type_int();
    if (forge_str_eq(name, forge_str("float"))) return forge_type_float();
    if (forge_str_eq(name, forge_str("bool"))) return forge_type_bool();
    if (forge_str_eq(name, forge_str("string"))) return forge_type_string();
    if (forge_str_eq(name, forge_str("void"))) return forge_type_void();
    if (forge_str_eq(name, forge_str("ptr"))) return forge_type_ptr();
    return forge_type_struct(name);
}

static Expr *expr_new(ExprKind kind) {
    Expr *e = (Expr *)calloc(1, sizeof(Expr));
    if (!e) forge_die("out of memory");
    e->kind = kind;
    return e;
}

Expr *expr_int(int64_t v) {
    Expr *e = expr_new(EXPR_INT);
    e->type = forge_type_int();
    e->as.int_val = v;
    return e;
}

Expr *expr_float(double v) {
    Expr *e = expr_new(EXPR_FLOAT);
    e->type = forge_type_float();
    e->as.float_val = v;
    return e;
}

Expr *expr_bool(bool v) {
    Expr *e = expr_new(EXPR_BOOL);
    e->type = forge_type_bool();
    e->as.bool_val = v;
    return e;
}

Expr *expr_string(ForgeStr v) {
    Expr *e = expr_new(EXPR_STRING);
    e->type = forge_type_string();
    e->as.string_val = v;
    return e;
}

Expr *expr_ident(ForgeStr name) {
    Expr *e = expr_new(EXPR_IDENT);
    e->as.ident = name;
    return e;
}

Expr *expr_binary(BinOp op, Expr *l, Expr *r) {
    Expr *e = expr_new(EXPR_BINARY);
    e->as.binary.op = op;
    e->as.binary.left = l;
    e->as.binary.right = r;
    if (op >= BIN_EQ) {
        e->type = forge_type_bool();
    } else if (l->type.kind == TY_INT && r->type.kind == TY_INT) {
        e->type = forge_type_int();
    } else if ((l->type.kind == TY_INT || l->type.kind == TY_FLOAT) &&
               (r->type.kind == TY_INT || r->type.kind == TY_FLOAT)) {
        e->type = forge_type_float();
    } else {
        /* Identifiers/calls are unresolved until codegen. Do not claim that
         * an arithmetic result has its left operand's type. */
        e->type = forge_type_void();
    }
    return e;
}

Expr *expr_call(ForgeStr name, Expr **args, size_t n) {
    Expr *e = expr_new(EXPR_CALL);
    e->as.call.name = name;
    e->as.call.args = args;
    e->as.call.arg_count = n;
    return e;
}

Expr *expr_qual_call(ForgeStr module, ForgeStr name, Expr **args, size_t n) {
    Expr *e = expr_new(EXPR_QUAL_CALL);
    e->as.qual_call.module = module;
    e->as.qual_call.name = name;
    e->as.qual_call.args = args;
    e->as.qual_call.arg_count = n;
    return e;
}

Expr *expr_recv(void) {
    Expr *e = expr_new(EXPR_RECV);
    e->type = forge_type_int();
    return e;
}

Expr *expr_move(Expr *inner) {
    Expr *e = expr_new(EXPR_MOVE);
    e->as.move_expr = inner;
    e->type = inner->type;
    return e;
}

Expr *expr_index(Expr *base, Expr *index) {
    Expr *e = expr_new(EXPR_INDEX);
    e->as.index.base = base;
    e->as.index.index = index;
    e->type = forge_type_int();
    return e;
}

Expr *expr_field(Expr *base, ForgeStr field) {
    Expr *e = expr_new(EXPR_FIELD);
    e->as.field.base = base;
    e->as.field.field = field;
    e->type = base->type;
    return e;
}

Block block_new(void) {
    return (Block){ NULL, NULL };
}

void block_append(Block *b, Stmt *s) {
    if (!b->first) {
        b->first = b->last = s;
    } else {
        b->last->next = s;
        b->last = s;
    }
}

static Stmt *stmt_new(int kind) {
    Stmt *s = (Stmt *)calloc(1, sizeof(Stmt));
    if (!s) forge_die("out of memory");
    s->kind = kind;
    return s;
}

Stmt *stmt_let(bool mut, bool owned, ForgeStr name, ForgeType ty, Expr *init) {
    Stmt *s = stmt_new(STMT_LET);
    s->as.let.mutable_ = mut;
    s->as.let.owned_ = owned;
    s->as.let.name = name;
    s->as.let.type = ty;
    s->as.let.init = init;
    return s;
}

Stmt *stmt_expr(Expr *e) {
    Stmt *s = stmt_new(STMT_EXPR);
    s->as.expr = e;
    return s;
}

Stmt *stmt_return(Expr *e) {
    Stmt *s = stmt_new(STMT_RETURN);
    s->as.ret = e;
    return s;
}

Stmt *stmt_if(Expr *cond, Block *then_br, Block *else_br) {
    Stmt *s = stmt_new(STMT_IF);
    s->as.if_stmt.cond = cond;
    s->as.if_stmt.then_br = then_br;
    s->as.if_stmt.else_br = else_br;
    return s;
}

Stmt *stmt_while(Expr *cond, Block *body) {
    Stmt *s = stmt_new(STMT_WHILE);
    s->as.while_stmt.cond = cond;
    s->as.while_stmt.body = body;
    return s;
}

Stmt *stmt_for(Stmt *init, Expr *cond, Stmt *step, Block *body) {
    Stmt *s = stmt_new(STMT_FOR);
    s->as.for_stmt.init = init;
    s->as.for_stmt.cond = cond;
    s->as.for_stmt.step = step;
    s->as.for_stmt.body = body;
    return s;
}

Stmt *stmt_spawn(ForgeStr name, Expr **args, size_t n) {
    Stmt *s = stmt_new(STMT_SPAWN);
    s->as.spawn.coro_name = name;
    s->as.spawn.args = args;
    s->as.spawn.arg_count = n;
    return s;
}

Stmt *stmt_send(Expr *target, int tag, Expr *value, bool move_) {
    Stmt *s = stmt_new(STMT_SEND);
    s->as.send.target = target;
    s->as.send.tag = tag;
    s->as.send.value = value;
    s->as.send.move_ = move_;
    return s;
}

Stmt *stmt_yield(void) {
    return stmt_new(STMT_YIELD);
}

Stmt *stmt_await(Expr *e) {
    Stmt *s = stmt_new(STMT_AWAIT);
    s->as.await_expr = e;
    return s;
}

Stmt *stmt_assign(ForgeStr name, Expr *value) {
    Stmt *s = stmt_new(STMT_ASSIGN);
    s->as.assign.name = name;
    s->as.assign.value = value;
    return s;
}

Stmt *stmt_break(void) { return stmt_new(STMT_BREAK); }
Stmt *stmt_continue(void) { return stmt_new(STMT_CONTINUE); }

Stmt *stmt_block(Block *b) {
    Stmt *s = stmt_new(STMT_BLOCK);
    s->as.block = b;
    return s;
}

Stmt *stmt_match(Expr *scrutinee, MatchArm *arms) {
    Stmt *s = stmt_new(STMT_MATCH);
    s->as.match_stmt.scrutinee = scrutinee;
    s->as.match_stmt.arms = arms;
    return s;
}

static void free_stmts(Stmt *s);

static void free_match_arms(MatchArm *arms) {
    while (arms) {
        MatchArm *next = arms->next;
        free_stmts(arms->body->first);
        free(arms->body);
        free(arms);
        arms = next;
    }
}

static void free_expr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
    case EXPR_BINARY:
        free_expr(e->as.binary.left);
        free_expr(e->as.binary.right);
        break;
    case EXPR_CALL:
        for (size_t i = 0; i < e->as.call.arg_count; i++) free_expr(e->as.call.args[i]);
        free(e->as.call.args);
        break;
    case EXPR_QUAL_CALL:
        for (size_t i = 0; i < e->as.qual_call.arg_count; i++) free_expr(e->as.qual_call.args[i]);
        free(e->as.qual_call.args);
        break;
    case EXPR_INDEX:
        free_expr(e->as.index.base);
        free_expr(e->as.index.index);
        break;
    case EXPR_FIELD:
        free_expr(e->as.field.base);
        break;
    case EXPR_MOVE:
        free_expr(e->as.move_expr);
        break;
    default:
        break;
    }
    free(e);
}

static void free_stmts(Stmt *s) {
    while (s) {
        Stmt *next = s->next;
        switch (s->kind) {
        case STMT_LET: free_expr(s->as.let.init); break;
        case STMT_EXPR: free_expr(s->as.expr); break;
        case STMT_RETURN: free_expr(s->as.ret); break;
        case STMT_IF:
            free_expr(s->as.if_stmt.cond);
            free_stmts(s->as.if_stmt.then_br->first);
            free(s->as.if_stmt.then_br);
            if (s->as.if_stmt.else_br) {
                free_stmts(s->as.if_stmt.else_br->first);
                free(s->as.if_stmt.else_br);
            }
            break;
        case STMT_WHILE:
            free_expr(s->as.while_stmt.cond);
            free_stmts(s->as.while_stmt.body->first);
            free(s->as.while_stmt.body);
            break;
        case STMT_FOR:
            if (s->as.for_stmt.init) free_stmts(s->as.for_stmt.init);
            free_expr(s->as.for_stmt.cond);
            if (s->as.for_stmt.step) free_stmts(s->as.for_stmt.step);
            free_stmts(s->as.for_stmt.body->first);
            free(s->as.for_stmt.body);
            break;
        case STMT_SPAWN:
            for (size_t i = 0; i < s->as.spawn.arg_count; i++) free_expr(s->as.spawn.args[i]);
            free(s->as.spawn.args);
            break;
        case STMT_SEND:
            free_expr(s->as.send.target);
            free_expr(s->as.send.value);
            break;
        case STMT_ASSIGN:
            free_expr(s->as.assign.value);
            break;
        case STMT_AWAIT:
            free_expr(s->as.await_expr);
            break;
        case STMT_BLOCK:
            free_stmts(s->as.block->first);
            free(s->as.block);
            break;
        case STMT_MATCH:
            free_expr(s->as.match_stmt.scrutinee);
            free_match_arms(s->as.match_stmt.arms);
            break;
        default:
            break;
        }
        free(s);
        s = next;
    }
}

void program_free(Program *p) {
    free(p->imports);
    free(p->path_imports);
    for (size_t i = 0; i < p->module_count; i++) {
        free(p->modules[i].path);
        free(p->modules[i].source);
        for (size_t j = 0; j < p->modules[i].fn_count; j++) {
            free_stmts(p->modules[i].functions[j].body.first);
        }
        free(p->modules[i].functions);
    }
    free(p->modules);
    if (p->library.present) {
        free(p->library.imports);
        for (size_t i = 0; i < p->library.fn_count; i++) {
            free_stmts(p->library.functions[i].body.first);
        }
        free(p->library.functions);
    }
    for (size_t i = 0; i < p->process_count; i++) {
        ProcessDecl *pd = &p->processes[i];
        free_stmts(pd->body.first);
        for (size_t j = 0; j < pd->coro_count; j++) {
            free_stmts(pd->coros[j].body.first);
        }
        free(pd->coros);
        if (pd->has_receive) {
            free_stmts(pd->on_receive->first);
            free(pd->on_receive);
        }
    }
    free(p->processes);
    for (size_t i = 0; i < p->native_count; i++) {
        free_stmts(p->natives[i].body.first);
    }
    free(p->natives);
    for (size_t i = 0; i < p->struct_count; i++) {
        Field *f = p->structs[i].fields;
        while (f) { Field *n = f->next; free(f); f = n; }
    }
    free(p->structs);
    for (size_t i = 0; i < p->enum_count; i++) {
        EnumVariant *v = p->enums[i].variants;
        while (v) { EnumVariant *n = v->next; free(v); v = n; }
    }
    free(p->enums);
    for (size_t i = 0; i < p->fn_count; i++) {
        if (!p->functions[i].is_extern) free_stmts(p->functions[i].body.first);
    }
    free(p->functions);
    for (size_t i = 0; i < p->supervisor_count; i++) {
        free(p->supervisors[i].children);
    }
    free(p->supervisors);
    for (size_t i = 0; i < p->const_count; i++) {
        free_expr(p->consts[i].value);
    }
    free(p->consts);
}
