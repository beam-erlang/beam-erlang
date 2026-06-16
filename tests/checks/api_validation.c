/* SPDX-License-Identifier: Apache-2.0 */
#include "beam.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call, expected) do { \
    beam_status actual = (call); \
    if (actual != (expected)) { \
        fprintf(stderr, "%d: %s returned %s\n", __LINE__, #call, beam_status_message(actual)); \
        return 1; \
    } \
} while (0)

static int check_library_path(void)
{
    beam_error error = {0};
    size_t required = 0;
    char small[2] = {'x', 'y'};
    char *library;
    CHECK(beam_library_path(NULL, 0, &required, &error), BEAM_OK);
    if (required < 2) return 1;
    CHECK(beam_library_path(small, sizeof small, &required, &error), BEAM_INVALID_ARGUMENT);
    if (small[0] != 'x' || small[1] != 'y') return 1;
    library = malloc(required);
    if (!library) return 1;
    CHECK(beam_library_path(library, required, &required, &error), BEAM_OK);
    if (library[0] != '/' || strlen(library) + 1 != required) return 1;
    free(library);
    CHECK(beam_library_path(NULL, 1, &required, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_library_path(NULL, 0, NULL, &error), BEAM_INVALID_ARGUMENT);
    return 0;
}

int
main(void)
{
    beam_host_info host = {0};
    beam_vm_options vm_options;
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_error error = {0};
    if (check_library_path() != 0) return 1;
    CHECK(beam_query_host(NULL, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_query_host(&host, &error), BEAM_INVALID_ARGUMENT);
    host.struct_size = sizeof host;
    CHECK(beam_query_host(&host, &error), BEAM_OK);
    CHECK(beam_init(NULL, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_vm_init(NULL, NULL, &vm, &error), BEAM_INVALID_ARGUMENT);
    CHECK(beam_init(&system, &error), BEAM_OK);
    beam_vm_options_init(&vm_options);
    if (vm_options.struct_size != sizeof vm_options ||
        vm_options.schedulers != 1 ||
        vm_options.schedulers_online != 1 || vm_options.dirty_cpu_schedulers != 1 ||
        vm_options.dirty_cpu_schedulers_online != 1 || vm_options.dirty_io_schedulers != 1 ||
        vm_options.async_threads != 1 || vm_options.poll_threads != 1 ||
        vm_options.process_limit != 1048576 || vm_options.atom_limit != 1048576 ||
        vm_options.min_heap_size != 233 || vm_options.min_bin_vheap_size != 32768 ||
        vm_options.max_heap_size != 0)
        return 1;
    --vm_options.struct_size;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.schedulers = 0;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.schedulers_online = 2;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.dirty_cpu_schedulers_online = 2;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_heap_size = INT_MAX;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_bin_vheap_size = INT_MAX;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    beam_vm_options_init(&vm_options);
    vm_options.min_heap_size = 234;
    vm_options.max_heap_size = 234;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_INVALID_ARGUMENT);
    if (vm != NULL || error.message[0] == '\0')
        return 1;
    /* Defaults and a fresh explicit composition share one system. */
    CHECK(beam_vm_init(system, NULL, &vm, &error), BEAM_OK);
    CHECK(beam_vm_terminate(&vm, NULL), BEAM_OK);
    beam_vm_options_init(&vm_options);
    beam_composition_options preparation = {0};
    beam_composition *composition = NULL;
    preparation.struct_size = sizeof preparation;
    CHECK(beam_composition_prepare(&preparation, &composition, &error), BEAM_OK);
    vm_options.composition = composition;
    CHECK(beam_vm_init(system, &vm_options, &vm, &error), BEAM_OK);
    CHECK(beam_vm_terminate(&vm, NULL), BEAM_OK);
    beam_composition_release(&composition);
    CHECK(beam_terminate(&system), BEAM_OK);
    if (system != NULL)
        return 1;
    return 0;
}
