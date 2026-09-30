#include "forge/gpu.h"
#include "forge/arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(FORGE_HAS_OPENCL)
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#define GPU_MAX_BUFFERS 256
#define GPU_MAX_DEVICES 16

typedef struct {
    cl_mem mem;
    size_t bytes;
    int in_use;
} gpu_buffer_t;

static cl_context g_context;
static cl_command_queue g_queue;
static cl_device_id g_device;
static cl_device_id g_devices[GPU_MAX_DEVICES];
static cl_program g_builtins;
static int g_device_count;
static int g_selected;
static int g_ready;
static gpu_buffer_t g_buffers[GPU_MAX_BUFFERS];

static const char *GPU_BUILTIN_SRC =
    "__kernel void fill_i32(__global int *buf, const int value, const int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) buf[i] = value;\n"
    "}\n"
    "__kernel void add_i32(__global const int *a, __global const int *b, __global int *out, const int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) out[i] = a[i] + b[i];\n"
    "}\n"
    "__kernel void mul_i32(__global const int *a, __global const int *b, __global int *out, const int n) {\n"
    "    int i = get_global_id(0);\n"
    "    if (i < n) out[i] = a[i] * b[i];\n"
    "}\n";

static char *gpu_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)fr_arena_alloc(fr_arena_tls(), n, 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

static int gpu_check(cl_int err, const char *where) {
    if (err == CL_SUCCESS) return 0;
    fprintf(stderr, "forge gpu: %s failed (%d)\n", where, (int)err);
    return -1;
}

static int gpu_init(void) {
    if (g_ready) return g_ready > 0 ? 0 : -1;
    g_ready = -1;

    cl_platform_id platform;
    cl_uint plat_count = 0;
    if (gpu_check(clGetPlatformIDs(1, &platform, &plat_count), "clGetPlatformIDs") || plat_count == 0)
        return -1;

    cl_uint dev_count = 0;
    if (gpu_check(clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, GPU_MAX_DEVICES, g_devices, &dev_count),
                  "clGetDeviceIDs") || dev_count == 0) {
        /* Fall back to CPU OpenCL devices when no GPU is exposed. */
        if (gpu_check(clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, GPU_MAX_DEVICES, g_devices, &dev_count),
                      "clGetDeviceIDs(cpu)") || dev_count == 0)
            return -1;
    }

    g_device_count = (int)dev_count;
    g_device = g_devices[0];
    g_selected = 0;

    cl_int err;
    g_context = clCreateContext(NULL, 1, &g_device, NULL, NULL, &err);
    if (gpu_check(err, "clCreateContext")) return -1;

    g_queue = clCreateCommandQueue(g_context, g_device, CL_QUEUE_PROFILING_ENABLE, &err);
    if (gpu_check(err, "clCreateCommandQueue")) return -1;

    g_builtins = clCreateProgramWithSource(g_context, 1, &GPU_BUILTIN_SRC, NULL, &err);
    if (gpu_check(err, "clCreateProgramWithSource")) return -1;
    if (gpu_check(clBuildProgram(g_builtins, 1, &g_device, "-cl-fast-relaxed-math", NULL, NULL),
                  "clBuildProgram")) {
        size_t log_len = 0;
        clGetProgramBuildInfo(g_builtins, g_device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_len);
        if (log_len > 1) {
            char *log = (char *)malloc(log_len);
            if (log) {
                clGetProgramBuildInfo(g_builtins, g_device, CL_PROGRAM_BUILD_LOG, log_len, log, NULL);
                fprintf(stderr, "forge gpu build log:\n%s\n", log);
                free(log);
            }
        }
        return -1;
    }

    memset(g_buffers, 0, sizeof(g_buffers));
    g_ready = 1;
    return 0;
}

static gpu_buffer_t *gpu_buf(int64_t handle) {
    if (handle < 0 || handle >= GPU_MAX_BUFFERS || !g_buffers[handle].in_use) return NULL;
    return &g_buffers[handle];
}

static cl_mem gpu_cl_mem(int64_t handle) {
    gpu_buffer_t *b = gpu_buf(handle);
    return b ? b->mem : NULL;
}

static int gpu_launch_builtin(const char *kernel, int64_t a, int64_t b, int64_t out, int64_t count) {
    if (gpu_init() != 0 || count < 0) return -1;
    cl_mem ma = gpu_cl_mem(a);
    cl_mem mb = gpu_cl_mem(b);
    cl_mem mout = gpu_cl_mem(out);
    if (!ma || !mb || !mout) return -1;
    if ((size_t)(count * 4) > g_buffers[a].bytes ||
        (size_t)(count * 4) > g_buffers[b].bytes ||
        (size_t)(count * 4) > g_buffers[out].bytes)
        return -1;

    cl_int err;
    cl_kernel k = clCreateKernel(g_builtins, kernel, &err);
    if (gpu_check(err, "clCreateKernel")) return -1;

    err  = clSetKernelArg(k, 0, sizeof(cl_mem), &ma);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &mb);
    err |= clSetKernelArg(k, 2, sizeof(cl_mem), &mout);
    int n = (int)count;
    err |= clSetKernelArg(k, 3, sizeof(int), &n);
    if (gpu_check(err, "clSetKernelArg")) {
        clReleaseKernel(k);
        return -1;
    }

    size_t global = (size_t)count;
    err = clEnqueueNDRangeKernel(g_queue, k, 1, NULL, &global, NULL, 0, NULL, NULL);
    clReleaseKernel(k);
    return gpu_check(err, "clEnqueueNDRangeKernel");
}

int64_t fr_gpu_available(void) {
    return gpu_init() == 0 ? 1 : 0;
}

const char *fr_gpu_backend(void) {
    return gpu_init() == 0 ? "opencl" : "none";
}

int64_t fr_gpu_device_count(void) {
    if (gpu_init() != 0) return 0;
    return g_device_count;
}

const char *fr_gpu_device_name(int64_t device_id) {
    if (gpu_init() != 0 || device_id < 0 || device_id >= g_device_count) return gpu_strdup("");
    char name[512];
    if (clGetDeviceInfo(g_devices[device_id], CL_DEVICE_NAME, sizeof(name), name, NULL) != CL_SUCCESS)
        return gpu_strdup("");
    return gpu_strdup(name);
}

int64_t fr_gpu_select_device(int64_t device_id) {
    if (gpu_init() != 0 || device_id < 0 || device_id >= g_device_count) return -1;
    if (device_id == g_selected) return 0;

    if (g_builtins) clReleaseProgram(g_builtins);
    if (g_queue) clReleaseCommandQueue(g_queue);
    if (g_context) clReleaseContext(g_context);
    for (int i = 0; i < GPU_MAX_BUFFERS; i++) {
        if (g_buffers[i].in_use) {
            clReleaseMemObject(g_buffers[i].mem);
            g_buffers[i].in_use = 0;
        }
    }

    g_ready = 0;
    g_selected = (int)device_id;
    g_device = g_devices[g_selected];
    return gpu_init();
}

int64_t fr_gpu_alloc(int64_t bytes) {
    if (gpu_init() != 0 || bytes <= 0) return -1;
    int slot = -1;
    for (int i = 0; i < GPU_MAX_BUFFERS; i++) {
        if (!g_buffers[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    cl_int err;
    cl_mem mem = clCreateBuffer(g_context, CL_MEM_READ_WRITE, (size_t)bytes, NULL, &err);
    if (gpu_check(err, "clCreateBuffer")) return -1;

    g_buffers[slot].mem = mem;
    g_buffers[slot].bytes = (size_t)bytes;
    g_buffers[slot].in_use = 1;
    return slot;
}

void fr_gpu_free(int64_t buffer) {
    gpu_buffer_t *b = gpu_buf(buffer);
    if (!b) return;
    clReleaseMemObject(b->mem);
    b->in_use = 0;
    b->mem = NULL;
    b->bytes = 0;
}

int64_t fr_gpu_copy(int64_t dst, int64_t src, int64_t bytes) {
    if (gpu_init() != 0 || bytes < 0) return -1;
    cl_mem md = gpu_cl_mem(dst);
    cl_mem ms = gpu_cl_mem(src);
    if (!md || !ms) return -1;
    if ((size_t)bytes > g_buffers[dst].bytes || (size_t)bytes > g_buffers[src].bytes) return -1;
    return gpu_check(clEnqueueCopyBuffer(g_queue, ms, md, 0, 0, (size_t)bytes, 0, NULL, NULL),
                     "clEnqueueCopyBuffer");
}

void fr_gpu_sync(void) {
    if (gpu_init() != 0) return;
    clFinish(g_queue);
}

int64_t fr_gpu_fill_i32(int64_t buffer, int64_t value, int64_t count) {
    if (gpu_init() != 0 || count < 0) return -1;
    cl_mem mem = gpu_cl_mem(buffer);
    if (!mem || (size_t)(count * 4) > g_buffers[buffer].bytes) return -1;

    cl_int err;
    cl_kernel k = clCreateKernel(g_builtins, "fill_i32", &err);
    if (gpu_check(err, "clCreateKernel(fill_i32)")) return -1;

    int v = (int)value;
    int n = (int)count;
    err  = clSetKernelArg(k, 0, sizeof(cl_mem), &mem);
    err |= clSetKernelArg(k, 1, sizeof(int), &v);
    err |= clSetKernelArg(k, 2, sizeof(int), &n);
    if (gpu_check(err, "clSetKernelArg(fill_i32)")) {
        clReleaseKernel(k);
        return -1;
    }

    size_t global = (size_t)count;
    err = clEnqueueNDRangeKernel(g_queue, k, 1, NULL, &global, NULL, 0, NULL, NULL);
    clReleaseKernel(k);
    return gpu_check(err, "clEnqueueNDRangeKernel(fill_i32)");
}

int64_t fr_gpu_write_i32(int64_t buffer, int64_t index, int64_t value) {
    if (gpu_init() != 0 || index < 0) return -1;
    gpu_buffer_t *b = gpu_buf(buffer);
    if (!b || (size_t)((index + 1) * 4) > b->bytes) return -1;
    int v = (int)value;
    return gpu_check(clEnqueueWriteBuffer(g_queue, b->mem, CL_TRUE, (size_t)index * 4, sizeof(int),
                                          &v, 0, NULL, NULL),
                     "clEnqueueWriteBuffer");
}

int64_t fr_gpu_read_i32(int64_t buffer, int64_t index) {
    if (gpu_init() != 0 || index < 0) return 0;
    gpu_buffer_t *b = gpu_buf(buffer);
    if (!b || (size_t)((index + 1) * 4) > b->bytes) return 0;
    int v = 0;
    if (gpu_check(clEnqueueReadBuffer(g_queue, b->mem, CL_TRUE, (size_t)index * 4, sizeof(int),
                                      &v, 0, NULL, NULL),
                  "clEnqueueReadBuffer"))
        return 0;
    return v;
}

int64_t fr_gpu_add_i32(int64_t a, int64_t b, int64_t out, int64_t count) {
    return gpu_launch_builtin("add_i32", a, b, out, count);
}

int64_t fr_gpu_mul_i32(int64_t a, int64_t b, int64_t out, int64_t count) {
    return gpu_launch_builtin("mul_i32", a, b, out, count);
}

int64_t fr_gpu_run_kernel(const char *source, const char *kernel_name,
                          int64_t arg0, int64_t arg1, int64_t arg2, int64_t count) {
    if (gpu_init() != 0 || !source || !kernel_name || count < 0) return -1;
    cl_mem m0 = gpu_cl_mem(arg0);
    cl_mem m1 = gpu_cl_mem(arg1);
    cl_mem m2 = gpu_cl_mem(arg2);
    if (!m0 || !m1 || !m2) return -1;

    cl_int err;
    cl_program prog = clCreateProgramWithSource(g_context, 1, &source, NULL, &err);
    if (gpu_check(err, "clCreateProgramWithSource(custom)")) return -1;
    if (gpu_check(clBuildProgram(prog, 1, &g_device, "-cl-fast-relaxed-math", NULL, NULL),
                  "clBuildProgram(custom)")) {
        clReleaseProgram(prog);
        return -1;
    }

    cl_kernel k = clCreateKernel(prog, kernel_name, &err);
    if (gpu_check(err, "clCreateKernel(custom)")) {
        clReleaseProgram(prog);
        return -1;
    }

    int n = (int)count;
    err  = clSetKernelArg(k, 0, sizeof(cl_mem), &m0);
    err |= clSetKernelArg(k, 1, sizeof(cl_mem), &m1);
    err |= clSetKernelArg(k, 2, sizeof(cl_mem), &m2);
    err |= clSetKernelArg(k, 3, sizeof(int), &n);

    size_t global = (size_t)count;
    if (!gpu_check(err, "clSetKernelArg(custom)"))
        err = clEnqueueNDRangeKernel(g_queue, k, 1, NULL, &global, NULL, 0, NULL, NULL);
    gpu_check(err, "clEnqueueNDRangeKernel(custom)");

    clReleaseKernel(k);
    clReleaseProgram(prog);
    return err == CL_SUCCESS ? 0 : -1;
}

#else /* !FORGE_HAS_OPENCL */

static char *gpu_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *out = (char *)fr_arena_alloc(fr_arena_tls(), n, 1);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

int64_t fr_gpu_available(void) { return 0; }
const char *fr_gpu_backend(void) { return gpu_strdup("none"); }
int64_t fr_gpu_device_count(void) { return 0; }
const char *fr_gpu_device_name(int64_t device_id) { (void)device_id; return gpu_strdup(""); }
int64_t fr_gpu_select_device(int64_t device_id) { (void)device_id; return -1; }
int64_t fr_gpu_alloc(int64_t bytes) { (void)bytes; return -1; }
void fr_gpu_free(int64_t buffer) { (void)buffer; }
int64_t fr_gpu_copy(int64_t dst, int64_t src, int64_t bytes) {
    (void)dst; (void)src; (void)bytes; return -1;
}
void fr_gpu_sync(void) {}
int64_t fr_gpu_fill_i32(int64_t buffer, int64_t value, int64_t count) {
    (void)buffer; (void)value; (void)count; return -1;
}
int64_t fr_gpu_write_i32(int64_t buffer, int64_t index, int64_t value) {
    (void)buffer; (void)index; (void)value; return -1;
}
int64_t fr_gpu_read_i32(int64_t buffer, int64_t index) {
    (void)buffer; (void)index; return 0;
}
int64_t fr_gpu_add_i32(int64_t a, int64_t b, int64_t out, int64_t count) {
    (void)a; (void)b; (void)out; (void)count; return -1;
}
int64_t fr_gpu_mul_i32(int64_t a, int64_t b, int64_t out, int64_t count) {
    (void)a; (void)b; (void)out; (void)count; return -1;
}
int64_t fr_gpu_run_kernel(const char *source, const char *kernel_name,
                          int64_t arg0, int64_t arg1, int64_t arg2, int64_t count) {
    (void)source; (void)kernel_name; (void)arg0; (void)arg1; (void)arg2; (void)count;
    return -1;
}

#endif /* FORGE_HAS_OPENCL */
