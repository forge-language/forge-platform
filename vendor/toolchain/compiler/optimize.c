#include "optimize.h"

#include <math.h>

/* The C backend uses signed 64-bit arithmetic. Never execute an overflowing
 * operation in the compiler; leave it for the backend's runtime semantics. */
static bool fold_int(BinOp op, int64_t a, int64_t b, int64_t *out) {
    switch (op) {
    case BIN_ADD:
        if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
        *out = a + b;
        return true;
    case BIN_SUB:
        if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
        *out = a - b;
        return true;
    case BIN_MUL:
        if (a > 0) {
            if ((b > 0 && a > INT64_MAX / b) || (b < 0 && b < INT64_MIN / a)) return false;
        } else if (a < 0) {
            if ((b > 0 && a < INT64_MIN / b) || (b < 0 && a < INT64_MAX / b)) return false;
        }
        *out = a * b;
        return true;
    case BIN_DIV:
    case BIN_MOD:
        if (b == 0 || (a == INT64_MIN && b == -1)) return false;
        *out = op == BIN_DIV ? a / b : a % b;
        return true;
    default:
        return false;
    }
}

/* Reuse the binary node when folding rather than allocating a replacement for
 * every expression (including expressions that cannot be folded). */
static Expr *fold_binary(Expr *e) {
    Expr *l = e->as.binary.left, *r = e->as.binary.right;
    BinOp op = e->as.binary.op;
    int64_t iv = 0;
    double fv = 0;
    bool bv = false;
    ExprKind kind;
    if (l->kind == EXPR_INT && r->kind == EXPR_INT) {
        int64_t a = l->as.int_val, b = r->as.int_val;
        kind = EXPR_BOOL;
        switch (op) {
        case BIN_EQ: bv = a == b; break;
        case BIN_NE: bv = a != b; break;
        case BIN_LT: bv = a < b; break;
        case BIN_LE: bv = a <= b; break;
        case BIN_GT: bv = a > b; break;
        case BIN_GE: bv = a >= b; break;
        default:
            if (!fold_int(op, a, b, &iv)) return e;
            kind = EXPR_INT;
            break;
        }
    } else if (l->kind == EXPR_FLOAT && r->kind == EXPR_FLOAT) {
        double a = l->as.float_val, b = r->as.float_val;
        switch (op) {
        case BIN_ADD: fv = a + b; break;
        case BIN_SUB: fv = a - b; break;
        case BIN_MUL: fv = a * b; break;
        case BIN_DIV:
            if (b == 0.0) return e;
            fv = a / b;
            break;
        default: return e;
        }
        if (!isfinite(fv)) return e;
        kind = EXPR_FLOAT;
    } else if (l->kind == EXPR_BOOL && r->kind == EXPR_BOOL) {
        bool a = l->as.bool_val, b = r->as.bool_val;
        switch (op) {
        case BIN_EQ: bv = a == b; break;
        case BIN_NE: bv = a != b; break;
        case BIN_AND: bv = a && b; break;
        case BIN_OR: bv = a || b; break;
        default: return e;
        }
        kind = EXPR_BOOL;
    } else {
        return e;
    }
    free(l);
    free(r);
    e->kind = kind;
    if (kind == EXPR_INT) { e->type = forge_type_int(); e->as.int_val = iv; }
    else if (kind == EXPR_FLOAT) { e->type = forge_type_float(); e->as.float_val = fv; }
    else { e->type = forge_type_bool(); e->as.bool_val = bv; }
    return e;
}

static Expr *simplify_binary(Expr *e) {
    Expr *l = e->as.binary.left, *r = e->as.binary.right;
    BinOp op = e->as.binary.op;
    /* Types of identifiers and calls are unresolved here. Restrict identity
     * rules to known ints, so float promotion and signed zero are preserved.
     * Do not simplify x * 0: evaluating x may have side effects. */
    if (l->type.kind == TY_INT && r->type.kind == TY_INT) {
        if ((op == BIN_ADD && l->kind == EXPR_INT && l->as.int_val == 0) ||
            (op == BIN_MUL && l->kind == EXPR_INT && l->as.int_val == 1)) {
            free(l);
            free(e);
            return r;
        }
        if (((op == BIN_ADD || op == BIN_SUB) && r->kind == EXPR_INT && r->as.int_val == 0) ||
            (op == BIN_MUL && r->kind == EXPR_INT && r->as.int_val == 1)) {
            free(r);
            free(e);
            return l;
        }
    }
    return fold_binary(e);
}

static Expr *optimize_expr(Expr *e) {
    if (!e) return NULL;
    switch (e->kind) {
    case EXPR_BINARY:
        e->as.binary.left = optimize_expr(e->as.binary.left);
        e->as.binary.right = optimize_expr(e->as.binary.right);
        return simplify_binary(e);
    case EXPR_CALL:
        for (size_t i = 0; i < e->as.call.arg_count; i++)
            e->as.call.args[i] = optimize_expr(e->as.call.args[i]);
        break;
    case EXPR_QUAL_CALL:
        for (size_t i = 0; i < e->as.qual_call.arg_count; i++)
            e->as.qual_call.args[i] = optimize_expr(e->as.qual_call.args[i]);
        break;
    case EXPR_INDEX:
        e->as.index.base = optimize_expr(e->as.index.base);
        e->as.index.index = optimize_expr(e->as.index.index);
        break;
    case EXPR_FIELD:
        e->as.field.base = optimize_expr(e->as.field.base);
        break;
    case EXPR_MOVE:
        e->as.move_expr = optimize_expr(e->as.move_expr);
        break;
    default:
        break;
    }
    return e;
}

static void optimize_block(Block *b) {
    if (!b) return;
    for (Stmt *s = b->first; s; s = s->next) {
        switch (s->kind) {
        case STMT_LET:
            s->as.let.init = optimize_expr(s->as.let.init);
            break;
        case STMT_EXPR:
            s->as.expr = optimize_expr(s->as.expr);
            break;
        case STMT_RETURN:
            s->as.ret = optimize_expr(s->as.ret);
            break;
        case STMT_IF:
            s->as.if_stmt.cond = optimize_expr(s->as.if_stmt.cond);
            optimize_block(s->as.if_stmt.then_br);
            optimize_block(s->as.if_stmt.else_br);
            break;
        case STMT_WHILE:
            s->as.while_stmt.cond = optimize_expr(s->as.while_stmt.cond);
            optimize_block(s->as.while_stmt.body);
            break;
        case STMT_FOR:
            if (s->as.for_stmt.init) optimize_block(&(Block){ s->as.for_stmt.init, s->as.for_stmt.init });
            s->as.for_stmt.cond = optimize_expr(s->as.for_stmt.cond);
            if (s->as.for_stmt.step) optimize_block(&(Block){ s->as.for_stmt.step, s->as.for_stmt.step });
            optimize_block(s->as.for_stmt.body);
            break;
        case STMT_SPAWN:
            for (size_t i = 0; i < s->as.spawn.arg_count; i++)
                s->as.spawn.args[i] = optimize_expr(s->as.spawn.args[i]);
            break;
        case STMT_SEND:
            s->as.send.target = optimize_expr(s->as.send.target);
            s->as.send.value = optimize_expr(s->as.send.value);
            break;
        case STMT_ASSIGN:
            s->as.assign.value = optimize_expr(s->as.assign.value);
            break;
        case STMT_BLOCK:
            optimize_block(s->as.block);
            break;
        case STMT_MATCH: {
            s->as.match_stmt.scrutinee = optimize_expr(s->as.match_stmt.scrutinee);
            for (MatchArm *arm = s->as.match_stmt.arms; arm; arm = arm->next)
                optimize_block(arm->body);
            break;
        }
        case STMT_AWAIT:
            s->as.await_expr = optimize_expr(s->as.await_expr);
            break;
        default:
            break;
        }
    }
}

void optimize_program(Program *prog) {
    for (size_t i = 0; i < prog->const_count; i++) {
        if (prog->consts[i].value)
            prog->consts[i].value = optimize_expr(prog->consts[i].value);
    }
    for (size_t i = 0; i < prog->fn_count; i++)
        if (!prog->functions[i].is_extern) optimize_block(&prog->functions[i].body);
    for (size_t i = 0; i < prog->native_count; i++)
        optimize_block(&prog->natives[i].body);
    for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *pd = &prog->processes[i];
        optimize_block(&pd->body);
        for (size_t j = 0; j < pd->coro_count; j++)
            optimize_block(&pd->coros[j].body);
        optimize_block(pd->on_receive);
    }
    if (prog->library.present) {
        for (size_t i = 0; i < prog->library.fn_count; i++)
            optimize_block(&prog->library.functions[i].body);
    }
    for (size_t i = 0; i < prog->module_count; i++) {
        for (size_t j = 0; j < prog->modules[i].fn_count; j++)
            optimize_block(&prog->modules[i].functions[j].body);
    }
}
