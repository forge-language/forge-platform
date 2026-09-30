#ifndef FORGE_GPU_H
#define FORGE_GPU_H

#include <stdint.h>

/* Device discovery */
int64_t fr_gpu_available(void);
const char *fr_gpu_backend(void);
int64_t fr_gpu_device_count(void);
const char *fr_gpu_device_name(int64_t device_id);
int64_t fr_gpu_select_device(int64_t device_id);

/* Buffer management (handles are small integers, like TCP sockets) */
int64_t fr_gpu_alloc(int64_t bytes);
void fr_gpu_free(int64_t buffer);
int64_t fr_gpu_copy(int64_t dst, int64_t src, int64_t bytes);
void fr_gpu_sync(void);

/* Host ↔ device int32 arrays */
int64_t fr_gpu_fill_i32(int64_t buffer, int64_t value, int64_t count);
int64_t fr_gpu_write_i32(int64_t buffer, int64_t index, int64_t value);
int64_t fr_gpu_read_i32(int64_t buffer, int64_t index);

/* Built-in element-wise kernels */
int64_t fr_gpu_add_i32(int64_t a, int64_t b, int64_t out, int64_t count);
int64_t fr_gpu_mul_i32(int64_t a, int64_t b, int64_t out, int64_t count);

/* Custom OpenCL kernel: three int32 buffers + element count */
int64_t fr_gpu_run_kernel(const char *source, const char *kernel_name,
                          int64_t arg0, int64_t arg1, int64_t arg2, int64_t count);

#endif
