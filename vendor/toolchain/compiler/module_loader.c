#define _GNU_SOURCE
#include "module_loader.h"
#include "lexer.h"
#include "mod_registry.h"
#include "parser.h"
#include "forge/platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char **paths;
    size_t count;
    size_t cap;
} LoadedPaths;

typedef struct {
    const ForgeModuleConfig *cfg;
    char entry_dir[PATH_MAX];
    LoadedPaths loaded;
} ModuleResolver;

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) forge_die("out of memory");
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

static void dirname_of(const char *path, char *out, size_t cap) {
    strncpy(out, path, cap - 1);
    out[cap - 1] = '\0';
    char *slash = strrchr(out, '/');
#if defined(FORGE_OS_WINDOWS)
    char *bslash = strrchr(out, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = '\0';
    else strcpy(out, ".");
}

static ForgeStr basename_module(ForgeStr path) {
    const char *start = path.data;
    for (size_t i = 0; i < path.len; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') start = path.data + i + 1;
    }
    size_t len = path.len - (size_t)(start - path.data);
    if (len > 3 && start[len - 3] == '.' && start[len - 2] == 'f' && start[len - 1] == 'g')
        len -= 3;
    ForgeStr r = { (char *)start, len };
    return r;
}

static bool path_already_loaded(ModuleResolver *r, const char *path) {
    for (size_t i = 0; i < r->loaded.count; i++) {
        if (strcmp(r->loaded.paths[i], path) == 0) return true;
    }
    return false;
}

static void mark_loaded(ModuleResolver *r, const char *path) {
    if (r->loaded.count == r->loaded.cap) {
        r->loaded.cap = r->loaded.cap ? r->loaded.cap * 2 : 8;
        r->loaded.paths = (char **)realloc(r->loaded.paths, r->loaded.cap * sizeof(char *));
        if (!r->loaded.paths) forge_die("out of memory");
    }
    r->loaded.paths[r->loaded.count++] = strdup(path);
}

static bool try_candidate(const char *path, char *out, size_t cap) {
    if (fr_path_exists(path)) {
        strncpy(out, path, cap - 1);
        out[cap - 1] = '\0';
        return true;
    }
    return false;
}

static bool is_prebuilt_lib(const char *lib_dir, ForgeStr name) {
    if (!lib_dir) return false;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/libforge_%.*s.a", lib_dir, (int)name.len, name.data);
    return fr_path_exists(path) != 0;
}

static bool resolve_module_path(ModuleResolver *r, ForgeStr name, ForgeStr rel_path,
                              char *out, size_t cap) {
    char candidate[PATH_MAX];
    const char *bases[257];
    size_t base_count = 0;
    bases[base_count++] = r->entry_dir;
    for (size_t i = 0; i < r->cfg->include_dir_count && base_count < 257; i++)
        bases[base_count++] = r->cfg->include_dirs[i];

    if (rel_path.len > 0) {
        for (size_t i = 0; i < base_count; i++) {
            snprintf(candidate, sizeof(candidate), "%s/%.*s", bases[i], (int)rel_path.len, rel_path.data);
            if (try_candidate(candidate, out, cap)) return true;
        }
        if (rel_path.len > 0 && (rel_path.data[0] == '/' || strchr(rel_path.data, ':')))
            return try_candidate(rel_path.data, out, cap);
        return false;
    }

    for (size_t i = 0; i < base_count; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%.*s.fg", bases[i], (int)name.len, name.data);
        if (try_candidate(candidate, out, cap)) return true;
        snprintf(candidate, sizeof(candidate), "%s/%.*s/%.*s.fg",
                 bases[i], (int)name.len, name.data, (int)name.len, name.data);
        if (try_candidate(candidate, out, cap)) return true;
    }
    return false;
}

static bool import_present(ForgeStr *imports, size_t count, ForgeStr name) {
    for (size_t i = 0; i < count; i++) {
        if (forge_str_eq(imports[i], name)) return true;
    }
    return false;
}

static void merge_import(Program *prog, ForgeStr name) {
    if (import_present(prog->imports, prog->import_count, name)) return;
    prog->import_count++;
    prog->imports = (ForgeStr *)realloc(prog->imports, prog->import_count * sizeof(ForgeStr));
    prog->imports[prog->import_count - 1] = name;
}

static FileModule *find_module(Program *prog, ForgeStr name) {
    for (size_t i = 0; i < prog->module_count; i++) {
        if (forge_str_eq(prog->modules[i].name, name)) return &prog->modules[i];
    }
    return NULL;
}

static void merge_program_decls(Program *dst, Program *src) {
    for (size_t i = 0; i < src->const_count; i++) {
        dst->const_count++;
        dst->consts = (ConstDecl *)realloc(dst->consts, dst->const_count * sizeof(ConstDecl));
        dst->consts[dst->const_count - 1] = src->consts[i];
    }
    src->const_count = 0;
    free(src->consts);
    src->consts = NULL;

    for (size_t i = 0; i < src->struct_count; i++) {
        dst->struct_count++;
        dst->structs = (StructDecl *)realloc(dst->structs, dst->struct_count * sizeof(StructDecl));
        dst->structs[dst->struct_count - 1] = src->structs[i];
    }
    src->struct_count = 0;
    free(src->structs);
    src->structs = NULL;

    for (size_t i = 0; i < src->enum_count; i++) {
        dst->enum_count++;
        dst->enums = (EnumDecl *)realloc(dst->enums, dst->enum_count * sizeof(EnumDecl));
        dst->enums[dst->enum_count - 1] = src->enums[i];
    }
    src->enum_count = 0;
    free(src->enums);
    src->enums = NULL;
}

static void load_module_file(ModuleResolver *r, Program *prog, ForgeStr name, ForgeStr rel_path);

static void load_parsed_module(ModuleResolver *r, Program *prog, ForgeStr name,
                               const char *resolved_path, Program *mod, char *src) {
    if (mod->library.present)
        fprintf(stderr, "forge: module '%.*s' cannot contain a library block\n",
                (int)name.len, name.data), exit(1);
    if (mod->process_count > 0)
        fprintf(stderr, "forge: module '%.*s' cannot contain a process\n",
                (int)name.len, name.data), exit(1);
    if (mod->native_count > 0)
        fprintf(stderr, "forge: module '%.*s' cannot contain native blocks\n",
                (int)name.len, name.data), exit(1);

    for (size_t i = 0; i < mod->import_count; i++) {
        ForgeStr imp = mod->imports[i];
        if (forge_import_is_stdlib(imp) || is_prebuilt_lib(r->cfg->lib_dir, imp)) {
            merge_import(prog, imp);
            continue;
        }
        load_module_file(r, prog, imp, forge_str(""));
    }

    for (size_t i = 0; i < mod->path_import_count; i++)
        load_module_file(r, prog, basename_module(mod->path_imports[i]), mod->path_imports[i]);

    FileModule *fm = find_module(prog, name);
    if (!fm) {
        prog->module_count++;
        prog->modules = (FileModule *)realloc(prog->modules, prog->module_count * sizeof(FileModule));
        fm = &prog->modules[prog->module_count - 1];
        fm->name = name;
        fm->path = strdup(resolved_path);
        fm->source = src;
        fm->functions = NULL;
        fm->fn_count = 0;
    } else {
        free(src);
    }

    for (size_t i = 0; i < mod->fn_count; i++) {
        if (mod->functions[i].is_extern) {
            prog->fn_count++;
            prog->functions = (FnDecl *)realloc(prog->functions, prog->fn_count * sizeof(FnDecl));
            if (!prog->functions) forge_die("out of memory");
            prog->functions[prog->fn_count - 1] = mod->functions[i];
            continue;
        }
        fm->fn_count++;
        fm->functions = (FnDecl *)realloc(fm->functions, fm->fn_count * sizeof(FnDecl));
        fm->functions[fm->fn_count - 1] = mod->functions[i];
    }
    mod->fn_count = 0;
    free(mod->functions);
    mod->functions = NULL;

    merge_program_decls(prog, mod);
}

static void load_module_file(ModuleResolver *r, Program *prog, ForgeStr name, ForgeStr rel_path) {
    if (find_module(prog, name)) return;

    char resolved[PATH_MAX];
    if (!resolve_module_path(r, name, rel_path, resolved, sizeof(resolved))) {
        fprintf(stderr, "forge: cannot find module '%.*s'", (int)name.len, name.data);
        if (rel_path.len > 0)
            fprintf(stderr, " (%.*s)", (int)rel_path.len, rel_path.data);
        fprintf(stderr, "\n");
        exit(1);
    }
    if (path_already_loaded(r, resolved)) return;
    mark_loaded(r, resolved);

    size_t len = 0;
    char *src = read_file(resolved, &len);
    if (!src) {
        fprintf(stderr, "forge: cannot read module '%s'\n", resolved);
        exit(1);
    }

    Lexer lx;
    lexer_init(&lx, src, len);
    Program mod = parse_program(&lx);

    load_parsed_module(r, prog, name, resolved, &mod, src);
    program_free(&mod);
}

static ForgeStr *filter_imports(Program *prog, const ForgeModuleConfig *cfg, size_t *out_count) {
    ForgeStr *kept = NULL;
    size_t kept_count = 0;
    for (size_t i = 0; i < prog->import_count; i++) {
        ForgeStr imp = prog->imports[i];
        if (forge_import_is_stdlib(imp) || is_prebuilt_lib(cfg->lib_dir, imp)) {
            kept_count++;
            kept = (ForgeStr *)realloc(kept, kept_count * sizeof(ForgeStr));
            kept[kept_count - 1] = imp;
            continue;
        }
        if (find_module(prog, imp)) continue;
        kept_count++;
        kept = (ForgeStr *)realloc(kept, kept_count * sizeof(ForgeStr));
        kept[kept_count - 1] = imp;
    }
    *out_count = kept_count;
    return kept;
}

void forge_load_modules(Program *prog, const ForgeModuleConfig *cfg) {
    ModuleResolver r = { .cfg = cfg };
    dirname_of(cfg->entry_path, r.entry_dir, sizeof(r.entry_dir));

    for (size_t i = 0; i < prog->import_count; i++) {
        ForgeStr imp = prog->imports[i];
        if (forge_import_is_stdlib(imp) || is_prebuilt_lib(cfg->lib_dir, imp)) continue;
        char resolved[PATH_MAX];
        if (resolve_module_path(&r, imp, forge_str(""), resolved, sizeof(resolved)))
            load_module_file(&r, prog, imp, forge_str(""));
    }

    for (size_t i = 0; i < prog->path_import_count; i++) {
        ForgeStr path = prog->path_imports[i];
        load_module_file(&r, prog, basename_module(path), path);
    }

    size_t kept_count = 0;
    ForgeStr *kept = filter_imports(prog, cfg, &kept_count);
    free(prog->imports);
    prog->imports = kept;
    prog->import_count = kept_count;

    for (size_t i = 0; i < r.loaded.count; i++) free(r.loaded.paths[i]);
    free(r.loaded.paths);
}
