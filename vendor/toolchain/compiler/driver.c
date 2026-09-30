#define _GNU_SOURCE
#include "driver.h"
#include "codegen.h"
#include "forge/platform.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !defined(FORGE_OS_WINDOWS)
#include <sys/wait.h>
#include <unistd.h>
#define FORGE_HAVE_UNISTD 1
#else
#include <process.h>
#define PATH_MAX MAX_PATH
#endif

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} Argv;

static void argv_init(Argv *a) {
    a->items = NULL;
    a->count = 0;
    a->cap = 0;
}

static void argv_push(Argv *a, char *s) {
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 32;
        a->items = (char **)realloc(a->items, a->cap * sizeof(char *));
        if (!a->items) forge_die("out of memory");
    }
    a->items[a->count++] = s;
}

static void argv_free(Argv *a) {
    free(a->items);
    a->items = NULL;
    a->count = a->cap = 0;
}

static void argv_add_opt(Argv *a, int opt_level) {
    char opt[8];
    snprintf(opt, sizeof(opt), "-O%d", opt_level < 0 ? 2 : (opt_level > 3 ? 3 : opt_level));
    argv_push(a, strdup(opt));
}

static void argv_add_lto(Argv *a) {
#if !defined(FORGE_OS_WINDOWS)
    argv_push(a, strdup("-flto"));
#endif
}

static void argv_add_includes(Argv *a, const ForgeDriverConfig *cfg) {
    if (cfg->include_dir) {
        size_t n = strlen(cfg->include_dir) + 3;
        char *inc = (char *)malloc(n);
        snprintf(inc, n, "-I%s", cfg->include_dir);
        argv_push(a, inc);
    }
    for (size_t i = 0; i < cfg->extra_include_count; i++) {
        size_t n = strlen(cfg->extra_includes[i]) + 3;
        char *inc = (char *)malloc(n);
        snprintf(inc, n, "-I%s", cfg->extra_includes[i]);
        argv_push(a, inc);
    }
}

static int exec_argv(Argv *a) {
    a->items = (char **)realloc(a->items, (a->count + 1) * sizeof(char *));
    a->items[a->count] = NULL;
#if defined(FORGE_OS_WINDOWS)
    int rc = _spawnvp(_P_WAIT, a->items[0], a->items);
    return rc < 0 ? 1 : rc;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "forge: fork failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        execvp(a->items[0], a->items);
        fprintf(stderr, "forge: failed to run '%s': %s\n", a->items[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "forge: waitpid failed: %s\n", strerror(errno));
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }
    return 0;
#endif
}

static int compile_c_source(Program *prog, const char *obj_path, const ForgeDriverConfig *cfg,
                            void (*emit)(Program *, FILE *, const char *)) {
    char cpath[PATH_MAX];
    if (fr_make_temp_path(cpath, sizeof(cpath), "forge", ".c") != 0) {
        fprintf(stderr, "forge: cannot create temp file\n");
        return 1;
    }
    FILE *out = fopen(cpath, "w");
    if (!out) {
        remove(cpath);
        return 1;
    }
    emit(prog, out, "forge_runtime.h");
    fclose(out);

    Argv args;
    argv_init(&args);
    argv_push(&args, (char *)cfg->cc);
    argv_push(&args, "-std=c11");
    argv_add_opt(&args, cfg->opt_level > 0 ? cfg->opt_level : 3);
    if (cfg->opt_level > 0 && getenv("FORGE_ENABLE_LTO")) argv_add_lto(&args);
    argv_add_includes(&args, cfg);
    argv_push(&args, "-c");
    argv_push(&args, cpath);
    argv_push(&args, "-o");
    argv_push(&args, (char *)obj_path);
    int rc = exec_argv(&args);
    argv_free(&args);
    if (!cfg->keep_intermediate) remove(cpath);
    if (rc != 0) fprintf(stderr, "forge: native compilation failed\n");
    return rc;
}

static void emit_program(Program *prog, FILE *out, const char *runtime_include) {
    codegen_emit(prog, out, runtime_include);
}

static void argv_add_link_extras(Argv *a, const ForgeDriverConfig *cfg) {
    if (!cfg->lib_dir) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/forge.link", cfg->lib_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';
        if (!p[0] || p[0] == '#') continue;
        char *save = NULL;
        for (char *tok = strtok_r(p, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save))
            argv_push(a, strdup(tok));
    }
    fclose(f);
}

static int link_object(const char *obj_path, const char *output_path, const ForgeDriverConfig *cfg) {
    Argv args;
    argv_init(&args);
    argv_push(&args, (char *)cfg->cc);
    argv_push(&args, (char *)obj_path);
    argv_push(&args, "-o");
    argv_push(&args, (char *)output_path);
    if (cfg->lib_dir) {
        size_t n = strlen(cfg->lib_dir) + 3;
        char *libdir = (char *)malloc(n);
        snprintf(libdir, n, "-L%s", cfg->lib_dir);
        argv_push(&args, libdir);
    }
    for (size_t i = 0; i < cfg->link_lib_count; i++) {
        size_t n = strlen(cfg->link_libs[i]) + 3;
        char *lib = (char *)malloc(n);
        snprintf(lib, n, "-l%s", cfg->link_libs[i]);
        argv_push(&args, lib);
    }
    argv_push(&args, "-lforge_std");
    argv_push(&args, "-lforge_runtime");
    argv_add_link_extras(&args, cfg);
    argv_push(&args, "-lm");
    int rc = exec_argv(&args);
    argv_free(&args);
    return rc;
}

#if defined(FORGE_HAVE_UNISTD)
static bool command_in_path(const char *cmd) {
    const char *path = getenv("PATH");
    if (!path || !cmd || !cmd[0]) return false;

    char *path_copy = strdup(path);
    if (!path_copy) return false;

    bool found = false;
    char *save = NULL;
    for (char *dir = strtok_r(path_copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        size_t len = strlen(dir) + strlen(cmd) + 2;
        char *full = (char *)malloc(len);
        if (!full) continue;
        snprintf(full, len, "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) found = true;
        free(full);
        if (found) break;
    }
    free(path_copy);
    return found;
}
#endif

static const char *detect_default_cc(void) {
    const char *cc = getenv("CC");
    if (cc && cc[0]) return cc;

#if defined(FORGE_HAVE_UNISTD)
    static const char *candidates[] = {"clang", "gcc", "cc", NULL};
    for (size_t i = 0; candidates[i]; i++) {
        if (command_in_path(candidates[i])) return candidates[i];
    }
#endif
    return "cc";
}

void forge_driver_config_init(ForgeDriverConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->cc = detect_default_cc();
    cfg->opt_level = 3;
}

void forge_driver_detect_paths(ForgeDriverConfig *cfg, const char *argv0) {
    static char rootbuf[PATH_MAX];
    static char libbuf[PATH_MAX];
    static char incbuf[PATH_MAX];

    const char *root = getenv("FORGE_ROOT");
    if (root && root[0]) {
        cfg->forge_root = root;
    } else if (argv0 && argv0[0]) {
#if defined(FORGE_OS_WINDOWS)
        char resolved[PATH_MAX];
        DWORD n = GetModuleFileNameA(NULL, resolved, (DWORD)sizeof(resolved));
        if (n > 0 && n < sizeof(resolved)) {
            char *slash = strstr(resolved, "\\build\\bin\\");
            if (!slash) slash = strstr(resolved, "/build/bin/");
            if (slash) {
                *slash = '\0';
                snprintf(rootbuf, sizeof(rootbuf), "%s", resolved);
                fr_path_normalize(rootbuf);
                cfg->forge_root = rootbuf;
            }
        }
#else
        char resolved[PATH_MAX];
        if (realpath(argv0, resolved)) {
            char *slash = strstr(resolved, "/build/bin/");
            if (slash) {
                *slash = '\0';
                snprintf(rootbuf, sizeof(rootbuf), "%s", resolved);
                cfg->forge_root = rootbuf;
            }
        }
#endif
    }
    if (!cfg->forge_root) cfg->forge_root = ".";

    if (!cfg->lib_dir) {
        snprintf(libbuf, sizeof(libbuf), "%s/build/lib", cfg->forge_root);
        if (fr_path_exists(libbuf)) cfg->lib_dir = libbuf;
    }
    if (!cfg->include_dir) {
        snprintf(incbuf, sizeof(incbuf), "%s/include", cfg->forge_root);
        if (fr_path_exists(incbuf)) cfg->include_dir = incbuf;
    }
}

static bool output_is_c(const char *path) {
    if (!path) return false;
    size_t n = strlen(path);
    return n > 2 && strcmp(path + n - 2, ".c") == 0;
}

int forge_driver_compile_program(Program *prog, const char *output_path, const ForgeDriverConfig *cfg) {
    if (cfg->emit_c_only || output_is_c(output_path)) {
        FILE *out = stdout;
        if (output_path) {
            out = fopen(output_path, "w");
            if (!out) {
                fprintf(stderr, "forge: cannot write '%s'\n", output_path);
                return 1;
            }
        }
        codegen_emit(prog, out, "forge_runtime.h");
        if (output_path) fclose(out);
        return 0;
    }

    if (!output_path) {
        fprintf(stderr, "forge: native output requires -o <binary>\n");
        return 1;
    }
    if (!cfg->lib_dir) {
        fprintf(stderr, "forge: cannot find runtime libraries; set FORGE_ROOT or pass --lib-dir\n");
        return 1;
    }

    char obj_path[PATH_MAX];
    if (fr_make_temp_path(obj_path, sizeof(obj_path), "forge", ".o") != 0) return 1;

    if (compile_c_source(prog, obj_path, cfg, emit_program) != 0) {
        remove(obj_path);
        return 1;
    }
    int rc = link_object(obj_path, output_path, cfg);
    if (!cfg->keep_intermediate) remove(obj_path);
    return rc;
}

static void emit_library_c(Program *prog, FILE *out, const char *runtime_include) {
    codegen_emit_library(prog, out, NULL, runtime_include);
}

int forge_driver_compile_library(Program *prog, const char *output_a, const char *output_h,
                                 const ForgeDriverConfig *cfg) {
    if (cfg->emit_c_only || (output_a && output_is_c(output_a))) {
        FILE *out_c = fopen(output_a, "w");
        FILE *out_h = fopen(output_h, "w");
        if (!out_c || !out_h) {
            fprintf(stderr, "forge: cannot write library output\n");
            return 1;
        }
        codegen_emit_library(prog, out_c, out_h, "forge_runtime.h");
        fclose(out_c);
        fclose(out_h);
        return 0;
    }

    if (!output_a || !output_h) {
        fprintf(stderr, "forge: library mode requires -o <archive.a> and --header <file.h>\n");
        return 1;
    }

    FILE *out_h = fopen(output_h, "w");
    if (!out_h) return 1;
    codegen_emit_library(prog, NULL, out_h, "forge_runtime.h");
    fclose(out_h);

    char obj_path[PATH_MAX];
    if (fr_make_temp_path(obj_path, sizeof(obj_path), "forge-lib", ".o") != 0) return 1;

    if (compile_c_source(prog, obj_path, cfg, emit_library_c) != 0) {
        remove(obj_path);
        return 1;
    }

    Argv args;
    argv_init(&args);
    argv_push(&args, "ar");
    argv_push(&args, "rcs");
    argv_push(&args, (char *)output_a);
    argv_push(&args, obj_path);
    int rc = exec_argv(&args);
    argv_free(&args);
    if (!cfg->keep_intermediate) remove(obj_path);
    return rc;
}
