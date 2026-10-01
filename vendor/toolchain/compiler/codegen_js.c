#include "codegen.h"
#include "mod_registry.h"
#include <inttypes.h>
#include <string.h>

typedef struct {
  FILE *out;
  Program *prog;
  ForgeStr module;
  unsigned indent;
  unsigned match_id;
  ForgeStr locals[1024];
  size_t local_count;
} JS;
static void expression(JS *g, Expr *e);
static void statements(JS *g, Stmt *s);
static void pad(JS *g) {
  for (unsigned i = 0; i < g->indent; i++)
    fputs("  ", g->out);
}
static FnDecl *module_fn(Program *p, ForgeStr module, ForgeStr name) {
  for (size_t i = 0; i < p->module_count; i++)
    if (forge_str_eq(p->modules[i].name, module))
      for (size_t j = 0; j < p->modules[i].fn_count; j++)
        if (forge_str_eq(p->modules[i].functions[j].name, name))
          return &p->modules[i].functions[j];
  return NULL;
}
static int local(JS *g, ForgeStr name_) {
  for (size_t i = 0; i < g->local_count; i++)
    if (forge_str_eq(g->locals[i], name_))
      return 1;
  return 0;
}
static void add_local(JS *g, ForgeStr name_) {
  if (g->local_count == 1024)
    forge_die("JavaScript function exceeds 1024 local bindings");
  g->locals[g->local_count++] = name_;
}
static void name(FILE *out, ForgeStr value) {
  fprintf(out, "fr_%.*s", (int)value.len, value.data);
}
static void module_name(FILE *out, ForgeStr module, ForgeStr value) {
  char symbol[256];
  forge_mod_mangle(symbol, sizeof(symbol), module, value);
  fputs(symbol, out);
}
static void quote(FILE *out, ForgeStr value) {
  fputc('"', out);
  for (size_t i = 0; i < value.len; i++) {
    unsigned char c = (unsigned char)value.data[i];
    if (c == '\\' && i + 1 < value.len &&
        strchr("nrt\\\"", value.data[i + 1])) {
      fputc('\\', out);
      fputc(value.data[++i], out);
    } else if (c == '"' || c == '\\') {
      fputc('\\', out);
      fputc(c, out);
    } else if (c < 32)
      fprintf(out, "\\u%04x", c);
    else
      fputc(c, out);
  }
  fputc('"', out);
}
static void arguments(JS *g, Expr **args, size_t count) {
  fputc('(', g->out);
  for (size_t i = 0; i < count; i++) {
    if (i)
      fputc(',', g->out);
    expression(g, args[i]);
  }
  fputc(')', g->out);
}
static int supported_standard(const char *name_) {
  static const char *supported[] = {"fr_str_len",
                                    "fr_str_concat",
                                    "fr_str_eq",
                                    "fr_str_sub",
                                    "fr_str_contains",
                                    "fr_str_trim",
                                    "fr_str_char_at",
                                    "fr_str_append",
                                    "fr_str_append_str",
                                    "fr_str_from_int",
                                    "fr_str_arena_reset",
                                    "fr_str_view",
                                    "fr_str_view_len",
                                    "fr_str_view_at",
                                    "fr_str_builder",
                                    "fr_str_builder_append",
                                    "fr_str_builder_char",
                                    "fr_str_builder_finish",

                                    "fr_abs_i",
                                    "fr_abs_f",
                                    "fr_min_i",
                                    "fr_max_i",
                                    "fr_clamp_i",
                                    "fr_pow_i",
                                    "fr_time_now_ms",
                                    "fr_json_get_string",
                                    "fr_json_get_int",
                                    "fr_json_stringify_str",
                                    "fr_json_stringify_int",
                                    NULL};
  for (size_t i = 0; supported[i]; i++)
    if (!strcmp(supported[i], name_))
      return 1;
  return 0;
}
static void expression(JS *g, Expr *e) {
  if (!e) {
    fputs("undefined", g->out);
    return;
  }
  switch (e->kind) {
  case EXPR_INT:
    fprintf(g->out, "(%" PRId64 "n)", e->as.int_val);
    break;
  case EXPR_FLOAT:
    fprintf(g->out, "(%.17g)", e->as.float_val);
    break;
  case EXPR_BOOL:
    fputs(e->as.bool_val ? "true" : "false", g->out);
    break;
  case EXPR_STRING:
    quote(g->out, e->as.string_val);
    break;
  case EXPR_IDENT:
    if (!local(g, e->as.ident) && g->module.len &&
        module_fn(g->prog, g->module, e->as.ident))
      module_name(g->out, g->module, e->as.ident);
    else
      name(g->out, e->as.ident);
    break;
  case EXPR_BINARY: {
    BinOp op = e->as.binary.op;
    if (op == BIN_AND || op == BIN_OR) {
      fputs("(Boolean(", g->out);
      expression(g, e->as.binary.left);
      fputs(op == BIN_AND ? ") && Boolean(" : ") || Boolean(", g->out);
      expression(g, e->as.binary.right);
      fputs("))", g->out);
    } else {
      static const char *ops[] = {"add", "sub", "mul", "div", "mod", "eq",
                                  "ne",  "lt",  "le",  "gt",  "ge"};
      fprintf(g->out, "__forge.%s(", ops[op]);
      expression(g, e->as.binary.left);
      fputc(',', g->out);
      expression(g, e->as.binary.right);
      fputc(')', g->out);
    }
    break;
  }
  case EXPR_CALL: {
    ForgeStr fn = e->as.call.name;
    const char *standard =
        forge_std_c_name(fn, g->prog->imports, g->prog->import_count);
    if (forge_str_eq(fn, forge_str("println")))
      fputs("__forge.println", g->out);
    else if (standard) {
      if (!supported_standard(standard)) {
        fprintf(
            stderr,
            "forge: JavaScript backend does not support native function '%s'\n",
            standard);
        exit(1);
      }
      fprintf(g->out, "__forge.%s", standard);
    } else if (!local(g, fn) && g->module.len &&
               module_fn(g->prog, g->module, fn))
      module_name(g->out, g->module, fn);
    else
      name(g->out, fn);
    arguments(g, e->as.call.args, e->as.call.arg_count);
    break;
  }
  case EXPR_QUAL_CALL:
    if (!module_fn(g->prog, e->as.qual_call.module, e->as.qual_call.name)) {
      fprintf(stderr, "forge: JavaScript backend requires a source module for "
                      "qualified calls\n");
      exit(1);
    }
    module_name(g->out, e->as.qual_call.module, e->as.qual_call.name);
    arguments(g, e->as.qual_call.args, e->as.qual_call.arg_count);
    break;
  case EXPR_INDEX:
    fputs("__forge.index(", g->out);
    expression(g, e->as.index.base);
    fputc(',', g->out);
    expression(g, e->as.index.index);
    fputc(')', g->out);
    break;
  case EXPR_FIELD:
    if (e->as.field.base->kind == EXPR_IDENT &&
        !local(g, e->as.field.base->as.ident) &&
        module_fn(g->prog, e->as.field.base->as.ident, e->as.field.field))
      module_name(g->out, e->as.field.base->as.ident, e->as.field.field);
    else {
      expression(g, e->as.field.base);
      fputc('[', g->out);
      quote(g->out, e->as.field.field);
      fputc(']', g->out);
    }
    break;
  case EXPR_MOVE:
  case EXPR_RECV:
    forge_die("ownership moves and coroutine receive are unavailable in "
              "JavaScript backend");
  }
}
static void block(JS *g, Block *b) {
  size_t saved_locals = g->local_count;
  fputs("{\n", g->out);
  g->indent++;
  statements(g, b ? b->first : NULL);
  g->local_count = saved_locals;
  g->indent--;
  pad(g);
  fputc('}', g->out);
}
static void inline_stmt(JS *g, Stmt *s) {
  if (!s)
    return;
  if (s->kind == STMT_LET) {
    if (s->as.let.owned_)
      forge_die("owned native resources are unavailable in JavaScript backend");
    fputs("let ", g->out);
    name(g->out, s->as.let.name);
    fputc('=', g->out);
    if (s->as.let.init)
      expression(g, s->as.let.init);
    else
      fputs(s->as.let.type.kind == TY_STRING  ? "\"\""
            : s->as.let.type.kind == TY_BOOL  ? "false"
            : s->as.let.type.kind == TY_FLOAT ? "0"
                                              : "0n",
            g->out);
    add_local(g, s->as.let.name);
  } else if (s->kind == STMT_ASSIGN) {
    name(g->out, s->as.assign.name);
    fputc('=', g->out);
    expression(g, s->as.assign.value);
  } else
    forge_die("unsupported for-loop statement in JavaScript backend");
}
static void statements(JS *g, Stmt *s) {
  for (; s; s = s->next) {
    pad(g);
    switch (s->kind) {
    case STMT_LET:
    case STMT_ASSIGN:
      inline_stmt(g, s);
      fputs(";\n", g->out);
      break;
    case STMT_EXPR:
      expression(g, s->as.expr);
      fputs(";\n", g->out);
      break;
    case STMT_RETURN:
      fputs("return ", g->out);
      expression(g, s->as.ret);
      fputs(";\n", g->out);
      break;
    case STMT_IF:
      fputs("if (", g->out);
      expression(g, s->as.if_stmt.cond);
      fputs(") ", g->out);
      block(g, s->as.if_stmt.then_br);
      if (s->as.if_stmt.else_br) {
        fputs(" else ", g->out);
        block(g, s->as.if_stmt.else_br);
      }
      fputc('\n', g->out);
      break;
    case STMT_WHILE:
      fputs("while (", g->out);
      expression(g, s->as.while_stmt.cond);
      fputs(") ", g->out);
      block(g, s->as.while_stmt.body);
      fputc('\n', g->out);
      break;
    case STMT_FOR: {
      size_t saved_locals = g->local_count;
      fputs("for (", g->out);
      inline_stmt(g, s->as.for_stmt.init);
      fputc(';', g->out);
      expression(g, s->as.for_stmt.cond);
      fputc(';', g->out);
      inline_stmt(g, s->as.for_stmt.step);
      fputs(") ", g->out);
      block(g, s->as.for_stmt.body);
      g->local_count = saved_locals;
      fputc('\n', g->out);
      break;
    }
    case STMT_BREAK:
      fputs("break;\n", g->out);
      break;
    case STMT_CONTINUE:
      fputs("continue;\n", g->out);
      break;
    case STMT_BLOCK:
      block(g, s->as.block);
      fputc('\n', g->out);
      break;
    case STMT_MATCH: {
      unsigned id = g->match_id++;
      fprintf(g->out, "{ const __match%u=", id);
      expression(g, s->as.match_stmt.scrutinee);
      fputs(";\n", g->out);
      g->indent++;
      int first = 1;
      for (MatchArm *a = s->as.match_stmt.arms; a; a = a->next) {
        pad(g);
        if (!first)
          fputs("else ", g->out);
        if (!a->wildcard)
          fprintf(g->out, "if (__forge.eq(__match%u,(%" PRId64 "n))) ", id,
                  a->int_pat);
        else if (first)
          fputs("if (true) ", g->out);
        block(g, a->body);
        fputc('\n', g->out);
        first = 0;
        if (a->wildcard)
          break;
      }
      g->indent--;
      pad(g);
      fputs("}\n", g->out);
      break;
    }
    default:
      forge_die("process, coroutine and await statements are unavailable in "
                "JavaScript backend");
    }
  }
}
static void function(JS *g, FnDecl *fn) {
  g->local_count = 0;
  for (Param *p = fn->params; p; p = p->next)
    add_local(g, p->name);
  fputs("function ", g->out);
  if (g->module.len)
    module_name(g->out, g->module, fn->name);
  else
    name(g->out, fn->name);
  fputc('(', g->out);
  for (Param *p = fn->params; p; p = p->next) {
    if (p != fn->params)
      fputc(',', g->out);
    name(g->out, p->name);
  }
  fputs(") ", g->out);
  if (fn->is_extern) {
    fputs("{ const f=globalThis.ForgeNative?.[", g->out);
    quote(g->out, fn->name);
    fputs("]; if(typeof f!=='function') throw new Error('Missing Forge "
          "JavaScript FFI'); return f(",
          g->out);
    for (Param *p = fn->params; p; p = p->next) {
      if (p != fn->params)
        fputc(',', g->out);
      name(g->out, p->name);
    }
    fputs("); }\n", g->out);
  } else {
    block(g, &fn->body);
    fputs("\n\n", g->out);
  }
}
void codegen_emit_js(Program *prog, FILE *out) {
  if (prog->process_count || prog->supervisor_count || prog->library.present ||
      prog->struct_count)
    forge_die(
        "JavaScript backend supports functions, enums, source modules and "
        "native main; native process/struct declarations are unavailable");
  JS g = {.out = out, .prog = prog};
  fputs(
      "\"use strict\";\nconst __forge=(()=>{\n"
      "const wrap=v=>BigInt.asIntN(64,v),numeric=v=>typeof "
      "v==='boolean'?BigInt(v):v;\n"
      "const arithmetic=(a,b,op)=>{a=numeric(a);b=numeric(b);if(typeof "
      "a==='bigint'&&typeof "
      "b==='bigint'){if((op==='div'||op==='mod')&&(b===0n||(a===-"
      "9223372036854775808n&&b===-1n)))throw new RangeError('Forge integer "
      "division');return "
      "wrap(op==='add'?a+b:op==='sub'?a-b:op==='mul'?a*b:op==='div'?a/"
      "b:a%b);}a=Number(a);b=Number(b);return "
      "op==='add'?a+b:op==='sub'?a-b:op==='mul'?a*b:op==='div'?a/b:a%b;};\n"
      "const bytes=s=>new TextEncoder().encode(s),decode=b=>new "
      "TextDecoder('utf-8',{fatal:true}).decode(b);\n"
      "const eq=(a,b)=>{a=numeric(a);b=numeric(b);return typeof "
      "a==='number'&&typeof b==='bigint'||typeof a==='bigint'&&typeof "
      "b==='number'?Number(a)===Number(b):a===b;},index=(s,n)=>{if(typeof "
      "s!=='string')return s[Number(n)];const b=bytes(s),j=Number(n);return "
      "j<0||j>=b.length?-1n:BigInt(b[j]);},i=v=>BigInt(v);\n"
      "const cbytes=s=>{s=s==null?'':s;const end=s.indexOf(String.fromCharCode(0));return bytes(end<0?s:s.slice(0,end));};\n"
      "const handles=new Map();let next=1n;const store=v=>{const h=next++;handles.set(h,v);return h;};\n"
      "const view=h=>handles.get(h),builder=h=>{const b=handles.get(h);return b&&b.kind==='builder'?b:null;};\n"
      "const reserve=(b,n)=>{if(n<=b.data.length)return;let cap=Math.max(64,b.data.length);while(cap<n)cap*=2;const d=new Uint8Array(cap);d.set(b.data.subarray(0,b.len));b.data=d;};\n"
      "return {add:(a,b)=>typeof a==='string'&&typeof "
      "b==='string'?a+b:arithmetic(a,b,'add'),sub:(a,b)=>arithmetic(a,b,'sub'),"
      "mul:(a,b)=>arithmetic(a,b,'mul'),div:(a,b)=>arithmetic(a,b,'div'),mod:("
      "a,b)=>arithmetic(a,b,'mod'),eq,ne:(a,b)=>!eq(a,b),lt:(a,b)=>a<b,le:(a,b)"
      "=>a<=b,gt:(a,b)=>a>b,ge:(a,b)=>a>=b,index,println:(...v)=>console.log(v."
      "map(x=>typeof x==='boolean'?(x?'1':'0'):String(x)).join('')),\n"
      "fr_str_len:s=>i(bytes(s).length),fr_str_concat:(a,b)=>a+b,fr_str_eq:(a,"
      "b)=>i(a===b),fr_str_sub:(s,a,n)=>{if(a<0n||n<0n)return null;return "
      "decode(bytes(s).slice(Number(a),Number(a+n)));},fr_str_contains:(s,x)=>"
      "i(s.includes(x)),fr_str_trim:s=>s.trim(),fr_str_char_at:index,fr_str_"
      "append:(s,c)=>{if(c<0n||c>127n)throw new RangeError('JavaScript strings "
      "require valid UTF-8');return "
      "s+String.fromCharCode(Number(c));},fr_str_append_str:(s,t)=>s+t,fr_str_"
      "from_int:String,fr_str_arena_reset:()=>{handles.clear();return 0n;},\n"
      "fr_str_view:s=>store({kind:'view',data:cbytes(s)}),fr_str_view_len:h=>{const v=view(h);return i(v&&v.kind==='view'?v.data.length:0);},\n"
      "fr_str_view_at:(h,n)=>{const v=view(h);return !v||v.kind!=='view'||n<0n||n>=BigInt(v.data.length)?-1n:BigInt(v.data[Number(n)]);},\n"
      "fr_str_builder:()=>store({kind:'builder',data:new Uint8Array(0),len:0}),fr_str_builder_append:(h,s)=>{const b=builder(h);if(!b)return 0n;const d=cbytes(s);reserve(b,b.len+d.length);b.data.set(d,b.len);b.len+=d.length;return h;},\n"
      "fr_str_builder_char:(h,c)=>{const b=builder(h);if(!b||c<=0n||c>255n)return 0n;reserve(b,b.len+1);b.data[b.len++]=Number(c);return h;},\n"
      "fr_str_builder_finish:h=>{const b=builder(h);return b?decode(b.data.subarray(0,b.len)):null;},\n"
      "fr_abs_i:x=>x<0n?-x:x,fr_abs_f:Math.abs,fr_min_i:(a,b)=>a<b?a:b,fr_max_"
      "i:(a,b)=>a>b?a:b,fr_clamp_i:(x,a,b)=>x<a?a:x>b?b:x,fr_pow_i:(a,b)=>{if("
      "b<0n)return 0n;let "
      "r=1n;while(b>0n){if(b&1n)r=wrap(r*a);a=wrap(a*a);b>>=1n;}return "
      "r;},fr_time_now_ms:()=>i(Date.now()),\n"
      "fr_json_get_string:(s,k)=>{const v=JSON.parse(s)[k];return typeof "
      "v==='string'?v:'';},fr_json_get_int:(s,k)=>{const "
      "v=JSON.parse(s)[k];if(!Number.isSafeInteger(v))throw new "
      "RangeError('Use the lossless browser JSON module');return "
      "i(v);},fr_json_stringify_str:(k,v)=>JSON.stringify({[k]:v}),fr_json_"
      "stringify_int:(k,v)=>'{'+JSON.stringify(k)+':'+String(v)+'}'};})();\n",
      out);
  for (size_t i = 0; i < prog->const_count; i++) {
    fputs("const ", out);
    name(out, prog->consts[i].name);
    fputc('=', out);
    expression(&g, prog->consts[i].value);
    fputs(";\n", out);
  }
  for (size_t i = 0; i < prog->enum_count; i++) {
    fputs("const ", out);
    name(out, prog->enums[i].name);
    fputs("=Object.freeze({", out);
    for (EnumVariant *v = prog->enums[i].variants; v; v = v->next) {
      if (v != prog->enums[i].variants)
        fputc(',', out);
      quote(out, v->name);
      fprintf(out, ":(%" PRId64 "n)", v->value);
    }
    fputs("});\n", out);
  }
  for (size_t i = 0; i < prog->fn_count; i++)
    function(&g, &prog->functions[i]);
  for (size_t i = 0; i < prog->module_count; i++) {
    g.module = prog->modules[i].name;
    for (size_t j = 0; j < prog->modules[i].fn_count; j++)
      function(&g, &prog->modules[i].functions[j]);
  }
  g.module = forge_str("");
  g.local_count=0;
  for (size_t i = 0; i < prog->native_count; i++)
    if (forge_str_eq(prog->natives[i].name, forge_str("main"))) {
      fputs("function __forge_main() ", out);
      block(&g, &prog->natives[i].body);
      fputs("\n__forge_main();\n", out);
    }
}
