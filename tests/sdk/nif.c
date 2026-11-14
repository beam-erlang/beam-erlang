/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_nif.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#if defined(__linux__)
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#endif

static ERL_NIF_TERM add(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    int value;
    if (argc != 1 || !enif_get_int(env, argv[0], &value))
        return enif_make_badarg(env);
    return enif_make_int(env, value + 1);
}

static ERL_NIF_TERM symbol_visibility(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
#ifdef _WIN32
    /* Windows resolves native APIs through the upstream callback tables,
     * rather than importing another runtime or exporting every VM symbol. */
    const char *private_symbols[] = {"erl_start", "erts_alloc", "pcre2_compile_8",
        "ZSTD_compress", "deflate", "d2s_buffered_n"};
    HMODULE host = GetModuleHandle(NULL);
    size_t i;
    (void)argc;
    (void)argv;
    if (!GetProcAddress(host, "beam_main"))
        return enif_make_atom(env, "missing_process_entry_export");
    for (i = 0; i < sizeof(private_symbols) / sizeof(private_symbols[0]); i++) {
        if (GetProcAddress(host, private_symbols[i]))
            return enif_make_tuple2(env, enif_make_atom(env, "private_symbol_exported"),
                enif_make_string(env, private_symbols[i], ERL_NIF_LATIN1));
    }
#else
    const char *public_symbols[] = {"beam_main", "enif_make_int", "driver_output"};
    const char *private_symbols[] = {"erl_start", "sys_init_signal_stack", "erts_alloc",
        "pcre2_compile_8", "ZSTD_compress", "deflate", "d2s_buffered_n"};
    Dl_info host, found;
    void *entry = dlsym(RTLD_DEFAULT, "beam_main");
    size_t i;
    (void)argc;
    (void)argv;
    if (!entry || !dladdr(entry, &host))
        return enif_make_atom(env, "missing_process_entry_export");
    for (i = 0; i < sizeof(public_symbols) / sizeof(public_symbols[0]); i++) {
        if (!dlsym(RTLD_DEFAULT, public_symbols[i]))
            return enif_make_tuple2(env, enif_make_atom(env, "missing_native_export"),
                enif_make_string(env, public_symbols[i], ERL_NIF_LATIN1));
    }
    for (i = 0; i < sizeof(private_symbols) / sizeof(private_symbols[0]); i++) {
        void *symbol = dlsym(RTLD_DEFAULT, private_symbols[i]);
        /* A system library may export its own vendor API. Only the copy in
         * this statically linked host must remain private. */
        if (symbol && dladdr(symbol, &found) && found.dli_fbase == host.dli_fbase)
            return enif_make_tuple2(env, enif_make_atom(env, "private_symbol_exported"),
                enif_make_string(env, private_symbols[i], ERL_NIF_LATIN1));
    }
#endif
    return enif_make_atom(env, "ok");
}

/* These test-only OS calls use the process leader, just like upstream's
 * taskset -p <os:getpid()> test and the runtime's CPU information reader.
 * pid 0 here would instead change the calling NIF scheduler thread. */
static ERL_NIF_TERM affinity_get(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
#if defined(__linux__)
    cpu_set_t cpuset;
    ERL_NIF_TERM cpus = enif_make_list(env, 0);
    int cpu;
    if (sched_getaffinity(getpid(), sizeof(cpuset), &cpuset) != 0)
        return enif_make_tuple2(env, enif_make_atom(env, "error"), enif_make_int(env, errno));
    for (cpu = CPU_SETSIZE - 1; cpu >= 0; cpu--)
        if (CPU_ISSET(cpu, &cpuset))
            cpus = enif_make_list_cell(env, enif_make_int(env, cpu), cpus);
    return enif_make_tuple2(env, enif_make_atom(env, "ok"), cpus);
#else
    return enif_make_atom(env, "not_supported");
#endif
}

static ERL_NIF_TERM affinity_set(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
#if defined(__linux__)
    cpu_set_t cpuset;
    ERL_NIF_TERM head, tail;
    int cpu, count = 0;
    if (argc != 1)
        return enif_make_badarg(env);
    CPU_ZERO(&cpuset);
    tail = argv[0];
    while (enif_get_list_cell(env, tail, &head, &tail)) {
        if (!enif_get_int(env, head, &cpu) || cpu < 0 || cpu >= CPU_SETSIZE)
            return enif_make_badarg(env);
        CPU_SET(cpu, &cpuset);
        count++;
    }
    if (count == 0 || !enif_is_empty_list(env, tail))
        return enif_make_badarg(env);
    if (sched_setaffinity(getpid(), sizeof(cpuset), &cpuset) != 0)
        return enif_make_tuple2(env, enif_make_atom(env, "error"), enif_make_int(env, errno));
    return enif_make_atom(env, "ok");
#else
    return enif_make_atom(env, "not_supported");
#endif
}

static ErlNifFunc functions[] = {
    {"add", 1, add, 0},
    {"symbol_visibility", 0, symbol_visibility, 0},
    {"affinity_get", 0, affinity_get, 0},
    {"affinity_set", 1, affinity_set, 0}
};
ERL_NIF_INIT(beam_sdk_probe, functions, NULL, NULL, NULL, NULL)
