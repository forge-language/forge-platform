#ifndef FORGE_IO_H
#define FORGE_IO_H

#include <stdint.h>

void fr_print_int(int64_t value);
void fr_print_str(const char *value);
void fr_println(void);
void fr_print(const char *value);
void fr_eprint(const char *value);
void fr_eprintln(const char *value);

void fr_io_eprint_int(int64_t value);
void fr_io_flush(void);
void fr_io_flush_err(void);

char *fr_io_read_line(void);
int64_t fr_io_read_char(void);
char *fr_io_read_stdin(void);
char *fr_io_prompt(const char *msg);

int64_t fr_io_write_fd(int64_t fd, const char *text);
char *fr_io_read_fd(int64_t fd, int64_t max_bytes);

int64_t fr_io_stdin_fd(void);
int64_t fr_io_stdout_fd(void);
int64_t fr_io_stderr_fd(void);

#endif
