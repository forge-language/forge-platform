#ifndef FORGE_OS_H
#define FORGE_OS_H

#include <stdint.h>

void fr_os_set_args(int argc, char **argv);
void fr_os_exit(int64_t code);
const char *fr_os_getenv(const char *name);
int64_t fr_os_argc(void);
const char *fr_os_argv(int64_t index);

#endif
