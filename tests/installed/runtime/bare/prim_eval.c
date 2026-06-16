/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <beam/beam.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"
#include "prim_eval.h"

/* Private accounting is acceptance instrumentation, not an SDK interface. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "prim_eval:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

static const unsigned char empty_data[] = {131, 106};
static const beam_bytes empty = {empty_data, sizeof empty_data};

static int flags(beam_vm *vm, unsigned expected)
{
    beam_module_info info = {sizeof info, 0};
    OK(beam_vm_get_module(vm, "prim_eval", &info));
    CHECK(info.flags == expected);
    return 0;
}

static int call(beam_vm *vm, const char *function)
{
    beam_result result = {0};
    OK(beam_vm_call(vm, "bare_prim_eval", function, empty, &result));
    int good = result.kind == BEAM_RETURN && result.value.size == 3 &&
               !memcmp(result.value.data, (unsigned char[]){131, 97, 42}, 3);
    if (!good) {
        fprintf(stderr, "bare_prim_eval:%s outcome=%d ETF=", function, result.kind);
        for (size_t i = 0; i < result.value.size; ++i)
            fprintf(stderr, "%02x", result.value.data[i]);
        fputc('\n', stderr);
    }
    beam_result_release(&result);
    CHECK(good);
    return 0;
}

static int load_copy(beam_vm *vm)
{
    unsigned char *copy = malloc(sizeof installed_prim_eval);
    CHECK(copy);
    memcpy(copy, installed_prim_eval, sizeof installed_prim_eval);
    beam_status status = beam_vm_load(vm, (beam_bytes){copy, sizeof installed_prim_eval});
    memset(copy, 0, sizeof installed_prim_eval);
    free(copy);
    CHECK(status == BEAM_OK);
    CHECK(!flags(vm, BEAM_MODULE_CURRENT));
    return 0;
}

static int unload(beam_vm *vm)
{
    struct timespec start, now;
    beam_status status;
    clock_gettime(CLOCK_MONOTONIC, &start);
    /* The call's result may arrive just before its process leaves the table. */
    do {
        status = beam_vm_unload(vm, "prim_eval", NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (status == BEAM_BUSY && now.tv_sec - start.tv_sec < 10);
    CHECK(status == BEAM_OK);
    CHECK(!flags(vm, 0));
    return 0;
}

static int resources_released(int system_terminated)
{
    size_t allocations, bytes, mappings, mapped_bytes;
    size_t vm_keys, system_keys, vm_callbacks, system_callbacks, blocks, slots;
    size_t vm_locks, system_locks;
    beam_runtime_memory_usage(&allocations, &bytes, &mappings, &mapped_bytes);
    ethr_runtime_usage(&vm_keys, &system_keys, &vm_callbacks, &system_callbacks,
                       &blocks, &slots);
    erts_sys_resource_usage(&vm_locks, &system_locks);
    CHECK(!allocations && !bytes && !mappings && !mapped_bytes &&
          !vm_keys && !vm_callbacks && !vm_locks);
    if (system_terminated)
        CHECK(!system_keys && !system_callbacks && !blocks && !slots && !system_locks);
    return 0;
}

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    int halt_status = -1;
    OK(beam_init(&system, NULL));
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        beam_vm_options options;
        beam_module_list modules = {0};
        beam_vm_options_init(&options);

        options.schedulers = 1 + cycle % 2;
        options.schedulers_online = options.schedulers;
        OK(beam_vm_init(system, &options, &vm, NULL));
        CHECK(!flags(vm, 0));
        OK(beam_vm_list_modules(vm, &modules));
        CHECK(modules.count == 9);
        beam_module_list_release(&modules);
        OK(beam_vm_load(vm, (beam_bytes){bare_prim_eval, sizeof bare_prim_eval}));
        if (cycle % 2 == 0) {
            CHECK(!load_copy(vm));
            CHECK(!unload(vm));
            CHECK(!load_copy(vm));
        }
        OK(beam_vm_start(vm, NULL));
        if (cycle % 2) {
            CHECK(!call(vm, "absent"));
            CHECK(!load_copy(vm));
        }
        CHECK(!call(vm, "exercise"));
        CHECK(!unload(vm));
        CHECK(!call(vm, "absent"));
        CHECK(!load_copy(vm));
        CHECK(!call(vm, "exercise"));
        /* Alternate explicit unloading and reclamation by VM termination. */
        if (cycle % 2) CHECK(!unload(vm));
        OK(beam_vm_stop(vm, &halt_status));
        CHECK(halt_status == 0);
        OK(beam_vm_terminate(&vm, &halt_status));
        CHECK(!vm && halt_status == 0 && !resources_released(0));
    }
    OK(beam_terminate(&system));
    CHECK(!system && !resources_released(1));
    puts("Explicit prim_eval lifetimes completed; C continued.");
    return 0;
}
