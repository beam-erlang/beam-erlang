/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include "module.h"

int main()
{
    beam_system *system = nullptr;
    beam_vm *vm = nullptr;
    beam_vm_options options;
    beam_vm_options_init(&options);

    beam_result result{};
    beam_module_info module{sizeof(beam_module_info), 0};
    beam_module_list modules{};
    beam_scheduler_info schedulers{sizeof(beam_scheduler_info), 0, 0, 0, 0, 0};
    const unsigned char empty[] = {131, 106};
    if (beam_init(&system, nullptr) != BEAM_OK ||
        beam_vm_init(system, &options, &vm, nullptr) != BEAM_OK ||
        beam_vm_load(vm, {bare_module, sizeof bare_module}) != BEAM_OK ||
        beam_vm_get_module(vm, "bare_example", &module) != BEAM_OK ||
        module.flags != BEAM_MODULE_CURRENT ||
        beam_vm_list_modules(vm, &modules) != BEAM_OK ||
        beam_vm_get_schedulers(vm, &schedulers) != BEAM_OK ||
        schedulers.schedulers != 1 ||
        beam_vm_start(vm, nullptr) != BEAM_OK ||
        beam_vm_set_schedulers_online(vm, 1, 1, nullptr) != BEAM_OK ||
        beam_vm_call(vm, "bare_example", "answer", {empty, sizeof empty}, &result) != BEAM_OK)
        return 1;
    bool ok = result.kind == BEAM_RETURN && result.value.size == 3 &&
              result.value.data[0] == 131 && result.value.data[1] == 97 &&
              result.value.data[2] == 42;
    int status = -1;
    if (beam_vm_unload(vm, "bare_example", nullptr) != BEAM_OK)
        return 1;
    if (beam_vm_stop(vm, &status) != BEAM_OK || status != 0)
        return 1;
    if (beam_vm_terminate(&vm, &status) != BEAM_OK || vm != nullptr ||
        beam_terminate(&system) != BEAM_OK || system != nullptr)
        return 1;
    beam_result_release(&result);
    beam_module_list_release(&modules);
    return ok ? 0 : 1;
}
