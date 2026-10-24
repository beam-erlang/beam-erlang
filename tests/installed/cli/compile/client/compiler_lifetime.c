/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "beam_compiler_host.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include "host_observe.h"
#endif

/* Distribution-owned acceptance instrumentation, not installed SDK APIs. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);

typedef struct resources {
    size_t allocations, bytes, mappings, mapped_bytes;
    size_t vm_keys, system_keys, vm_callbacks, system_callbacks, blocks, slots;
    size_t vm_locks, system_locks;
    int files, sockets, streams;
} resources;

static int entries(const char *path)
{
#ifdef __APPLE__
    if (!strcmp(path, "/proc/self/task")) return beam_test_host_threads(getpid());
    int *values=NULL; size_t count=0;
    if (strcmp(path, "/proc/self/fd") || beam_test_host_fds(getpid(),&values,&count)) return -1;
    free(values); return (int)count;
#else
    DIR *directory = opendir(path);
    struct dirent *entry;
    int count = 0;
    if (!directory)
        return -1;
    while ((entry = readdir(directory)))
        if (entry->d_name[0] != '.')
            ++count;
    closedir(directory);
    return count;
#endif
}

static void observe(resources *out)
{
    beam_runtime_memory_usage(&out->allocations, &out->bytes, &out->mappings, &out->mapped_bytes);
    ethr_runtime_usage(&out->vm_keys, &out->system_keys, &out->vm_callbacks,
                       &out->system_callbacks, &out->blocks, &out->slots);
    erts_sys_resource_usage(&out->vm_locks, &out->system_locks);
    out->files = beam_file_descriptor_count();
    out->sockets = beam_socket_count();
    out->streams = beam_zlib_stream_count();
}

static int quiescent(int threads, int descriptors, const resources *steady, int terminated)
{
    resources current = {0};
    struct timespec delay = {0, 1000000};
    int current_threads, current_descriptors;
    observe(&current);
    if (current.allocations || current.bytes || current.mappings || current.mapped_bytes ||
        current.vm_keys || current.vm_callbacks || current.vm_locks ||
        current.files || current.sockets || current.streams) {
        fprintf(stderr, "compiler VM resources remain: allocations=%zu bytes=%zu mappings=%zu "
                "mapped_bytes=%zu keys=%zu callbacks=%zu locks=%zu files=%d sockets=%d streams=%d\n",
                current.allocations, current.bytes, current.mappings, current.mapped_bytes,
                current.vm_keys, current.vm_callbacks, current.vm_locks,
                current.files, current.sockets, current.streams);
        return 0;
    }
    if (steady && (current.system_keys != steady->system_keys ||
                   current.system_callbacks != steady->system_callbacks ||
                   current.system_locks != steady->system_locks)) {
        fprintf(stderr, "compiler system resources changed: keys=%zu/%zu callbacks=%zu/%zu locks=%zu/%zu\n",
                current.system_keys, steady->system_keys, current.system_callbacks,
                steady->system_callbacks, current.system_locks, steady->system_locks);
        return 0;
    }
    if (terminated && (current.system_keys || current.system_callbacks || current.system_locks ||
                       current.blocks || current.slots)) {
        fprintf(stderr, "compiler system resources remain: keys=%zu callbacks=%zu locks=%zu "
                "event_blocks=%zu event_slots=%zu\n", current.system_keys,
                current.system_callbacks, current.system_locks, current.blocks, current.slots);
        return 0;
    }
    /* A joined Linux thread may briefly remain visible after clear_child_tid.
     * Resource accounting must already be clear before this bounded census. */
    current_threads = entries("/proc/self/task");
    for (unsigned attempt = 0; current_threads != threads && attempt < 1000; ++attempt) {
        nanosleep(&delay, NULL);
        current_threads = entries("/proc/self/task");
    }
    current_descriptors = entries("/proc/self/fd");
    if (current_threads != threads || current_descriptors != descriptors) {
        fprintf(stderr, "compiler OS resources changed: threads=%d/%d descriptors=%d/%d\n",
                current_threads, threads, current_descriptors, descriptors);
        return 0;
    }
    return 1;
}

static int answer(const beam_result *result)
{
    const unsigned char expected[] = {131, 97, 42};
    return result->kind == BEAM_RETURN && result->value.size == sizeof expected &&
           memcmp(result->value.data, expected, sizeof expected) == 0;
}

#define REQUIRE(condition) do { if (!(condition)) { \
    fprintf(stderr, "compiler lifetime:%d: %s\n", __LINE__, #condition); goto cleanup; \
} } while (0)
#define CALL(expression) do { beam_status call_status = (expression); \
    if (call_status != BEAM_OK) { \
        fprintf(stderr, "compiler lifetime:%d: %s: %s (%s)\n", __LINE__, #expression, \
                beam_status_message(call_status), error.message); goto cleanup; \
    } \
} while (0)

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error = {0};
    beam_result result = {0};
    beam_compiled_source saved = {0}, current = {0};
    resources steady = {0}, final = {0};
    unsigned char *snapshot = NULL;
    const unsigned char arguments[] = {131, 106};
    int threads = entries("/proc/self/task"), descriptors = entries("/proc/self/fd");
    int halt_status = -1, outcome = 1;

    REQUIRE(argc == 3 && threads > 0 && descriptors >= 0);
    CALL(beam_init(&system, &error));
    REQUIRE(quiescent(threads, descriptors, NULL, 0));
    for (unsigned generation = 0; generation < 3; ++generation) {
        REQUIRE(beam_compiler_execute(system, argv[1], argv[2], 0, NULL, &current) == 0);
        REQUIRE(current.module && strcmp(current.module, "compiler_lifetime_fixture") == 0);
        REQUIRE(current.bytes.size > 12 && current.bytes.data);
        REQUIRE(quiescent(threads, descriptors, generation ? &steady : NULL, 0));
        if (!generation) {
            observe(&steady);
            saved = current;
            memset(&current, 0, sizeof current);
            snapshot = malloc(saved.bytes.size);
            REQUIRE(snapshot);
            memcpy(snapshot, saved.bytes.data, saved.bytes.size);
        } else {
            REQUIRE(memcmp(snapshot, saved.bytes.data, saved.bytes.size) == 0);
            beam_compiled_source_release(&current);
        }
    }
    beam_vm_options_init(&options);

    CALL(beam_vm_init(system, &options, &vm, &error));
    CALL(beam_vm_load(vm, (beam_bytes){saved.bytes.data, saved.bytes.size}));
    /* Initialization may acquire VM-owned descriptors, but creates no workers. */
    REQUIRE(entries("/proc/self/task") == threads);
    CALL(beam_vm_start(vm, &error));
    CALL(beam_vm_call(vm, saved.module, "value", (beam_bytes){arguments, sizeof arguments}, &result));
    REQUIRE(answer(&result));
    CALL(beam_vm_terminate(&vm, &halt_status));
    REQUIRE(!vm && halt_status == 0 && quiescent(threads, descriptors, &steady, 0));
    REQUIRE(answer(&result) && memcmp(snapshot, saved.bytes.data, saved.bytes.size) == 0);
    CALL(beam_terminate(&system));
    REQUIRE(!system && quiescent(threads, descriptors, NULL, 1));
    REQUIRE(answer(&result) && memcmp(snapshot, saved.bytes.data, saved.bytes.size) == 0);
    observe(&final);
    printf("compiler lifetimes ok: threads=%d descriptors=%d allocations=%zu mappings=%zu "
           "files=%d sockets=%d streams=%d\n", entries("/proc/self/task"), entries("/proc/self/fd"),
           final.allocations, final.mappings, final.files, final.sockets, final.streams);
    outcome = 0;

cleanup:
    if (vm)
        (void)beam_vm_terminate(&vm, &halt_status);
    if (system)
        (void)beam_terminate(&system);
    beam_result_release(&result);
    beam_compiled_source_release(&current);
    beam_compiled_source_release(&saved);
    free(snapshot);
    return outcome;
}
