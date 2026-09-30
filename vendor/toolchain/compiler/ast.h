#ifndef FORGE_AST_H
#define FORGE_AST_H

#include "common.h"

typedef enum {
    TY_VOID,
    TY_INT,
    TY_FLOAT,
    TY_BOOL,
    TY_STRING,
    TY_PTR,
    TY_STRUCT
} ForgeTypeKind;

typedef struct ForgeType {
    ForgeTypeKind kind;
    ForgeStr struct_name;
} ForgeType;

typedef enum {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_IDENT,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_QUAL_CALL,
    EXPR_RECV,
    EXPR_INDEX,
    EXPR_FIELD,
    EXPR_MOVE
} ExprKind;

typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NE, BIN_LT, BIN_LE, BIN_GT, BIN_GE,
    BIN_AND, BIN_OR
} BinOp;

typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Block Block;
typedef struct Param Param;

struct Expr {
    ExprKind kind;
    ForgeType type;
    union {
        int64_t int_val;
        double float_val;
        bool bool_val;
        ForgeStr string_val;
        ForgeStr ident;
        struct {
            BinOp op;
            Expr *left;
            Expr *right;
        } binary;
        struct {
            ForgeStr name;
            Expr **args;
            size_t arg_count;
        } call;
        struct {
            ForgeStr module;
            ForgeStr name;
            Expr **args;
            size_t arg_count;
        } qual_call;
        struct {
            Expr *base;
            Expr *index;
        } index;
        struct {
            Expr *base;
            ForgeStr field;
        } field;
        Expr *move_expr;
    } as;
};

struct Stmt {
    enum {
        STMT_LET,
        STMT_EXPR,
        STMT_RETURN,
        STMT_IF,
        STMT_WHILE,
        STMT_FOR,
        STMT_SPAWN,
        STMT_SEND,
        STMT_YIELD,
        STMT_ASSIGN,
        STMT_BREAK,
        STMT_CONTINUE,
        STMT_BLOCK,
        STMT_AWAIT,
        STMT_MATCH
    } kind;
    union {
        struct {
            bool mutable_;
            bool owned_;
            ForgeStr name;
            ForgeType type;
            Expr *init;
        } let;
        Expr *expr;
        Expr *ret;
        struct {
            Expr *cond;
            Block *then_br;
            Block *else_br;
        } if_stmt;
        struct {
            Expr *cond;
            Block *body;
        } while_stmt;
        struct {
            Stmt *init;
            Expr *cond;
            Stmt *step;
            Block *body;
        } for_stmt;
        struct {
            ForgeStr coro_name;
            Expr **args;
            size_t arg_count;
        } spawn;
        struct {
            Expr *target;
            int tag;
            Expr *value;
            bool move_;
        } send;
        struct {
            ForgeStr name;
            Expr *value;
        } assign;
        Expr *await_expr;
        Block *block;
        struct {
            Expr *scrutinee;
            struct MatchArm *arms;
        } match_stmt;
    } as;
    Stmt *next;
};

struct Block {
    Stmt *first;
    Stmt *last;
};

typedef struct MatchArm {
    bool wildcard;
    int64_t int_pat;
    Block *body;
    struct MatchArm *next;
} MatchArm;

typedef struct ConstDecl {
    ForgeStr name;
    Expr *value;
} ConstDecl;

struct Param {
    ForgeStr name;
    ForgeType type;
    Param *next;
};

typedef struct Field {
    ForgeStr name;
    ForgeType type;
    struct Field *next;
} Field;

typedef struct {
    ForgeStr name;
    Field *fields;
} StructDecl;

typedef struct EnumVariant {
    ForgeStr name;
    int64_t value;
    struct EnumVariant *next;
} EnumVariant;

typedef struct {
    ForgeStr name;
    EnumVariant *variants;
} EnumDecl;

typedef struct {
    ForgeStr name;
    Param *params;
    ForgeType ret_type;
    Block body;
    bool is_extern;
} FnDecl;

typedef struct {
    ForgeStr name;
    Param *params;
    Block body;
} CoroDecl;

typedef struct {
    ForgeStr name;
    Block body;
    CoroDecl *coros;
    size_t coro_count;
    Block *on_receive;
    Param receive_param;
    bool has_receive;
} ProcessDecl;

typedef struct {
    ForgeStr name;
    Block body;
} NativeDecl;

typedef struct {
    ForgeStr name;
    enum { SUP_RESTART_CORO, SUP_RESTART_PROCESS, SUP_RESTART_ALL } policy;
    ForgeStr *children;
    size_t child_count;
} SupervisorDecl;

typedef struct {
    ForgeStr name;
    ForgeStr *imports;
    size_t import_count;
    FnDecl *functions;
    size_t fn_count;
    bool present;
} LibraryDecl;

typedef struct {
    ForgeStr name;
    char *path;
    char *source;
    FnDecl *functions;
    size_t fn_count;
} FileModule;

typedef struct {
    ForgeStr *imports;
    size_t import_count;
    ForgeStr *path_imports;
    size_t path_import_count;
    FileModule *modules;
    size_t module_count;
    LibraryDecl library;
    StructDecl *structs;
    size_t struct_count;
    EnumDecl *enums;
    size_t enum_count;
    ProcessDecl *processes;
    size_t process_count;
    NativeDecl *natives;
    size_t native_count;
    FnDecl *functions;
    size_t fn_count;
    SupervisorDecl *supervisors;
    size_t supervisor_count;
    ConstDecl *consts;
    size_t const_count;
} Program;

ForgeType forge_type_void(void);
ForgeType forge_type_int(void);
ForgeType forge_type_float(void);
ForgeType forge_type_bool(void);
ForgeType forge_type_string(void);
ForgeType forge_type_ptr(void);
ForgeType forge_type_struct(ForgeStr name);
ForgeType forge_type_from_name(ForgeStr name);

Expr *expr_int(int64_t v);
Expr *expr_float(double v);
Expr *expr_bool(bool v);
Expr *expr_string(ForgeStr v);
Expr *expr_ident(ForgeStr name);
Expr *expr_binary(BinOp op, Expr *l, Expr *r);
Expr *expr_call(ForgeStr name, Expr **args, size_t n);
Expr *expr_qual_call(ForgeStr module, ForgeStr name, Expr **args, size_t n);
Expr *expr_index(Expr *base, Expr *index);
Expr *expr_field(Expr *base, ForgeStr field);
Expr *expr_recv(void);
Expr *expr_move(Expr *inner);

Block block_new(void);
void block_append(Block *b, Stmt *s);
Stmt *stmt_let(bool mut, bool owned, ForgeStr name, ForgeType ty, Expr *init);
Stmt *stmt_expr(Expr *e);
Stmt *stmt_return(Expr *e);
Stmt *stmt_if(Expr *cond, Block *then_br, Block *else_br);
Stmt *stmt_while(Expr *cond, Block *body);
Stmt *stmt_for(Stmt *init, Expr *cond, Stmt *step, Block *body);
Stmt *stmt_spawn(ForgeStr name, Expr **args, size_t n);
Stmt *stmt_send(Expr *target, int tag, Expr *value, bool move_);
Stmt *stmt_yield(void);
Stmt *stmt_await(Expr *e);
Stmt *stmt_assign(ForgeStr name, Expr *value);
Stmt *stmt_break(void);
Stmt *stmt_continue(void);
Stmt *stmt_block(Block *b);
Stmt *stmt_match(Expr *scrutinee, MatchArm *arms);

void program_free(Program *p);

#endif
