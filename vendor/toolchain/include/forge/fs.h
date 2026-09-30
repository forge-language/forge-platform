#ifndef FORGE_FS_H
#define FORGE_FS_H

#include <stdint.h>

char *fr_fs_read(const char *path);
int fr_fs_write(const char *path, const char *content);
int fr_fs_append(const char *path, const char *content);
int fr_fs_exists(const char *path);
int fr_fs_remove(const char *path);
int64_t fr_fs_size(const char *path);
int fr_fs_is_file(const char *path);
int fr_fs_is_dir(const char *path);
int fr_fs_mkdir(const char *path);
int fr_fs_rename(const char *old_path, const char *new_path);
int fr_fs_copy(const char *src, const char *dst);
char *fr_fs_list_dir(const char *path);

#endif
