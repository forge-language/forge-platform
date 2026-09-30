#include "symbols.h"
#include <stdio.h>

static void print_str(FILE *out, ForgeStr s) {
    fputc('"', out);
    for (size_t i = 0; i < s.len; i++) {
        char c = s.data[i];
        if (c == '"' || c == '\\') fputc('\\', out);
        fputc(c, out);
    }
    fputc('"', out);
}

static void emit_symbol(FILE *out, bool *first, const char *kind, ForgeStr name) {
    if (!*first) fputs(",", out);
    *first = false;
    fputs("\n  {\"kind\":", out);
    print_str(out, forge_str(kind));
    fputs(",\"name\":", out);
    print_str(out, name);
    fputs("}", out);
}

void forge_emit_symbols_json(const Program *prog, FILE *out) {
    bool first = true;
    fputs("[", out);
    for (size_t i = 0; i < prog->const_count; i++)
        emit_symbol(out, &first, "const", prog->consts[i].name);
    for (size_t i = 0; i < prog->struct_count; i++)
        emit_symbol(out, &first, "struct", prog->structs[i].name);
    for (size_t i = 0; i < prog->enum_count; i++)
        emit_symbol(out, &first, "enum", prog->enums[i].name);
    for (size_t i = 0; i < prog->fn_count; i++)
        emit_symbol(out, &first, "function", prog->functions[i].name);
    for (size_t i = 0; i < prog->process_count; i++)
        emit_symbol(out, &first, "process", prog->processes[i].name);
    for (size_t i = 0; i < prog->supervisor_count; i++)
        emit_symbol(out, &first, "supervisor", prog->supervisors[i].name);
    for (size_t i = 0; i < prog->native_count; i++)
        emit_symbol(out, &first, "native", prog->natives[i].name);
    if (prog->library.present) {
        emit_symbol(out, &first, "library", prog->library.name);
        for (size_t i = 0; i < prog->library.fn_count; i++)
            emit_symbol(out, &first, "function", prog->library.functions[i].name);
    }
    for (size_t i = 0; i < prog->module_count; i++) {
        for (size_t j = 0; j < prog->modules[i].fn_count; j++) {
            if (!first) fputs(",", out);
            first = false;
            fputs("\n  {\"kind\":\"function\",\"name\":", out);
            print_str(out, prog->modules[i].functions[j].name);
            fputs(",\"container\":", out);
            print_str(out, prog->modules[i].name);
            fputs("}", out);
        }
    }
  for (size_t i = 0; i < prog->process_count; i++) {
        ProcessDecl *pd = &prog->processes[i];
        for (size_t j = 0; j < pd->coro_count; j++)
            emit_symbol(out, &first, "coroutine", pd->coros[j].name);
    }
    fputs("\n]\n", out);
}
