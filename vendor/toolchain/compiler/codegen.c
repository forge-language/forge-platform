#include "codegen.h"
#include "mod_registry.h"
#include <stdarg.h>
#include <string.h>

typedef struct {
    FILE *out;
    int indent;
    Program *prog;
    const char *state_prefix;
    struct ForgeLocal { ForgeStr name; ForgeType type; } *locals;
    size_t local_count;
    size_t local_cap;
    ForgeStr *imports;
    size_t import_count;
    int loop_depth;
    FnDecl **fn_table;
    size_t fn_table_count;
    ForgeStr *moved_locals;
    size_t moved_count;
    size_t moved_cap;
    ForgeStr current_module;
} Codegen;

static FnDecl *lookup_module_fn(Program *prog, ForgeStr module, ForgeStr name) {
    for (size_t i = 0; i < prog->module_count; i++) {
        if (!forge_str_eq(prog->modules[i].name, module)) continue;
        for (size_t j = 0; j < prog->modules[i].fn_count; j++) {
            if (forge_str_eq(prog->modules[i].functions[j].name, name))
                return &prog->modules[i].functions[j];
        }
    }
    return NULL;
}

static void cg_mark_moved(Codegen *cg, ForgeStr name) {
    for (size_t i = 0; i < cg->moved_count; i++) {
        if (forge_str_eq(cg->moved_locals[i], name)) return;
    }
    if (cg->moved_count == cg->moved_cap) {
        cg->moved_cap = cg->moved_cap ? cg->moved_cap * 2 : 8;
        cg->moved_locals = (ForgeStr *)realloc(cg->moved_locals, cg->moved_cap * sizeof(ForgeStr));
    }
    cg->moved_locals[cg->moved_count++] = name;
}

static int cg_is_moved(Codegen *cg, ForgeStr name) {
    for (size_t i = 0; i < cg->moved_count; i++) {
        if (forge_str_eq(cg->moved_locals[i], name)) return 1;
    }
    return 0;
}

static FnDecl *cg_lookup_fn(Codegen *cg, ForgeStr name) {
    for (size_t i = 0; i < cg->fn_table_count; i++) {
        if (forge_str_eq(cg->fn_table[i]->name, name)) return cg->fn_table[i];
    }
    return NULL;
}

static size_t fn_param_count(FnDecl *fn) {
    size_t n = 0;
    if (!fn) return 0;
    for (Param *p = fn->params; p; p = p->next) n++;
    return n;
}

static FnDecl *cg_resolve_fn_ref(Codegen *cg, ForgeStr name) {
    FnDecl *fn = cg_lookup_fn(cg, name);
    if (fn) return fn;
    if (cg->current_module.len > 0)
        return lookup_module_fn(cg->prog, cg->current_module, name);
    return NULL;
}

static void emit_fn_c_symbol(Codegen *cg, ForgeStr name, FnDecl *fn) {
    if (cg->current_module.len > 0 &&
        lookup_module_fn(cg->prog, cg->current_module, name) == fn) {
        char sym[128];
        forge_mod_mangle(sym, sizeof(sym), cg->current_module, name);
        fputs(sym, cg->out);
    } else {
        fprintf(cg->out, "%.*s", (int)name.len, name.data);
    }
}

static void emit_expr(Codegen *cg, Expr *e);

static int emit_thread_call(Codegen *cg, ForgeStr name, Expr **args, size_t arg_count) {
    if (forge_str_eq(name, forge_str("thread_spawn")) && arg_count == 2) {
        Expr *fn_expr = args[0];
        if (fn_expr->kind != EXPR_IDENT) {
            fprintf(stderr, "forge: thread_spawn requires a function name\n");
            exit(1);
        }
        ForgeStr fn_name = fn_expr->as.ident;
        FnDecl *fn = cg_resolve_fn_ref(cg, fn_name);
        if (!fn || fn_param_count(fn) != 1) {
            fprintf(stderr, "forge: thread_spawn(fn, arg) requires fn(arg: int): int\n");
            exit(1);
        }
        fputs("fr_threading_spawn((fr_threading_fn1_t)", cg->out);
        emit_fn_c_symbol(cg, fn_name, fn);
        fputs(", ", cg->out);
        emit_expr(cg, args[1]);
        fputc(')', cg->out);
        return 1;
    }

    if (forge_str_eq(name, forge_str("thread_spawn_indexed")) && arg_count == 2) {
        Expr *fn_expr = args[0];
        if (fn_expr->kind != EXPR_IDENT) {
            fprintf(stderr, "forge: thread_spawn_indexed requires a function name\n");
            exit(1);
        }
        ForgeStr fn_name = fn_expr->as.ident;
        FnDecl *fn = cg_resolve_fn_ref(cg, fn_name);
        if (!fn || fn_param_count(fn) != 2) {
            fprintf(stderr, "forge: thread_spawn_indexed(fn, n) requires fn(id: int, total: int): int\n");
            exit(1);
        }
        fputs("fr_threading_spawn_indexed((fr_threading_fn2_t)", cg->out);
        emit_fn_c_symbol(cg, fn_name, fn);
        fputs(", ", cg->out);
        emit_expr(cg, args[1]);
        fputc(')', cg->out);
        return 1;
    }

    return 0;
}

static int cg_lookup_const(Codegen *cg, ForgeStr name) {
    for (size_t i = 0; i < cg->prog->const_count; i++) {
        if (forge_str_eq(cg->prog->consts[i].name, name)) return 1;
    }
    return 0;
}

static void cg_build_fn_table(Codegen *cg) {
    Program *prog = cg->prog;
    size_t n = prog->fn_count;
    if (prog->library.present) n += prog->library.fn_count;
    cg->fn_table = (FnDecl **)calloc(n, sizeof(FnDecl *));
  if (!cg->fn_table) forge_die("out of memory");
    size_t k = 0;
    for (size_t i = 0; i < prog->fn_count; i++)
        cg->fn_table[k++] = &prog->functions[i];
    if (prog->library.present) {
        for (size_t i = 0; i < prog->library.fn_count; i++)
            cg->fn_table[k++] = &prog->library.functions[i];
    }
    cg->fn_table_count = k;
}

static void cg_push_local(Codegen *cg, ForgeStr name, ForgeType ty) {
    for (size_t i = 0; i < cg->local_count; i++) {
        if (forge_str_eq(cg->locals[i].name, name)) {
            cg->locals[i].type = ty;
            return;
        }
    }
    if (cg->local_count == cg->local_cap) {
        cg->local_cap = cg->local_cap ? cg->local_cap * 2 : 8;
        cg->locals = (struct ForgeLocal *)realloc(cg->locals, cg->local_cap * sizeof(struct ForgeLocal));
    }
    cg->locals[cg->local_count].name = name;
    cg->locals[cg->local_count].type = ty;
    cg->local_count++;
}

static void cg_push_params(Codegen *cg, Param *params) {
    for (Param *p = params; p; p = p->next) cg_push_local(cg, p->name, p->type);
}

static ForgeType cg_lookup_local(Codegen *cg, ForgeStr name) {
    for (size_t i = 0; i < cg->local_count; i++) {
        if (forge_str_eq(cg->locals[i].name, name)) return cg->locals[i].type;
    }
    return forge_type_int();
}

static int cg_stdlib_returns_string(Codegen *cg, ForgeStr name) {
    const char *mapped = forge_std_c_name(name, cg->imports, cg->import_count);
    if (!mapped) return 0;
    static const char *str_fns[] = {
        "fr_str_concat", "fr_str_sub", "fr_str_trim", "fr_fs_read", "fr_fs_list_dir",
        "fr_io_read_line", "fr_io_read_stdin", "fr_io_prompt", "fr_io_read_fd",
        "fr_tcp_recv", "fr_udp_recv", "fr_http_get", "fr_http_post",
        "fr_http_req_method", "fr_http_req_path", "fr_http_req_body",
        "fr_json_get_string", "fr_json_stringify_str", "fr_json_stringify_int",
        "fr_udp_peer", "fr_os_getenv", "fr_os_argv",
        "fr_gpu_backend", "fr_gpu_device_name",
        "fr_str_append", "fr_str_append_str", "fr_str_from_int", NULL
    };
    for (int i = 0; str_fns[i]; i++) {
        if (strcmp(mapped, str_fns[i]) == 0) return 1;
    }
    return 0;
}

static int qual_call_returns_string(ForgeStr fn) {
    static const char *names[] = {
        "hello", "greet", "shout", "format", "read", "recv", "get", "path", "body",
        "peer", "concat", "trim", "sub", "message", NULL
    };
    for (int i = 0; names[i]; i++) {
        if (forge_str_eq(fn, forge_str(names[i]))) return 1;
    }
    return 0;
}

static int cg_expr_is_string(Codegen *cg, Expr *e) {
    if (e->kind == EXPR_STRING) return 1;
    if (e->kind == EXPR_IDENT) return cg_lookup_local(cg, e->as.ident).kind == TY_STRING;
    if (e->kind == EXPR_CALL) {
        if (cg_stdlib_returns_string(cg, e->as.call.name)) return 1;
        FnDecl *fn = cg_resolve_fn_ref(cg, e->as.call.name);
        return fn && fn->ret_type.kind == TY_STRING;
    }
    if (e->kind == EXPR_QUAL_CALL) {
        FnDecl *fn = lookup_module_fn(cg->prog, e->as.qual_call.module, e->as.qual_call.name);
        if (fn) return fn->ret_type.kind == TY_STRING;
        return qual_call_returns_string(e->as.qual_call.name);
    }
    return 0;
}

static void cg_indent(Codegen *cg) {
    for (int i = 0; i < cg->indent; i++) fputs("    ", cg->out);
}

static void cg_line(Codegen *cg, const char *fmt, ...) {
    cg_indent(cg);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(cg->out, fmt, ap);
    va_end(ap);
    fputc('\n', cg->out);
}

static const char *c_type(ForgeType ty) {
    switch (ty.kind) {
    case TY_INT: return "int64_t";
    case TY_FLOAT: return "double";
    case TY_BOOL: return "int";
    case TY_STRING: return "const char*";
    case TY_PTR: return "void*";
    case TY_STRUCT:
        fprintf(stderr, "forge: internal error: use c_type_name for struct types\n");
        return "void*";
    default: return "void";
    }
}

static void c_type_name(FILE *out, ForgeType ty) {
    if (ty.kind == TY_STRUCT) {
        fprintf(out, "%.*s", (int)ty.struct_name.len, ty.struct_name.data);
    } else {
        fputs(c_type(ty), out);
    }
}

static void emit_stmts(Codegen *cg, Stmt *s, const char *proc_var, bool in_coro, const char *state_var);

static void emit_expr(Codegen *cg, Expr *e) {
    switch (e->kind) {
    case EXPR_INT:
        if (e->as.int_val == INT64_MIN) {
            fputs("(-INT64_C(9223372036854775807) - INT64_C(1))", cg->out);
        } else if (e->as.int_val < 0) {
            fprintf(cg->out, "(-INT64_C(%lld))", (long long)-e->as.int_val);
        } else {
            fprintf(cg->out, "INT64_C(%lld)", (long long)e->as.int_val);
        }
        break;
    case EXPR_FLOAT: {
        char literal[64];
        snprintf(literal, sizeof(literal), "%.17g", e->as.float_val);
        fputs(literal, cg->out);
        if (!strpbrk(literal, ".eE")) fputs(".0", cg->out);
        break;
    }
    case EXPR_BOOL:
        fprintf(cg->out, "%s", e->as.bool_val ? "1" : "0");
        break;
    case EXPR_STRING: {
        fputc('"', cg->out);
        for (size_t i = 0; i < e->as.string_val.len; i++) {
            char c = e->as.string_val.data[i];
            if (c == '\\' && i + 1 < e->as.string_val.len &&
                strchr("nrt\\\"", e->as.string_val.data[i + 1])) {
                fputc('\\', cg->out);
                fputc(e->as.string_val.data[++i], cg->out);
                continue;
            }
            if (c == '\n') { fputs("\\n", cg->out); continue; }
            if (c == '\r') { fputs("\\r", cg->out); continue; }
            if (c == '\t') { fputs("\\t", cg->out); continue; }
            if (c == '"' || c == '\\') fputc('\\', cg->out);
            fputc(c, cg->out);
        }
        fputc('"', cg->out);
        break;
    }
    case EXPR_IDENT:
        if (cg_is_moved(cg, e->as.ident)) {
            fprintf(stderr, "forge: use of moved value '%.*s'\n",
                    (int)e->as.ident.len, e->as.ident.data);
            exit(1);
        }
        if (cg_lookup_const(cg, e->as.ident)) {
            fprintf(cg->out, "forge_const_%.*s", (int)e->as.ident.len, e->as.ident.data);
            break;
        }
        if (cg->state_prefix) fputs(cg->state_prefix, cg->out);
        fprintf(cg->out, "%.*s", (int)e->as.ident.len, e->as.ident.data);
        break;
    case EXPR_MOVE:
        if (e->as.move_expr && e->as.move_expr->kind == EXPR_IDENT) {
            ForgeStr name = e->as.move_expr->as.ident;
            cg_mark_moved(cg, name);
            fputs("fr_own_take(&", cg->out);
            if (cg->state_prefix) fputs(cg->state_prefix, cg->out);
            fprintf(cg->out, "%.*s)", (int)name.len, name.data);
        } else {
            emit_expr(cg, e->as.move_expr);
        }
        break;
    case EXPR_BINARY: {
        const char *op = "?";
        switch (e->as.binary.op) {
        case BIN_ADD: op = "+"; break;
        case BIN_SUB: op = "-"; break;
        case BIN_MUL: op = "*"; break;
        case BIN_DIV: op = "/"; break;
        case BIN_MOD: op = "%"; break;
        case BIN_EQ: op = "=="; break;
        case BIN_NE: op = "!="; break;
        case BIN_LT: op = "<"; break;
        case BIN_LE: op = "<="; break;
        case BIN_GT: op = ">"; break;
        case BIN_GE: op = ">="; break;
        case BIN_AND: op = "&&"; break;
        case BIN_OR: op = "||"; break;
        }
        fputc('(', cg->out);
        emit_expr(cg, e->as.binary.left);
        fprintf(cg->out, " %s ", op);
        emit_expr(cg, e->as.binary.right);
        fputc(')', cg->out);
        break;
    }
    case EXPR_CALL: {
        ForgeStr name = e->as.call.name;
        if (emit_thread_call(cg, name, e->as.call.args, e->as.call.arg_count)) break;
        if (forge_str_eq(name, forge_str("println"))) {
            fputs("({ ", cg->out);
            for (size_t i = 0; i < e->as.call.arg_count; i++) {
                Expr *arg = e->as.call.args[i];
                if (i > 0) fputs(" ", cg->out);
                if (cg_expr_is_string(cg, arg)) {
                    fputs("fr_print_str(", cg->out);
                    emit_expr(cg, arg);
                    fputs("); ", cg->out);
                } else {
                    fputs("fr_print_int(", cg->out);
                    emit_expr(cg, arg);
                    fputs("); ", cg->out);
                }
            }
            fputs("fr_println(); })", cg->out);
            break;
        }
        const char *c_fn = forge_std_c_name(name, cg->imports, cg->import_count);
        if (c_fn) {
            fprintf(cg->out, "%s(", c_fn);
        } else if (cg->current_module.len > 0 &&
                   lookup_module_fn(cg->prog, cg->current_module, name)) {
            char sym[128];
            forge_mod_mangle(sym, sizeof(sym), cg->current_module, name);
            fprintf(cg->out, "%s(", sym);
            FnDecl *fn = lookup_module_fn(cg->prog, cg->current_module, name);
            if (fn) e->type = fn->ret_type;
        } else {
            FnDecl *fn = cg_lookup_fn(cg, name);
            if (fn) e->type = fn->ret_type;
            fprintf(cg->out, "%.*s(", (int)name.len, name.data);
        }
        for (size_t i = 0; i < e->as.call.arg_count; i++) {
            if (i) fputc(',', cg->out);
            emit_expr(cg, e->as.call.args[i]);
        }
        fputc(')', cg->out);
        break;
    }
    case EXPR_RECV:
        fputs("(fr_recv(fr_coro_process(__coro), &__recv_msg), __recv_msg.value)", cg->out);
        break;
    case EXPR_QUAL_CALL: {
        char sym[128];
        if (forge_import_is_file_module(cg->prog, e->as.qual_call.module))
            forge_mod_mangle(sym, sizeof(sym), e->as.qual_call.module, e->as.qual_call.name);
        else
            forge_lib_mangle(sym, sizeof(sym), e->as.qual_call.module, e->as.qual_call.name);
        fprintf(cg->out, "%s(", sym);
        for (size_t i = 0; i < e->as.qual_call.arg_count; i++) {
            if (i) fputc(',', cg->out);
            emit_expr(cg, e->as.qual_call.args[i]);
        }
        fputc(')', cg->out);
        break;
    }
    case EXPR_INDEX: {
        ForgeType base_ty = e->as.index.base->type;
        if (base_ty.kind == TY_STRING || cg_expr_is_string(cg, e->as.index.base)) {
            fputs("fr_str_char_at(", cg->out);
            emit_expr(cg, e->as.index.base);
            fputs(", ", cg->out);
            emit_expr(cg, e->as.index.index);
            fputc(')', cg->out);
        } else {
            fputs("((int64_t*)", cg->out);
            emit_expr(cg, e->as.index.base);
            fputs(")[", cg->out);
            emit_expr(cg, e->as.index.index);
            fputs("]", cg->out);
        }
        break;
    }
    case EXPR_FIELD:
        emit_expr(cg, e->as.field.base);
        fprintf(cg->out, ".%.*s", (int)e->as.field.field.len, e->as.field.field.data);
        break;
    }
}

static void emit_coro_state_struct(Codegen *cg, CoroDecl *coro) {
    fprintf(cg->out, "typedef struct {\n");
    fprintf(cg->out, "    int _forge_step;\n");
    fprintf(cg->out, "    fr_process_t *proc;\n");
    for (Param *p = coro->params; p; p = p->next) {
        fprintf(cg->out, "    %s %.*s;\n", c_type(p->type), (int)p->name.len, p->name.data);
    }
    for (Stmt *s = coro->body.first; s; s = s->next) {
        if (s->kind == STMT_LET) {
            fprintf(cg->out, "    %s %.*s;\n", c_type(s->as.let.type),
                    (int)s->as.let.name.len, s->as.let.name.data);
        }
    }
    fprintf(cg->out, "} %.*s_state_t;\n\n", (int)coro->name.len, coro->name.data);
}

static void emit_coro_body(Codegen *cg, CoroDecl *coro, const char *state_var, int *next_step) {
    Stmt *s = coro->body.first;
    while (s) {
        if (s->kind == STMT_YIELD) {
            int resume_step = (*next_step)++;
            cg_line(cg, "fr_coro_set_step(__coro, %d);", resume_step);
            cg_line(cg, "return fr_yield(__coro);");
            cg_line(cg, "case %d:;", resume_step);
            s = s->next;
            continue;
        }

        if (s->kind == STMT_AWAIT) {
            int resume_step = (*next_step)++;
            cg_line(cg, "fr_coro_set_step(__coro, %d);", resume_step);
            cg_line(cg, "case %d:;", resume_step);
            cg_indent(cg);
            fputs("if (!fr_await_fd(__coro, ", cg->out);
            emit_expr(cg, s->as.await_expr);
            fputs(", FR_EVENT_READ)) return FR_CORO_WAITING_IO;\n", cg->out);
            s = s->next;
            continue;
        }

        if (s->kind == STMT_LET) {
            cg_indent(cg);
            if (cg->state_prefix) fputs(cg->state_prefix, cg->out);
            fprintf(cg->out, "%.*s = ", (int)s->as.let.name.len, s->as.let.name.data);
            if (s->as.let.init) emit_expr(cg, s->as.let.init);
            else fputs("0", cg->out);
            fputs(";\n", cg->out);
            s = s->next;
            continue;
        }

        switch (s->kind) {
        case STMT_EXPR:
            cg_indent(cg);
            emit_expr(cg, s->as.expr);
            fputs(";\n", cg->out);
            break;
        case STMT_RETURN:
            cg_indent(cg);
            if (s->as.ret) emit_expr(cg, s->as.ret);
            fputs(";\n", cg->out);
            cg_line(cg, "return FR_CORO_DONE;");
            break;
        case STMT_IF:
            cg_line(cg, "if (");
            emit_expr(cg, s->as.if_stmt.cond);
            fputs(") {\n", cg->out);
            cg->indent++;
            emit_coro_body(cg, &(CoroDecl){ .body = *s->as.if_stmt.then_br }, state_var, next_step);
            cg->indent--;
            cg_indent(cg);
            fputs("}", cg->out);
            if (s->as.if_stmt.else_br) {
                fputs(" else {\n", cg->out);
                cg->indent++;
                emit_coro_body(cg, &(CoroDecl){ .body = *s->as.if_stmt.else_br }, state_var, next_step);
                cg->indent--;
                cg_line(cg, "}");
            } else {
                fputc('\n', cg->out);
            }
            break;
        case STMT_WHILE:
            cg_line(cg, "while (");
            emit_expr(cg, s->as.while_stmt.cond);
            fputs(") {\n", cg->out);
            cg->indent++;
            emit_coro_body(cg, &(CoroDecl){ .body = *s->as.while_stmt.body }, state_var, next_step);
            cg->indent--;
            cg_line(cg, "}");
            break;
        case STMT_SPAWN:
            cg_line(cg, "%.*s_spawn(%s->proc", (int)s->as.spawn.coro_name.len,
                    s->as.spawn.coro_name.data, state_var);
            for (size_t i = 0; i < s->as.spawn.arg_count; i++) {
                fputs(", ", cg->out);
                emit_expr(cg, s->as.spawn.args[i]);
            }
            fputs(");\n", cg->out);
            break;
        case STMT_SEND:
            cg_indent(cg);
            if (s->as.send.move_) {
                fputs("fr_send_move(", cg->out);
                emit_expr(cg, s->as.send.target);
                fprintf(cg->out, ", %d, ", s->as.send.tag);
                emit_expr(cg, s->as.send.value);
                fputs(");\n", cg->out);
            } else {
                fputs("fr_send(", cg->out);
                emit_expr(cg, s->as.send.target);
                fprintf(cg->out, ", %d, ", s->as.send.tag);
                emit_expr(cg, s->as.send.value);
                fputs(", NULL, 0);\n", cg->out);
            }
            break;
        case STMT_ASSIGN:
            cg_indent(cg);
            if (cg->state_prefix) fputs(cg->state_prefix, cg->out);
            fprintf(cg->out, "%.*s = ", (int)s->as.assign.name.len, s->as.assign.name.data);
            emit_expr(cg, s->as.assign.value);
            fputs(";\n", cg->out);
            break;
        case STMT_BLOCK:
            cg->indent++;
            emit_coro_body(cg, &(CoroDecl){ .body = *s->as.block }, state_var, next_step);
            cg->indent--;
            break;
        default:
            break;
        }
        s = s->next;
    }
}

static void emit_coro_fn(Codegen *cg, CoroDecl *coro) {
    emit_coro_state_struct(cg, coro);
    fprintf(cg->out, "static fr_coro_status_t %.*s_fn(fr_coro_t *__coro, void *__userdata) {\n",
            (int)coro->name.len, coro->name.data);
    fprintf(cg->out, "    %.*s_state_t *%s = (%.*s_state_t *)__userdata;\n",
            (int)coro->name.len, coro->name.data, "st",
            (int)coro->name.len, coro->name.data);
    fputs("    fr_msg_t __recv_msg;\n", cg->out);
    fputs("    switch (fr_coro_step(__coro)) {\n", cg->out);
    cg->indent++;
    cg_line(cg, "default:");
    const char *saved_prefix = cg->state_prefix;
    cg->state_prefix = "st->";
    cg->local_count = 0;
    for (Param *p = coro->params; p; p = p->next) cg_push_local(cg, p->name, p->type);
    for (Stmt *s = coro->body.first; s; s = s->next) {
        if (s->kind == STMT_LET) cg_push_local(cg, s->as.let.name, s->as.let.type);
    }
    int next_step = 1;
    emit_coro_body(cg, coro, "st", &next_step);
    cg->state_prefix = saved_prefix;
    cg->local_count = 0;
    cg->indent--;
    fputs("    }\n", cg->out);
    fputs("    return FR_CORO_DONE;\n", cg->out);
    fputs("}\n\n", cg->out);

    fprintf(cg->out, "static void %.*s_spawn(fr_process_t *proc", (int)coro->name.len, coro->name.data);
    for (Param *p = coro->params; p; p = p->next) {
        fprintf(cg->out, ", %s %.*s", c_type(p->type), (int)p->name.len, p->name.data);
    }
    fputs(") {\n", cg->out);
    fprintf(cg->out, "    %.*s_state_t *init = (%.*s_state_t *)calloc(1, sizeof(%.*s_state_t));\n",
            (int)coro->name.len, coro->name.data,
            (int)coro->name.len, coro->name.data,
            (int)coro->name.len, coro->name.data);
    fputs("    init->proc = proc;\n", cg->out);
    for (Param *p = coro->params; p; p = p->next) {
        fprintf(cg->out, "    init->%.*s = %.*s;\n", (int)p->name.len, p->name.data,
                (int)p->name.len, p->name.data);
    }
    fprintf(cg->out, "    fr_coro_spawn(proc, %.*s_fn, init, sizeof(%.*s_state_t));\n",
            (int)coro->name.len, coro->name.data,
            (int)coro->name.len, coro->name.data);
    fputs("}\n\n", cg->out);
}

static void emit_if_stmt(Codegen *cg, Stmt *s, const char *proc_var, bool in_coro, const char *state_var) {
    cg_indent(cg);
    fputs("if (", cg->out);
    emit_expr(cg, s->as.if_stmt.cond);
    fputs(") {\n", cg->out);
    cg->indent++;
    emit_stmts(cg, s->as.if_stmt.then_br->first, proc_var, in_coro, state_var);
    cg->indent--;
    cg_indent(cg);
    fputs("}", cg->out);
    if (s->as.if_stmt.else_br) {
        Stmt *es = s->as.if_stmt.else_br->first;
        if (es && !es->next && es->kind == STMT_IF) {
            fputs(" else ", cg->out);
            emit_if_stmt(cg, es, proc_var, in_coro, state_var);
            fputc('\n', cg->out);
        } else {
            fputs(" else {\n", cg->out);
            cg->indent++;
            emit_stmts(cg, s->as.if_stmt.else_br->first, proc_var, in_coro, state_var);
            cg->indent--;
            cg_line(cg, "}");
        }
    } else {
        fputc('\n', cg->out);
    }
}

static void emit_stmts(Codegen *cg, Stmt *s, const char *proc_var, bool in_coro, const char *state_var) {
    while (s) {
        switch (s->kind) {
        case STMT_LET:
            cg_push_local(cg, s->as.let.name, s->as.let.type);
            cg_indent(cg);
            c_type_name(cg->out, s->as.let.type);
            fprintf(cg->out, " %.*s", (int)s->as.let.name.len, s->as.let.name.data);
            if (s->as.let.init) {
                fputs(" = ", cg->out);
                emit_expr(cg, s->as.let.init);
            }
            fputs(";\n", cg->out);
            break;
        case STMT_EXPR:
            cg_indent(cg);
            emit_expr(cg, s->as.expr);
            fputs(";\n", cg->out);
            break;
        case STMT_RETURN:
            cg_indent(cg);
            fputs("return ", cg->out);
            if (s->as.ret) emit_expr(cg, s->as.ret);
            fputs(";\n", cg->out);
            break;
        case STMT_IF:
            emit_if_stmt(cg, s, proc_var, in_coro, state_var);
            break;
        case STMT_WHILE:
            cg_line(cg, "while (");
            emit_expr(cg, s->as.while_stmt.cond);
            fputs(") {\n", cg->out);
            cg->indent++;
            cg->loop_depth++;
            emit_stmts(cg, s->as.while_stmt.body->first, proc_var, in_coro, state_var);
            cg->loop_depth--;
            cg_indent(cg);
            fputs("}\n", cg->out);
            break;
        case STMT_FOR:
            fputs("    {\n", cg->out);
            cg->indent++;
            if (s->as.for_stmt.init) emit_stmts(cg, s->as.for_stmt.init, proc_var, in_coro, state_var);
            cg_indent(cg);
            fputs("while (", cg->out);
            emit_expr(cg, s->as.for_stmt.cond);
            fputs(") {\n", cg->out);
            cg->indent++;
            cg->loop_depth++;
            emit_stmts(cg, s->as.for_stmt.body->first, proc_var, in_coro, state_var);
            if (s->as.for_stmt.step) emit_stmts(cg, s->as.for_stmt.step, proc_var, in_coro, state_var);
            cg->loop_depth--;
            cg_indent(cg);
            fputs("}\n", cg->out);
            cg->indent--;
            cg_indent(cg);
            fputs("}\n", cg->out);
            break;
        case STMT_BREAK:
            cg_line(cg, "break;");
            break;
        case STMT_CONTINUE:
            cg_line(cg, "continue;");
            break;
        case STMT_SPAWN:
            cg_indent(cg);
            fprintf(cg->out, "%.*s_spawn(%s", (int)s->as.spawn.coro_name.len,
                    s->as.spawn.coro_name.data, proc_var);
            for (size_t i = 0; i < s->as.spawn.arg_count; i++) {
                fputs(", ", cg->out);
                emit_expr(cg, s->as.spawn.args[i]);
            }
            fputs(");\n", cg->out);
            break;
        case STMT_SEND:
            cg_indent(cg);
            if (s->as.send.move_) {
                fputs("fr_send_move(", cg->out);
                emit_expr(cg, s->as.send.target);
                fprintf(cg->out, ", %d, ", s->as.send.tag);
                emit_expr(cg, s->as.send.value);
                fputs(");\n", cg->out);
            } else {
                fputs("fr_send(", cg->out);
                emit_expr(cg, s->as.send.target);
                fprintf(cg->out, ", %d, ", s->as.send.tag);
                emit_expr(cg, s->as.send.value);
                fputs(", NULL, 0);\n", cg->out);
            }
            break;
        case STMT_YIELD:
            if (in_coro) cg_line(cg, "return fr_yield(__coro);");
            break;
        case STMT_AWAIT:
            if (in_coro) {
                cg_indent(cg);
                fputs("if (!fr_await_fd(__coro, ", cg->out);
                emit_expr(cg, s->as.await_expr);
                fputs(", FR_EVENT_READ)) return fr_yield(__coro);\n", cg->out);
            }
            break;
        case STMT_ASSIGN:
            cg_indent(cg);
            fprintf(cg->out, "%.*s = ", (int)s->as.assign.name.len, s->as.assign.name.data);
            emit_expr(cg, s->as.assign.value);
            fputs(";\n", cg->out);
            break;
        case STMT_MATCH: {
            cg_indent(cg);
            fputs("{\n", cg->out);
            cg->indent++;
            cg_indent(cg);
            fputs("int64_t __match_val = ", cg->out);
            emit_expr(cg, s->as.match_stmt.scrutinee);
            fputs(";\n", cg->out);
            MatchArm *arm = s->as.match_stmt.arms;
            int first = 1;
            while (arm) {
                cg_indent(cg);
                if (arm->wildcard) {
                    fputs(first ? "if (1)" : "else", cg->out);
                } else {
                    fprintf(cg->out, "%s (__match_val == %lld)", first ? "if" : "else if",
                            (long long)arm->int_pat);
                }
                fputs(" {\n", cg->out);
                cg->indent++;
                emit_stmts(cg, arm->body->first, proc_var, in_coro, state_var);
                cg->indent--;
                cg_line(cg, "}");
                first = 0;
                arm = arm->next;
            }
            cg->indent--;
            cg_line(cg, "}");
            break;
        }
        case STMT_BLOCK:
            cg->indent++;
            emit_stmts(cg, s->as.block->first, proc_var, in_coro, state_var);
            cg->indent--;
            break;
        }
        s = s->next;
    }
}

static ProcessDecl *find_process(Program *prog, ForgeStr name) {
    for (size_t i = 0; i < prog->process_count; i++) {
        if (forge_str_eq(prog->processes[i].name, name)) return &prog->processes[i];
    }
    return NULL;
}

static void emit_consts(Program *prog, FILE *out) {
    for (size_t i = 0; i < prog->const_count; i++) {
        ConstDecl *c = &prog->consts[i];
        if (c->value && c->value->kind == EXPR_INT) {
            fprintf(out, "static const int64_t forge_const_%.*s = %lld;\n",
                    (int)c->name.len, c->name.data, (long long)c->value->as.int_val);
        } else if (c->value && c->value->kind == EXPR_STRING) {
            fprintf(out, "static const char *forge_const_%.*s = \"%.*s\";\n",
                    (int)c->name.len, c->name.data,
                    (int)c->value->as.string_val.len, c->value->as.string_val.data);
        }
    }
    if (prog->const_count > 0) fputs("\n", out);
}

static void emit_import_headers(Program *prog, FILE *out) {
    for (size_t i = 0; i < prog->import_count; i++) {
        if (forge_import_is_file_module(prog, prog->imports[i])) continue;
        const char *hdr = forge_std_header(prog->imports[i]);
        if (hdr) fprintf(out, "#include \"%s\"\n", hdr);
        else fprintf(out, "#include \"%.*s.h\"\n", (int)prog->imports[i].len, prog->imports[i].data);
    }
}

static void emit_fn_signature(FILE *out, const char *name, FnDecl *fn) {
    c_type_name(out, fn->ret_type);
    if (name && name[0]) fprintf(out, " %s", name);
    fputs("(", out);
    Param *p = fn->params;
    bool first = true;
    while (p) {
        if (!first) fputs(", ", out);
        c_type_name(out, p->type);
        fprintf(out, " %.*s", (int)p->name.len, p->name.data);
        first = false;
        p = p->next;
    }
    fputs(")", out);
}

static void emit_structs(Program *prog, FILE *out) {
    for (size_t i = 0; i < prog->struct_count; i++) {
        StructDecl *sd = &prog->structs[i];
        fprintf(out, "typedef struct {\n");
        for (Field *f = sd->fields; f; f = f->next) {
            fputs("    ", out);
            c_type_name(out, f->type);
            fprintf(out, " %.*s;\n", (int)f->name.len, f->name.data);
        }
        fprintf(out, "} %.*s;\n\n", (int)sd->name.len, sd->name.data);
    }
}

static void emit_enums(Program *prog, FILE *out) {
    for (size_t i = 0; i < prog->enum_count; i++) {
        EnumDecl *ed = &prog->enums[i];
        fprintf(out, "typedef enum {\n");
        for (EnumVariant *v = ed->variants; v; v = v->next) {
            fprintf(out, "    %.*s_%.*s = %lld,\n",
                    (int)ed->name.len, ed->name.data,
                    (int)v->name.len, v->name.data,
                    (long long)v->value);
        }
        fprintf(out, "} %.*s;\n\n", (int)ed->name.len, ed->name.data);
    }
}

static NativeDecl *find_native(Program *prog, ForgeStr name) {
    for (size_t i = 0; i < prog->native_count; i++) {
        if (forge_str_eq(prog->natives[i].name, name)) return &prog->natives[i];
    }
    return NULL;
}

void codegen_emit_library(Program *prog, FILE *out_c, FILE *out_h, const char *runtime_include) {
    if (!prog->library.present) {
        fprintf(stderr, "forge: no library block found\n");
        exit(1);
    }
    LibraryDecl *lib = &prog->library;
    char guard[128];
    snprintf(guard, sizeof(guard), "FRLIB_%.*s_H", (int)lib->name.len, lib->name.data);
    for (char *p = guard; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    }

    if (!out_h && !out_c) {
        fprintf(stderr, "forge: library mode requires header or C output\n");
        exit(1);
    }

    if (out_h) {
        fprintf(out_h, "#ifndef %s\n#define %s\n\n", guard, guard);
        fprintf(out_h, "#include <stdint.h>\n\n");
        for (size_t i = 0; i < lib->fn_count; i++) {
            char sym[128];
            forge_lib_mangle(sym, sizeof(sym), lib->name, lib->functions[i].name);
            emit_fn_signature(out_h, sym, &lib->functions[i]);
            fputs(";\n", out_h);
        }
        fprintf(out_h, "\n#endif\n");
    }

    if (!out_c) return;

    fprintf(out_c, "// Generated by Forge compiler (library mode)\n");
    fprintf(out_c, "#include \"%s\"\n", runtime_include);
    fputs("#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n", out_c);
    for (size_t i = 0; i < lib->import_count; i++) {
        const char *hdr = forge_std_header(lib->imports[i]);
        if (hdr) fprintf(out_c, "#include \"%s\"\n", hdr);
        else fprintf(out_c, "#include \"%.*s.h\"\n", (int)lib->imports[i].len, lib->imports[i].data);
    }
    fputs("\n", out_c);

    Codegen cg = { .out = out_c, .prog = prog, .imports = lib->imports,
                   .import_count = lib->import_count };
    cg_build_fn_table(&cg);
    for (size_t i = 0; i < lib->fn_count; i++) {
        FnDecl *fn = &lib->functions[i];
        char sym[128];
        forge_lib_mangle(sym, sizeof(sym), lib->name, fn->name);
        emit_fn_signature(out_c, sym, fn);
        fputs(" {\n", out_c);
        cg.indent = 1;
        cg.local_count = 0;
        cg_push_params(&cg, fn->params);
        emit_stmts(&cg, fn->body.first, NULL, false, NULL);
        cg.indent = 0;
        fputs("}\n\n", out_c);
    }
    free(cg.moved_locals);
    free(cg.locals);
    free(cg.fn_table);
}

void codegen_emit(Program *prog, FILE *out, const char *runtime_include) {
    Codegen cg = { .out = out, .prog = prog, .imports = prog->imports,
                   .import_count = prog->import_count };
    cg_build_fn_table(&cg);

    fputs("// Generated by Forge compiler (native backend)\n", out);
    fprintf(out, "#include \"%s\"\n", runtime_include);
    fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n", out);
    fputs("#include \"forge/os.h\"\n", out);
    fputs("#include \"forge/ownership.h\"\n", out);
    fputs("#include \"forge/event.h\"\n", out);
    emit_import_headers(prog, out);
    fputs("\n", out);

    emit_consts(prog, out);

    emit_structs(prog, out);
    emit_enums(prog, out);

    for (size_t i = 0; i < prog->fn_count; i++) {
        FnDecl *fn = &prog->functions[i];
        if (fn->is_extern) {
            fputs("extern ", out);
            char fn_name[128];
            snprintf(fn_name, sizeof(fn_name), "%.*s", (int)fn->name.len, fn->name.data);
            emit_fn_signature(out, fn_name, fn);
            fputs(";\n", out);
        }
    }
    fputs("\n", out);

    for (size_t i = 0; i < prog->fn_count; i++) {
        FnDecl *fn = &prog->functions[i];
        if (fn->is_extern) continue;
        fputs("static ", out);
        char fn_name[128];
        snprintf(fn_name, sizeof(fn_name), "%.*s", (int)fn->name.len, fn->name.data);
        emit_fn_signature(out, fn_name, fn);
        fputs(";\n", out);
    }
    fputs("\n", out);

    for (size_t i = 0; i < prog->module_count; i++) {
        FileModule *mod = &prog->modules[i];
        for (size_t j = 0; j < mod->fn_count; j++) {
            char sym[128];
            forge_mod_mangle(sym, sizeof(sym), mod->name, mod->functions[j].name);
            fputs("static ", out);
            emit_fn_signature(out, sym, &mod->functions[j]);
            fputs(";\n", out);
        }
    }
    fputs("\n", out);

    for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *proc = &prog->processes[i];
        for (size_t j = 0; j < proc->coro_count; j++) {
            emit_coro_fn(&cg, &proc->coros[j]);
        }
    }

    for (size_t i = 0; i < prog->fn_count; i++) {
        FnDecl *fn = &prog->functions[i];
        if (fn->is_extern) continue;
        fputs("static ", out);
        char fn_name[128];
        snprintf(fn_name, sizeof(fn_name), "%.*s", (int)fn->name.len, fn->name.data);
        emit_fn_signature(out, fn_name, fn);
        fputs(" {\n", out);
        cg.indent = 1;
        cg.local_count = 0;
        cg.current_module = forge_str("");
        cg_push_params(&cg, fn->params);
        emit_stmts(&cg, fn->body.first, NULL, false, NULL);
        cg.indent = 0;
        fputs("}\n\n", out);
    }

    for (size_t i = 0; i < prog->module_count; i++) {
        FileModule *mod = &prog->modules[i];
        for (size_t j = 0; j < mod->fn_count; j++) {
            FnDecl *fn = &mod->functions[j];
            fputs("static ", out);
            char sym[128];
            forge_mod_mangle(sym, sizeof(sym), mod->name, fn->name);
            emit_fn_signature(out, sym, fn);
            fputs(" {\n", out);
            cg.indent = 1;
            cg.local_count = 0;
            cg.current_module = mod->name;
            cg_push_params(&cg, fn->params);
            emit_stmts(&cg, fn->body.first, NULL, false, NULL);
            cg.indent = 0;
            fputs("}\n\n", out);
        }
        cg.current_module = forge_str("");
    }

    for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *pd = &prog->processes[i];
        if (!pd->body.first) continue;
        fprintf(out, "static void %.*s_init(fr_process_t *proc) {\n", (int)pd->name.len, pd->name.data);
        cg.indent = 1;
        cg.local_count = 0;
        emit_stmts(&cg, pd->body.first, "proc", false, NULL);
        cg.indent = 0;
        fputs("}\n\n", out);
    }

    ForgeStr entry_name = forge_str("main");
    NativeDecl *native_main = find_native(prog, entry_name);

    if (native_main) {
        fputs("int main(int argc, char **argv) {\n", out);
        cg.indent = 1;
        cg.local_count = 0;
        cg_line(&cg, "fr_os_set_args(argc, argv);");
        emit_stmts(&cg, native_main->body.first, NULL, false, NULL);
        cg_line(&cg, "return 0;");
        cg.indent = 0;
        fputs("}\n", out);
        free(cg.locals);
        free(cg.fn_table);
        return;
    }

    ProcessDecl *entry = find_process(prog, entry_name);
    if (!entry && prog->process_count > 0) entry = &prog->processes[0];

    if (!entry) {
        fputs("int main(void) { return 0; }\n", out);
        free(cg.locals);
        free(cg.fn_table);
        return;
    }

    fputs("int main(int argc, char **argv) {\n", out);
  cg.indent = 1;
    cg_line(&cg, "fr_os_set_args(argc, argv);");
    cg_line(&cg, "fr_scheduler_t *sched = fr_scheduler_create(0);");

    for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *pd = &prog->processes[i];
        fprintf(out, "    fr_process_t *proc_%.*s = fr_process_create(\"%.*s\");\n",
                (int)pd->name.len, pd->name.data, (int)pd->name.len, pd->name.data);
        cg_line(&cg, "fr_scheduler_add_process(sched, proc_%.*s);", (int)pd->name.len, pd->name.data);
    }

    for (size_t i = 0; i < prog->supervisor_count; i++) {
        SupervisorDecl *sup = &prog->supervisors[i];
        const char *pol = "FR_RESTART_PROCESS";
        if (sup->policy == SUP_RESTART_CORO) pol = "FR_RESTART_CORO";
        else if (sup->policy == SUP_RESTART_ALL) pol = "FR_RESTART_ALL";
        fprintf(out, "    fr_process_t *sup_%.*s = fr_supervisor_create(\"%.*s\", %s);\n",
                (int)sup->name.len, sup->name.data, (int)sup->name.len, sup->name.data, pol);
        for (size_t j = 0; j < sup->child_count; j++) {
            ProcessDecl *child = find_process(prog, sup->children[j]);
            if (child) {
                fprintf(out, "    fr_supervisor_add_child(sup_%.*s, proc_%.*s);\n",
                        (int)sup->name.len, sup->name.data,
                        (int)child->name.len, child->name.data);
            }
        }
        cg_line(&cg, "fr_scheduler_add_process(sched, sup_%.*s);", (int)sup->name.len, sup->name.data);
    }

    for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *pd = &prog->processes[i];
        if (pd->body.first) {
            fprintf(out, "    %.*s_init(proc_%.*s);\n",
                    (int)pd->name.len, pd->name.data,
                    (int)pd->name.len, pd->name.data);
        }
    }

    cg_line(&cg, "fr_scheduler_run(sched);");
    cg_line(&cg, "fr_scheduler_destroy(sched);");
    cg_line(&cg, "return 0;");
    cg.indent = 0;
    fputs("}\n", out);
    free(cg.moved_locals);
    free(cg.locals);
    free(cg.fn_table);
}
