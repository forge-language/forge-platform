#ifndef FORGE_MODULE_LOADER_H
#define FORGE_MODULE_LOADER_H

#include "ast.h"
#include <stddef.h>

typedef struct {
    const char *entry_path;
    const char *lib_dir;
    const char **include_dirs;
    size_t include_dir_count;
} ForgeModuleConfig;

void forge_load_modules(Program *prog, const ForgeModuleConfig *cfg);

#endif
