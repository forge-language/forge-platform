#ifndef FORGE_DRIVER_H
#define FORGE_DRIVER_H

#include "ast.h"
#include <stdbool.h>

typedef struct {
    const char *forge_root;
    const char *lib_dir;
    const char *include_dir;
    const char *cc;
    const char **extra_includes;
    size_t extra_include_count;
    const char **link_libs;
    size_t link_lib_count;
    bool emit_c_only;
    bool keep_intermediate;
    int opt_level;
} ForgeDriverConfig;

void forge_driver_config_init(ForgeDriverConfig *cfg);
void forge_driver_detect_paths(ForgeDriverConfig *cfg, const char *argv0);
int forge_driver_compile_program(Program *prog, const char *output_path, const ForgeDriverConfig *cfg);
int forge_driver_compile_library(Program *prog, const char *output_a, const char *output_h,
                               const ForgeDriverConfig *cfg);

#endif
