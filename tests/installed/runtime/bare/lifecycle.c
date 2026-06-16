/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <beam.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "module.h"
#include "fixtures.h"
#include "support.h"

/* Internal acceptance instrumentation; these are not SDK interfaces. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);
extern char *erl_errno_id(int);
extern uintptr_t erts_debugger_flags;

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "lifecycle:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

static const unsigned char empty_data[] = {131, 106};
static const beam_bytes empty = {empty_data, sizeof empty_data};

static const char *host_environment[] = {
    "ERL_THREAD_POOL_SIZE", "ERL_MAX_ETS_TABLES", "ERL_FULLSWEEP_AFTER", "ERL_MAX_PORTS"
};

static int host_settings_unchanged(const char *locale)
{
    const char *current = setlocale(LC_CTYPE, NULL);
    const char *environment_locale = getenv("LC_ALL");
    if (!current || strcmp(current, locale)) return 0;
    if (!environment_locale || strcmp(environment_locale, "C.UTF-8")) return 0;
    for (size_t i = 0; i < sizeof host_environment / sizeof host_environment[0]; ++i) {
        const char *value = getenv(host_environment[i]);
        if (!value || strcmp(value, "host-owned-invalid-number")) return 0;
    }
    return 1;
}

static int entries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;
    if (!dir) return -1;
    while ((entry = readdir(dir)))
        if (entry->d_name[0] != '.') ++count;
    closedir(dir);
    return count;
}

static int executable_mappings(void)
{
    FILE *file = fopen("/proc/self/maps", "r");
    char line[4096], permissions[5];
    int count = 0;
    if (!file) return -1;
    while (fgets(line, sizeof line, file))
        if (sscanf(line, "%*s %4s", permissions) == 1 && permissions[2] == 'x')
            ++count;
    fclose(file);
    return count;
}

static int resources_released(int system_released, int steady_system)
{
    static size_t baseline_keys, baseline_callbacks, baseline_locks;
    static int baseline_recorded;
    size_t allocations, bytes, mappings, mapped_bytes;
    size_t vm_keys, system_keys, vm_callbacks, system_callbacks, blocks, slots;
    size_t vm_locks, system_locks;
    beam_runtime_memory_usage(&allocations, &bytes, &mappings, &mapped_bytes);
    ethr_runtime_usage(&vm_keys, &system_keys, &vm_callbacks, &system_callbacks,
                       &blocks, &slots);
    erts_sys_resource_usage(&vm_locks, &system_locks);
    if (allocations || bytes || mappings || mapped_bytes || vm_keys || vm_callbacks || vm_locks) {
        fprintf(stderr, "unreleased VM resources: alloc=%zu bytes=%zu maps=%zu mapped=%zu "
                        "keys=%zu callbacks=%zu locks=%zu\n", allocations, bytes, mappings,
                        mapped_bytes, vm_keys, vm_callbacks, vm_locks);
        return 0;
    }
    if (system_released && (system_keys || system_callbacks || blocks || slots || system_locks)) {
        fprintf(stderr, "unreleased system resources: keys=%zu callbacks=%zu "
                        "event_blocks=%zu event_slots=%zu locks=%zu\n",
                system_keys, system_callbacks, blocks, slots, system_locks);
        return 0;
    }
    if (steady_system) {
        if (!baseline_recorded) {
            baseline_keys = system_keys;
            baseline_callbacks = system_callbacks;
            baseline_locks = system_locks;
            baseline_recorded = 1;
        } else if (system_keys != baseline_keys || system_callbacks != baseline_callbacks ||
                   system_locks != baseline_locks) {
            fprintf(stderr, "system resources changed across VM lifetimes: "
                            "keys=%zu/%zu callbacks=%zu/%zu locks=%zu/%zu\n",
                    system_keys, baseline_keys, system_callbacks, baseline_callbacks,
                    system_locks, baseline_locks);
            return 0;
        }
    }
    return 1;
}

static void pause_ms(long milliseconds)
{
    struct timespec delay = {milliseconds / 1000, milliseconds % 1000 * 1000000};
    nanosleep(&delay, NULL);
}

static int joined_thread_count(int baseline)
{
    int count = entries("/proc/self/task");
    /* Linux may retain an exiting task entry briefly after pthread_join:
     * clear_child_tid precedes final task release. Resource accounting must
     * already be at baseline before allowing this bounded census delay. */
    for (unsigned attempt = 0; count != baseline && attempt < 1000; ++attempt) {
        pause_ms(1);
        count = entries("/proc/self/task");
    }
    return count;
}

static int small_result(const beam_result *result, unsigned value)
{
    return result->kind == BEAM_RETURN && result->value.size == 3 &&
           result->value.data[0] == 131 && result->value.data[1] == 97 &&
           result->value.data[2] == value;
}

static int atom_result(const beam_result *result, const char *value)
{
    const unsigned char *p = result->value.data;
    size_t n = strlen(value), start;
    if (result->kind != BEAM_RETURN || result->value.size < 3 || p[0] != 131)
        return 0;
    if (p[1] == 119 || p[1] == 115) {
        start = 3;
        if (p[2] != n) return 0;
    } else if ((p[1] == 118 || p[1] == 100) && result->value.size >= 4) {
        start = 4;
        if (((size_t)p[2] << 8 | p[3]) != n) return 0;
    } else return 0;
    return result->value.size == start + n && !memcmp(p + start, value, n);
}

static int call_atom(beam_vm *vm, const char *function, beam_bytes args,
                     const char *expected)
{
    beam_result result = {0};
    OK(beam_vm_call(vm, "bare_lifecycle", function, args, &result));
    if (!atom_result(&result, expected)) {
        fprintf(stderr, "bare_lifecycle:%s: expected %s, outcome=%d, ETF=",
                function, expected, (int)result.kind);
        for (size_t i = 0; i < result.value.size; ++i)
            fprintf(stderr, "%02x", result.value.data[i]);
        fputc('\n', stderr);
        beam_result_release(&result);
        return 1;
    }
    beam_result_release(&result);
    return 0;
}

static beam_bytes binary_argument(const char *value, unsigned char *storage)
{
    size_t n = strlen(value), pos = 0;
    storage[pos++] = 131;
    storage[pos++] = 108;
    storage[pos++] = 0; storage[pos++] = 0; storage[pos++] = 0; storage[pos++] = 1;
    storage[pos++] = 109;
    for (int shift = 24; shift >= 0; shift -= 8)
        storage[pos++] = (unsigned char)(n >> shift);
    memcpy(storage + pos, value, n);
    pos += n;
    storage[pos++] = 106;
    return (beam_bytes){storage, pos};
}

static beam_bytes configuration_arguments(const beam_vm_options *options,
                                         unsigned char *storage)
{
    const size_t values[] = {options->schedulers, options->schedulers_online,
        options->dirty_cpu_schedulers, options->dirty_cpu_schedulers_online,
        options->dirty_io_schedulers, options->async_threads,
        options->process_limit, options->atom_limit,
        options->min_heap_size == 234 ? 376 : 987,
        options->min_bin_vheap_size == 30000 ? 46422 : 75113,
        options->max_heap_size};
    size_t pos = 0;
    storage[pos++] = 131; storage[pos++] = 108;
    storage[pos++] = 0; storage[pos++] = 0; storage[pos++] = 0;
    storage[pos++] = sizeof values / sizeof values[0];
    for (size_t i = 0; i < sizeof values / sizeof values[0]; ++i) {
        storage[pos++] = 98;
        for (int shift = 24; shift >= 0; shift -= 8)
            storage[pos++] = (unsigned char)(values[i] >> shift);
    }
    storage[pos++] = 106;
    return (beam_bytes){storage, pos};
}

static beam_status handler(void *context, beam_bytes request, beam_host_reply *reply)
{
    unsigned *calls = context;
    const unsigned char answer[] = {131, 97, 42};
    (void)request;
    ++*calls;
    return beam_host_reply_copy(reply, (beam_bytes){answer, sizeof answer});
}

int main(void)
{
    beam_system *system = NULL, *other_system = NULL;
    beam_vm *vm = NULL, *other = NULL;
    beam_vm_options options;
    beam_vm_state_info state;
    beam_result result = {0}, saved = {0}, identities = {0}, local = {0};
    beam_host_info host = {0};
    beam_error error = {0};
    int threads = entries("/proc/self/task"), fds = entries("/proc/self/fd");
    int status = -1, executable_maps = executable_mappings();
    char host_locale[128];
    char previous_atom[64] = "beam_lifecycle_never_created";
    unsigned char arguments[512];
    CHECK(threads > 0 && fds >= 0 && executable_maps > 0);
    /* Make an accidental setlocale(..., "") observable even in a C-locale shell. */
    CHECK(setenv("LC_ALL", "C.UTF-8", 1) == 0);
    CHECK(setlocale(LC_CTYPE, "C") != NULL);
    CHECK(strlen(setlocale(LC_CTYPE, NULL)) < sizeof host_locale);
    strcpy(host_locale, setlocale(LC_CTYPE, NULL));
    for (size_t i = 0; i < sizeof host_environment / sizeof host_environment[0]; ++i)
        CHECK(setenv(host_environment[i], "host-owned-invalid-number", 1) == 0);
    host.struct_size = sizeof host;
    OK(beam_query_host(&host, &error));
    OK(beam_query_host(&host, NULL));
    CHECK(host_settings_unchanged(host_locale));
    CHECK(entries("/proc/self/task") == threads && entries("/proc/self/fd") == fds);
    OK(beam_init(&system, &error));
    CHECK(host_settings_unchanged(host_locale));
    CHECK(beam_init(&other_system, &error) == BEAM_ALREADY_STARTED && !other_system);
    {
        beam_system *saved_system = system;
        CHECK(beam_init(&system, &error) == BEAM_ALREADY_STARTED && system == saved_system);
    }
    CHECK(entries("/proc/self/task") == threads && entries("/proc/self/fd") == fds);
    beam_vm_options_init(&options);
    /* Failed preparation leaves the system available for correction. */
    options.schedulers_online = options.schedulers + 1;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm);
    CHECK(error.message[0]);
    other = (beam_vm *)&options;
    CHECK(beam_vm_init(system, &options, &other, &error) == BEAM_INVALID_ARGUMENT &&
          other == (beam_vm *)&options);
    other = NULL;
    beam_vm_options_init(&options);
    CHECK(options.allocator_tagging == BEAM_ALLOCATOR_TAGGING_DEFAULT);
    options.allocator_tagging = (beam_allocator_tagging) -1;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm);
    options.allocator_tagging = (beam_allocator_tagging) 3;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm);
    beam_vm_options_init(&options);
    CHECK(entries("/proc/self/task") == threads && entries("/proc/self/fd") == fds);

    /* Stopping an unstarted VM never needs to create a worker. */
    OK(beam_vm_init(system, &options, &vm, NULL));
    OK(beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}));
    CHECK(beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}) == BEAM_ALREADY_LOADED);
    CHECK(beam_vm_load(vm, (beam_bytes){bare_module, 20}) == BEAM_INVALID_BEAM);
    CHECK(beam_vm_load(vm, (beam_bytes){bare_on_load, sizeof bare_on_load}) == BEAM_UNSUPPORTED);
    CHECK(host_settings_unchanged(host_locale));
    OK(beam_vm_get_state(vm, &state));
    CHECK(state.state == BEAM_VM_INITIALIZED && state.halt_status == 0);
    CHECK(beam_vm_call(vm, "bare_example", "answer", empty, &result) == BEAM_INVALID_STATE);
    CHECK(entries("/proc/self/task") == threads);
    CHECK(beam_vm_init(system, &options, &other, NULL) == BEAM_ALREADY_STARTED && !other);
    {
        beam_vm *saved_vm = vm;
        CHECK(beam_vm_init(system, &options, &vm, NULL) == BEAM_ALREADY_STARTED && vm == saved_vm);
    }
    CHECK(beam_terminate(&system) == BEAM_INVALID_STATE && system);
    OK(beam_vm_stop(vm, &status));
    OK(beam_vm_stop(vm, &status));
    CHECK(status == 0 && entries("/proc/self/task") == threads);
    CHECK(beam_vm_start(vm, NULL) == BEAM_INVALID_STATE);
    CHECK(beam_vm_init(system, &options, &other, NULL) == BEAM_ALREADY_STARTED && !other);
    CHECK(beam_vm_init(system, &options, &other, &error) == BEAM_ALREADY_STARTED && !other);

    OK(beam_vm_terminate(&vm, &status));
    CHECK(!vm && status == 0 && resources_released(0, 0));
    OK(beam_vm_init(system, &options, &vm, NULL));
    OK(beam_vm_terminate(&vm, &status));
    CHECK(!vm && status == 0 && resources_released(0, 0));

    for (unsigned cycle = 0; cycle < 12; ++cycle) {
        unsigned calls = 0;
        int expected_halt = 0;
        char atom[64];
        beam_bytes module = cycle % 2
            ? (beam_bytes){bare_lifecycle_2, sizeof bare_lifecycle_2}
            : (beam_bytes){bare_lifecycle_1, sizeof bare_lifecycle_1};
        unsigned char *copy = malloc(module.size);
        CHECK(copy);
        memcpy(copy, module.data, module.size);
        beam_vm_options_init(&options);
        /* Repeat default -> enabled -> disabled -> default with real work and
         * complete resource accounting between every fresh VM. */
        options.allocator_tagging = (beam_allocator_tagging)(cycle % 4 == 3 ? 0 : cycle % 4);

        if (!(cycle % 2)) {
            options.schedulers = 3;
            options.dirty_cpu_schedulers = 2;
            options.dirty_io_schedulers = 2;
            options.async_threads = 2;
            options.poll_threads = 2;
        }
        options.process_limit = cycle % 2 ? 65536 : 131072;
        options.atom_limit = cycle % 2 ? 65536 : 131072;
        /* Request sizes between native bins to verify documented rounding. */
        options.min_heap_size = cycle % 2 ? 611 : 234;
        options.min_bin_vheap_size = cycle % 2 ? 50000 : 30000;
        options.max_heap_size = cycle % 2 ? 400000 : 0;
        OK(beam_vm_init(system, &options, &vm, &error));
        CHECK(!strcmp(erl_errno_id(EINVAL), "einval"));
        CHECK(!strcmp(erl_errno_id(EAGAIN), "eagain"));
        CHECK(erts_debugger_flags == 2); /* Line instrumentation permitted, debugger disabled. */
        CHECK(host_settings_unchanged(host_locale));
        OK(beam_vm_get_state(vm, &state));
        CHECK(state.state == BEAM_VM_INITIALIZED && state.halt_status == 0);
        OK(beam_vm_load(vm, (beam_bytes){copy, module.size}));
        CHECK(host_settings_unchanged(host_locale));
        memset(copy, 0, module.size);
        free(copy);
        OK(beam_vm_load(vm, (beam_bytes){host_module, sizeof host_module}));
        OK(beam_vm_set_host_handler(vm, handler, &calls));
        CHECK(entries("/proc/self/task") == threads && calls == 0);
        /* Internal acceptance instrumentation: mutate only before workers run,
         * then require the documented defaults in the next fresh VM. */
        erts_debugger_flags |= 1;
        OK(beam_vm_start(vm, &error));
        CHECK(call_atom(vm, "configuration", configuration_arguments(&options, arguments), "ok") == 0);
        {
            unsigned char mode[] = {131, 108, 0, 0, 0, 1, 97,
                                   (unsigned char)options.allocator_tagging, 106};
            CHECK(call_atom(vm, "allocator_tags", (beam_bytes){mode, sizeof mode}, "ok") == 0);
        }
        CHECK(host_settings_unchanged(host_locale));
        CHECK(beam_vm_start(vm, NULL) == BEAM_INVALID_STATE);
        OK(beam_vm_get_state(vm, &state));
        CHECK(state.state == BEAM_VM_RUNNING && state.halt_status == 0);
        OK(beam_vm_call(vm, "bare_lifecycle", "generation", empty, &result));
        CHECK(small_result(&result, cycle % 2 ? 2 : 1));
        if (!cycle) {
            saved = result;
            memset(&result, 0, sizeof result);
        } else beam_result_release(&result);
        if (!cycle) {
            OK(beam_vm_call(vm, "bare_lifecycle", "identities", empty, &identities));
            OK(beam_vm_call(vm, "bare_lifecycle", "local_arguments", empty, &local));
            CHECK(identities.kind == BEAM_RETURN && local.kind == BEAM_RETURN);
            CHECK(local.value.size > 7 && local.value.data[0] == 131 && local.value.data[1] == 109);
        } else {
            size_t size = identities.value.size;
            CHECK(size > 1 && size + 6 < sizeof arguments && identities.value.data[0] == 131);
            memcpy(arguments, (unsigned char[]){131,108,0,0,0,1}, 6);
            memcpy(arguments + 6, identities.value.data + 1, size - 1);
            arguments[size + 5] = 106;
            CHECK(call_atom(vm, "old_identities", (beam_bytes){arguments, size + 6}, "ok") == 0);
        }
        CHECK(beam_vm_call(vm, "erlang", "length",
                          (beam_bytes){local.value.data + 6, local.value.size - 6},
                          &result) == BEAM_INVALID_TERM);
        CHECK(call_atom(vm, "atom_absent", binary_argument(previous_atom, arguments), "true") == 0);
        CHECK(call_atom(vm, "callback", empty, "ok") == 0 && calls == 1);
        snprintf(atom, sizeof atom, "beam_lifecycle_dynamic_%u", cycle);
        CHECK(call_atom(vm, "leave_state", binary_argument(atom, arguments), "ok") == 0);
        strcpy(previous_atom, atom);
        CHECK(beam_vm_init(system, &options, &other, NULL) == BEAM_ALREADY_STARTED && !other);
        CHECK(beam_terminate(&system) == BEAM_INVALID_STATE && system);
        if (cycle % 4 == 0) {
            OK(beam_vm_call(vm, "bare_lifecycle", "halt", empty, &result));
            CHECK(result.kind == BEAM_HALT && result.halt_status == 17);
            beam_result_release(&result);
            expected_halt = 17;
        } else if (cycle % 4 == 1) {
            CHECK(call_atom(vm, "background_halt", empty, "ok") == 0);
            for (unsigned attempt = 0; attempt < 100; ++attempt) {
                OK(beam_vm_get_state(vm, &state));
                if (state.state != BEAM_VM_RUNNING) break;
                pause_ms(10);
            }
            CHECK(state.state == BEAM_VM_HALT_REQUESTED && state.halt_status == 19);
            expected_halt = 19;
        } else {
            CHECK(call_atom(vm, "pending", empty, "ok") == 0);
            pause_ms(75);
            CHECK(calls == 1);
        }
        if (!(cycle % 2)) {
            OK(beam_vm_stop(vm, &status));
            OK(beam_vm_get_state(vm, &state));
            CHECK(state.state == BEAM_VM_STOPPED);
            CHECK(beam_vm_init(system, &options, &other, NULL) == BEAM_ALREADY_STARTED && !other);
        }
        OK(beam_vm_terminate(&vm, &status));
        CHECK(!vm && status == expected_halt && calls == 1);
        CHECK(resources_released(0, 1));
        CHECK(executable_mappings() == executable_maps);
        CHECK(host_settings_unchanged(host_locale));
        CHECK(entries("/proc/self/fd") == fds);
        CHECK(joined_thread_count(threads) == threads);
        CHECK(small_result(&saved, 1));
    }
    OK(beam_terminate(&system));
    CHECK(!system && small_result(&saved, 1) && resources_released(1, 0));
    CHECK(host_settings_unchanged(host_locale));
    CHECK(identities.kind == BEAM_RETURN && identities.value.size > 1);
    CHECK(local.kind == BEAM_RETURN && local.value.size > 7);
    beam_result_release(&identities);
    beam_result_release(&local);
    beam_result_release(&saved);
    CHECK(entries("/proc/self/task") == threads && entries("/proc/self/fd") == fds);
    puts("Sequential VM lifetimes completed; C continued.");
    return 0;
}
