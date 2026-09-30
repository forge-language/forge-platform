#include "mod_registry.h"
#include <stdio.h>

static const ForgeStdFn IO_FNS[] = {
    {"print", "fr_print"},
    {"print_int", "fr_print_int"},
    {"print_str", "fr_print_str"},
    {"eprint", "fr_eprint"},
    {"eprint_int", "fr_io_eprint_int"},
    {"eprintln", "fr_eprintln"},
    {"flush", "fr_io_flush"},
    {"flush_err", "fr_io_flush_err"},
    {"read_line", "fr_io_read_line"},
    {"read_char", "fr_io_read_char"},
    {"read_stdin", "fr_io_read_stdin"},
    {"prompt", "fr_io_prompt"},
    {"write_fd", "fr_io_write_fd"},
    {"read_fd", "fr_io_read_fd"},
    {"stdin_fd", "fr_io_stdin_fd"},
    {"stdout_fd", "fr_io_stdout_fd"},
    {"stderr_fd", "fr_io_stderr_fd"},
};

static const ForgeStdFn STRING_FNS[] = {
    {"str_len", "fr_str_len"},
    {"str_concat", "fr_str_concat"},
    {"str_eq", "fr_str_eq"},
    {"str_sub", "fr_str_sub"},
    {"str_contains", "fr_str_contains"},
    {"str_trim", "fr_str_trim"},
    {"str_char_at", "fr_str_char_at"},
    {"str_append", "fr_str_append"},
    {"str_append_str", "fr_str_append_str"},
    {"str_from_int", "fr_str_from_int"},
    {"str_reset_arena", "fr_str_arena_reset"},
};

static const ForgeStdFn MATH_FNS[] = {
    {"abs_i", "fr_abs_i"},
    {"abs_f", "fr_abs_f"},
    {"min_i", "fr_min_i"},
    {"max_i", "fr_max_i"},
    {"clamp_i", "fr_clamp_i"},
    {"pow_i", "fr_pow_i"},
};

static const ForgeStdFn TIME_FNS[] = {
    {"time_now_ms", "fr_time_now_ms"},
    {"sleep_ms", "fr_sleep_ms"},
};

static const ForgeStdFn FS_FNS[] = {
    {"fs_read", "fr_fs_read"},
    {"fs_write", "fr_fs_write"},
    {"fs_append", "fr_fs_append"},
    {"fs_exists", "fr_fs_exists"},
    {"fs_remove", "fr_fs_remove"},
    {"fs_size", "fr_fs_size"},
    {"fs_is_file", "fr_fs_is_file"},
    {"fs_is_dir", "fr_fs_is_dir"},
    {"fs_mkdir", "fr_fs_mkdir"},
    {"fs_rename", "fr_fs_rename"},
    {"fs_copy", "fr_fs_copy"},
    {"fs_list_dir", "fr_fs_list_dir"},
};

static const ForgeStdFn OS_FNS[] = {
    {"os_exit", "fr_os_exit"},
    {"os_getenv", "fr_os_getenv"},
    {"os_argc", "fr_os_argc"},
    {"os_argv", "fr_os_argv"},
};

static const ForgeStdFn TCP_FNS[] = {
    {"tcp_listen", "fr_tcp_listen"},
    {"tcp_accept", "fr_tcp_accept"},
    {"tcp_connect", "fr_tcp_connect"},
    {"tcp_send", "fr_tcp_send"},
    {"tcp_recv", "fr_tcp_recv"},
    {"tcp_close", "fr_tcp_close"},
    {"net_init", "fr_net_init"},
};

static const ForgeStdFn UDP_FNS[] = {
    {"udp_bind", "fr_udp_bind"},
    {"udp_send", "fr_udp_send"},
    {"udp_recv", "fr_udp_recv"},
    {"udp_peer", "fr_udp_peer"},
    {"udp_close", "fr_udp_close"},
};

static const ForgeStdFn HTTP_FNS[] = {
    {"http_get", "fr_http_get"},
    {"http_post", "fr_http_post"},
    {"http_listen", "fr_http_listen"},
    {"http_accept", "fr_http_accept"},
    {"http_req_method", "fr_http_req_method"},
    {"http_req_path", "fr_http_req_path"},
    {"http_req_body", "fr_http_req_body"},
    {"http_respond", "fr_http_respond"},
    {"http_close", "fr_http_close"},
    {"http_server_close", "fr_http_server_close"},
    {"http_prepare", "fr_http_prepare"},
    {"http_serve_prepared", "fr_http_serve_prepared"},
    {"http_serve_forever", "fr_http_serve_forever"},
    {"http_serve_ok", "fr_http_serve_ok"},
    {"http_serve_mt", "fr_http_serve_mt"},
    {"http_serve_hybrid", "fr_http_serve_hybrid"},
};

static const ForgeStdFn EVENT_FNS[] = {
    {"event_poll", "fr_event_poll"},
    {"event_add_read", "fr_event_add_read"},
};

static const ForgeStdFn JSON_FNS[] = {
    {"json_get_string", "fr_json_get_string"},
    {"json_get_int", "fr_json_get_int"},
    {"json_stringify_str", "fr_json_stringify_str"},
    {"json_stringify_int", "fr_json_stringify_int"},
};

static const ForgeStdFn THREAD_FNS[] = {
    {"thread_cpu_count", "fr_threading_cpu_count"},
    {"thread_yield", "fr_threading_yield"},
    {"thread_pin", "fr_threading_pin"},
    {"thread_self", "fr_threading_self"},
    {"thread_worker_id", "fr_threading_worker_id"},
    {"thread_mutex_create", "fr_threading_mutex_create"},
    {"thread_mutex_lock", "fr_threading_mutex_lock"},
    {"thread_mutex_unlock", "fr_threading_mutex_unlock"},
    {"thread_mutex_destroy", "fr_threading_mutex_destroy"},
    {"thread_join", "fr_threading_join"},
    {"thread_join_all", "fr_threading_join_all"},
};

static const ForgeStdFn GPU_FNS[] = {
    {"gpu_available", "fr_gpu_available"},
    {"gpu_backend", "fr_gpu_backend"},
    {"gpu_device_count", "fr_gpu_device_count"},
    {"gpu_device_name", "fr_gpu_device_name"},
    {"gpu_select_device", "fr_gpu_select_device"},
    {"gpu_alloc", "fr_gpu_alloc"},
    {"gpu_free", "fr_gpu_free"},
    {"gpu_copy", "fr_gpu_copy"},
    {"gpu_sync", "fr_gpu_sync"},
    {"gpu_fill_i32", "fr_gpu_fill_i32"},
    {"gpu_write_i32", "fr_gpu_write_i32"},
    {"gpu_read_i32", "fr_gpu_read_i32"},
    {"gpu_add_i32", "fr_gpu_add_i32"},
    {"gpu_mul_i32", "fr_gpu_mul_i32"},
    {"gpu_run_kernel", "fr_gpu_run_kernel"},
};

static const ForgeModule MODULES[] = {
    { .name = { "io", 2 }, .header = "forge/io.h", .fns = IO_FNS, .fn_count = 17 },
    { .name = { "strings", 7 }, .header = "forge/string.h", .fns = STRING_FNS, .fn_count = 11 },
    { .name = { "math", 4 }, .header = "forge/math.h", .fns = MATH_FNS, .fn_count = 6 },
    { .name = { "time", 4 }, .header = "forge/time.h", .fns = TIME_FNS, .fn_count = 2 },
    { .name = { "fs", 2 }, .header = "forge/fs.h", .fns = FS_FNS, .fn_count = 12 },
    { .name = { "os", 2 }, .header = "forge/os.h", .fns = OS_FNS, .fn_count = 4 },
    { .name = { "tcp", 3 }, .header = "forge/tcp.h", .fns = TCP_FNS, .fn_count = 7 },
    { .name = { "udp", 3 }, .header = "forge/udp.h", .fns = UDP_FNS, .fn_count = 5 },
    { .name = { "http", 4 }, .header = "forge/http.h", .fns = HTTP_FNS, .fn_count = 16 },
    { .name = { "event", 5 }, .header = "forge/event.h", .fns = EVENT_FNS, .fn_count = 2 },
    { .name = { "json", 4 }, .header = "forge/json.h", .fns = JSON_FNS, .fn_count = 4 },
    { .name = { "gpu", 3 }, .header = "forge/gpu.h", .fns = GPU_FNS, .fn_count = 15 },
    { .name = { "thread", 6 }, .header = "forge/threading.h", .fns = THREAD_FNS, .fn_count = 11 },
};

const ForgeModule *forge_std_module(ForgeStr name) {
    for (size_t i = 0; i < sizeof(MODULES) / sizeof(MODULES[0]); i++) {
        if (forge_str_eq(MODULES[i].name, name)) return &MODULES[i];
    }
    return NULL;
}

const char *forge_std_c_name(ForgeStr fr_name, ForgeStr *imports, size_t import_count) {
    for (size_t i = 0; i < import_count; i++) {
        const ForgeModule *mod = forge_std_module(imports[i]);
        if (!mod) continue;
        for (size_t j = 0; j < mod->fn_count; j++) {
            ForgeStr fn = forge_str(mod->fns[j].fr_name);
            if (forge_str_eq(fn, fr_name)) return mod->fns[j].c_name;
        }
    }
    return NULL;
}

const char *forge_std_header(ForgeStr module) {
    const ForgeModule *mod = forge_std_module(module);
    return mod ? mod->header : NULL;
}

int forge_import_is_stdlib(ForgeStr name) {
    return forge_std_module(name) != NULL;
}

void forge_lib_mangle(char *out, size_t cap, ForgeStr lib, ForgeStr fn) {
    snprintf(out, cap, "frlib_%.*s_%.*s", (int)lib.len, lib.data, (int)fn.len, fn.data);
}

void forge_mod_mangle(char *out, size_t cap, ForgeStr mod, ForgeStr fn) {
    snprintf(out, cap, "frmod_%.*s_%.*s", (int)mod.len, mod.data, (int)fn.len, fn.data);
}

int forge_import_is_file_module(Program *prog, ForgeStr name) {
    for (size_t i = 0; i < prog->module_count; i++) {
        if (forge_str_eq(prog->modules[i].name, name)) return 1;
    }
    return 0;
}
