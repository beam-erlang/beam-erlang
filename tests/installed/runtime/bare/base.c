/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include "bytecode_input.h"
#include "builtin_bindings.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"
#ifdef __APPLE__
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include "host_observe.h"
#endif

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "base:%d: %s (%s)\n", __LINE__, #test, error.message); return 1; } } while (0)
static const char *base_names[] = {"beam", "beam_format", "beam_time", "beam_atomics", "beam_counters", "beam_persistent_term", "beam_binary", "beam_lists", "beam_maps", "beam_records", "beam_ets", "beam_string", "beam_unicode", "beam_runtime_unicode_util", "beam_math", "beam_ddll", "beam_debugger"};

static int entries(const char *path)
{
#ifdef __APPLE__
    if (!strcmp(path, "/proc/self/task")) return beam_test_host_threads(getpid());
    if (!strcmp(path, "/proc/self/fd")) {
        int *descriptors; size_t count;
        if (beam_test_host_fds(getpid(), &descriptors, &count)) return -1;
        free(descriptors);
        return count <= INT_MAX ? (int)count : -1;
    }
    errno = EINVAL; return -1;
#else
    DIR *directory = opendir(path);
    struct dirent *entry;
    int count = 0;
    if (!directory) return -1;
    while ((entry = readdir(directory)))
        if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count;
#endif
}

static int module_count(beam_vm *vm, size_t expected)
{
    beam_module_list modules = {0};
    int valid = beam_module_list_names(vm, &modules) == BEAM_OK && modules.count == expected;
    beam_module_list_release(&modules);
    return valid;
}

static int joined_threads(int expected)
{
    int count = entries("/proc/self/task");
    /* As in lifecycle.c, joined Linux tasks can remain visible until their
     * final task release. Bound the census delay after VM termination. */
    for (unsigned attempt = 0; count != expected && attempt < 1000; ++attempt) {
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
        count = entries("/proc/self/task");
    }
    return count;
}

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error = {0};
    beam_result result = {0};
    beam_module_info info = {sizeof info, 0};
    const unsigned char empty[] = {131, 106};
    const unsigned char ok[] = {131, 119, 2, 'o', 'k'};
    CHECK(argc == 2);
    CHECK(beam_init(&system, &error) == BEAM_OK);
    int workers = entries("/proc/self/task"), descriptors = entries("/proc/self/fd");
    CHECK(workers > 0 && descriptors >= 0);
    beam_vm_options_init(&options);
    beam_composition *observers = NULL;
    beam_composition_options observer_spec = {.struct_size = sizeof observer_spec,
        .builtin_bindings = beam_test_base_observer_bindings,
        .builtin_binding_count = BEAM_TEST_BASE_OBSERVER_BINDING_COUNT};
    CHECK(beam_composition_prepare(&observer_spec, &observers, &error) == BEAM_OK);
    options.composition = observers;

    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    CHECK(module_count(vm, 6));
    int initialized_descriptors = entries("/proc/self/fd");
    CHECK(entries("/proc/self/task") == workers && initialized_descriptors >= descriptors);
    for (size_t i = 0; i < sizeof base_names / sizeof base_names[0]; ++i) {
        CHECK(beam_module_get_info(vm, base_names[i], &info) == BEAM_OK && !info.flags);
    }
    CHECK(beam_module_load(vm, (beam_bytes){base_contract, sizeof base_contract}) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(beam_vm_call(vm, "base_contract", "check", (beam_bytes){empty, sizeof empty}, &result) == BEAM_OK);
    CHECK(result.kind == BEAM_RETURN && result.value.size == sizeof ok && !memcmp(result.value.data, ok, sizeof ok));
    beam_result_release(&result);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    CHECK(joined_threads(workers) == workers && entries("/proc/self/fd") == descriptors);
    for (int latin1 = 0; latin1 < 2; ++latin1) {
        beam_vm_options_init(&options);
        options.composition = observers;
        options.filename_encoding = latin1 ? BEAM_FILENAME_LATIN1 : BEAM_FILENAME_UTF8;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        CHECK(beam_module_load(vm, (beam_bytes){base_contract, sizeof base_contract}) == BEAM_OK);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        CHECK(beam_vm_call(vm, "base_contract", latin1 ? "filenames_latin1" : "filenames_utf8",
                          (beam_bytes){empty, sizeof empty}, &result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN && result.value.size == sizeof ok && !memcmp(result.value.data, ok, sizeof ok));
        beam_result_release(&result);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
        CHECK(joined_threads(workers) == workers && entries("/proc/self/fd") == descriptors);
    }
    beam_composition_release(&observers);
    beam_vm_options_init(&options);
    {
        beam_composition *composition = NULL;
        bytecode_input base = {0};
        CHECK(bytecode_input_read(&base, "/no/such/base-inventory", INPUT_BASE, &error) == BEAM_RUNTIME_NOT_FOUND);
        CHECK(bytecode_input_read(&base, argv[1], INPUT_BASE, &error) == BEAM_OK);
        size_t base_count = base.count;
        CHECK(base_count == sizeof base_names / sizeof base_names[0]);
        beam_composition_options prep = {.struct_size = sizeof prep,
            .modules = base.modules, .module_count = base_count,
            .builtin_binding_count = BEAM_TEST_MODERN_BINDING_COUNT,
            .builtin_bindings = beam_test_modern_bindings};
        beam_module_spec *duplicate = calloc(base_count + 1, sizeof(*duplicate));
        CHECK(duplicate != NULL);
        memcpy(duplicate, base.modules, base_count * sizeof(*base.modules)); duplicate[base_count] = base.modules[0];
        prep.modules = duplicate; prep.module_count = base_count + 1;
        CHECK(beam_composition_prepare(&prep, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition);
        CHECK(beam_bytecode_verify(beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode, duplicate, base_count + 1, &error) == BEAM_INVALID_ARGUMENT);
        prep.module_count = base_count;
        size_t conflict_index = 0;
        while (conflict_index < base_count && strcmp(base.modules[conflict_index].name,"beam_maps"))
            ++conflict_index;
        CHECK(conflict_index < base_count);
        duplicate[conflict_index] = (beam_module_spec){"beam_maps", {base_conflict, sizeof base_conflict}, "conflict:base"};
        CHECK(beam_bytecode_verify(beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode, duplicate, base_count, &error) == BEAM_INVALID_BEAM);
        duplicate[conflict_index] = base.modules[conflict_index]; duplicate[conflict_index].name = "wrong_name";
        CHECK(beam_composition_prepare(&prep, &composition, &error) == BEAM_INVALID_BEAM && !composition);
        CHECK(beam_bytecode_verify(beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode, duplicate, base_count, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(beam_bytecode_verify(beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode, base.modules, base_count - 1, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(entries("/proc/self/task") == workers && entries("/proc/self/fd") == descriptors);
        prep.modules = base.modules;
        CHECK(beam_composition_prepare(&prep, &composition, &error) == BEAM_OK);
        free(duplicate);
        bytecode_input_release(&base); /* Preparation owns bytes and origin strings. */
        const beam_module_spec *inventory = NULL; size_t count = 0;
        CHECK(beam_composition_get_modules(composition, &inventory, &count) == BEAM_OK && count == base_count);
        for (size_t i = 0; i < count; ++i) {
            CHECK(!strcmp(inventory[i].name, base_names[i]));
            CHECK(strstr(inventory[i].origin, "/lib/beam/library/ebin/") != NULL);
        }
        CHECK(entries("/proc/self/task") == workers && entries("/proc/self/fd") == descriptors);
        options.composition = composition;
        for (int pass = 0; pass < 3; ++pass) {
            CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
            CHECK(module_count(vm, 6 + base_count));
            /* VM initialization owns polling descriptors. Base composition
             * must add nothing beyond the observed six-module baseline. */
            CHECK(entries("/proc/self/task") == workers && entries("/proc/self/fd") == initialized_descriptors);
            for (size_t i = 0; i < count; ++i)
                CHECK(beam_module_get_info(vm, base_names[i], &info) == BEAM_OK && info.flags == BEAM_MODULE_CURRENT);
            CHECK(beam_module_load(vm, (beam_bytes){base_contract, sizeof base_contract}) == BEAM_OK);
            CHECK(beam_vm_start(vm, &error) == BEAM_OK);
            CHECK(beam_vm_call(vm, "base_contract", "check", (beam_bytes){empty, sizeof empty}, &result) == BEAM_OK);
            CHECK(result.kind == BEAM_RETURN && result.value.size == sizeof ok && !memcmp(result.value.data, ok, sizeof ok));
            beam_result_release(&result);
            CHECK(beam_module_unload(vm, "beam_format", &error) == BEAM_OK);
            CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
            CHECK(joined_threads(workers) == workers && entries("/proc/self/fd") == descriptors);
        }
        beam_composition_release(&composition);
        options.composition = NULL;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK && module_count(vm, 6));
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    CHECK(beam_terminate(&system) == BEAM_OK);
    puts("Explicit base library completed; C continued.");
    return 0;
}
