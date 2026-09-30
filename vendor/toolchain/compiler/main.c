#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "lexer.h"
#include "parser.h"
#include "codegen.h"
#include "optimize.h"
#include "module_loader.h"
#include "driver.h"
#include "symbols.h"

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "forge: cannot open '%s'\n", path);
        exit(1);
    }
    if (fseek(f, 0, SEEK_END) != 0) forge_die("cannot seek input file");
    long sz = ftell(f);
    if (sz < 0 || (uintmax_t)sz >= SIZE_MAX) forge_die("invalid input file size");
    if (fseek(f, 0, SEEK_SET) != 0) forge_die("cannot seek input file");
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) forge_die("out of memory");
    size_t n = fread(buf, 1, (size_t)sz, f);
    if (ferror(f)) forge_die("cannot read input file");
    buf[n] = '\0';
    fclose(f);
    *out_len = n;
    return buf;
}

static void usage(const char *prog) {
    fprintf(stderr, "Forge %s - AOT native compiler\n", FORGE_VERSION);
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <input.fg> -o <binary>          Compile directly to native executable\n", prog);
    fprintf(stderr, "  %s <input.fg> -o <output.c> --emit-c   Emit C source only\n", prog);
    fprintf(stderr, "  %s --lib <input.fg> -o <lib.a> --header <lib.h>\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --emit-js          Emit JavaScript for browser/native-JS FFI\n");
    fprintf(stderr, "  --emit-c           Emit C instead of a native binary\n");
    fprintf(stderr, "  --forge-root PATH  Project root (include/, build/lib)\n");
    fprintf(stderr, "  --lib-dir PATH     Directory containing libforge_*.a\n");
    fprintf(stderr, "  -I PATH            Extra include directory (also searches for .fg modules)\n");
    fprintf(stderr, "  -l NAME             Link libforge_NAME.a (repeatable)\n");
    fprintf(stderr, "  --cc PATH          C compiler for native output (default: CC, clang, gcc, or cc)\n");
    fprintf(stderr, "  --check            Parse only; exit 0 on success (for LSP / CI)\n");
    fprintf(stderr, "  --symbols-json     Print document symbols as JSON to stdout\n");
    fprintf(stderr, "  --keep-temp        Keep intermediate object files\n");
}

static const char *option_value(int argc, char **argv, int *index) {
    if (*index + 1 >= argc || argv[*index + 1][0] == '-') {
        fprintf(stderr, "forge: option '%s' requires a value\n", argv[*index]);
        exit(1);
    }
    return argv[++*index];
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    bool emit_js = false;
    bool lib_mode = false;
    bool check_only = false;
    bool symbols_json = false;
    const char *input = NULL;
    const char *output = NULL;
    const char *header = NULL;

    ForgeDriverConfig cfg;
    forge_driver_config_init(&cfg);
    forge_driver_detect_paths(&cfg, argv[0]);

    const char *includes[256];
    const char *link_libs[32];
    size_t include_count = 0;
    size_t link_lib_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("forge %s\n", FORGE_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--lib") == 0) {
            lib_mode = true;
        } else if (strcmp(argv[i], "-o") == 0) {
            output = option_value(argc, argv, &i);
        } else if (strcmp(argv[i], "--header") == 0) {
            header = option_value(argc, argv, &i);
        } else if (strcmp(argv[i], "--emit-js") == 0) {
            emit_js = true;
        } else if (strcmp(argv[i], "--emit-c") == 0) {
            cfg.emit_c_only = true;
        } else if (strcmp(argv[i], "--forge-root") == 0) {
            cfg.forge_root = option_value(argc, argv, &i);
            forge_driver_detect_paths(&cfg, argv[0]);
        } else if (strcmp(argv[i], "--lib-dir") == 0) {
            cfg.lib_dir = option_value(argc, argv, &i);
        } else if (strcmp(argv[i], "--cc") == 0) {
            cfg.cc = option_value(argc, argv, &i);
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else if (strcmp(argv[i], "--symbols-json") == 0) {
            symbols_json = true;
        } else if (strcmp(argv[i], "--keep-temp") == 0) {
            cfg.keep_intermediate = true;
        } else if (strcmp(argv[i], "-I") == 0) {
            if (include_count == sizeof(includes) / sizeof(includes[0]))
                forge_die("too many include directories (maximum 256)");
            includes[include_count++] = option_value(argc, argv, &i);
        } else if (strcmp(argv[i], "-l") == 0) {
            if (link_lib_count == sizeof(link_libs) / sizeof(link_libs[0]))
                forge_die("too many link libraries (maximum 32)");
            link_libs[link_lib_count++] = option_value(argc, argv, &i);
        } else if (argv[i][0] != '-') {
            if (input) forge_die("multiple input files are not supported");
            input = argv[i];
        } else {
            fprintf(stderr, "forge: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    cfg.extra_includes = includes;
    cfg.extra_include_count = include_count;
    cfg.link_libs = link_libs;
    cfg.link_lib_count = link_lib_count;

    if (!input) {
        usage(argv[0]);
        return 1;
    }
    if (lib_mode && !header) {
        fprintf(stderr, "forge: library mode requires --header\n");
        return 1;
    }

    size_t len = 0;
    char *src = read_file(input, &len);

    Lexer lx;
    lexer_init(&lx, src, len);
    Program prog = parse_program(&lx);

    ForgeModuleConfig mcfg = {
        .entry_path = input,
        .lib_dir = cfg.lib_dir,
        .include_dirs = includes,
        .include_dir_count = include_count,
    };
    forge_load_modules(&prog, &mcfg);
    optimize_program(&prog);

    if (symbols_json) {
        forge_emit_symbols_json(&prog, stdout);
        program_free(&prog);
        free(src);
        return 0;
    }
    if (check_only) {
        program_free(&prog);
        free(src);
        return 0;
    }

    int rc = 0;
    if (emit_js) {
        if (lib_mode || cfg.emit_c_only || !output || link_lib_count) forge_die("--emit-js requires -o and cannot be combined with native library options");
        FILE *out_js = fopen(output, "wb");
        if (!out_js) forge_die("cannot open JavaScript output");
        codegen_emit_js(&prog, out_js);
        if (fclose(out_js) != 0) forge_die("cannot write JavaScript output");
    } else if (lib_mode) {
        if (!output) {
            fprintf(stderr, "forge: library mode requires -o\n");
            rc = 1;
        } else {
            rc = forge_driver_compile_library(&prog, output, header, &cfg);
        }
    } else {
        rc = forge_driver_compile_program(&prog, output, &cfg);
    }

    program_free(&prog);
    free(src);
    return rc;
}
