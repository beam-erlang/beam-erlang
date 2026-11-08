/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/process.h>
#include "module.h"

int main()
{
    beam_system *system = nullptr;
    beam_vm *vm = nullptr;
    beam_vm_options options;
    beam_vm_options_init(&options);
    beam_process_query process_query;
    beam_process_spawn_options spawn_options;
    beam_process_query_init(&process_query);
    beam_process_spawn_options_init(&spawn_options);
    if (process_query.struct_size != sizeof process_query || process_query.fields ||
        spawn_options.struct_size != sizeof spawn_options ||
        !beam_enif_is_pid_undefined(&spawn_options.group_leader)) return 1;

    beam_result result{};
    beam_module_info module{sizeof(beam_module_info), 0};
    beam_module_list modules{};
    beam_module_query query;
    beam_module_query_init(&query);
    query.fields = BEAM_MODULE_FIELD_EXPORTS;
    beam_module_snapshot snapshot{};
    beam_module_name name{"bare_example", sizeof("bare_example") - 1};
    beam_scheduler_info schedulers{sizeof(beam_scheduler_info), 0, 0, 0, 0, 0};
    beam_scheduler_snapshot scheduler_snapshot{};
    beam_process_list process_list{};
    const unsigned char empty[] = {131, 106};
    if (beam_init(&system, nullptr) != BEAM_OK ||
        beam_vm_init(system, &options, &vm, nullptr) != BEAM_OK ||
        beam_module_load(vm, {bare_module, sizeof bare_module}) != BEAM_OK ||
        beam_module_get_info(vm, "bare_example", &module) != BEAM_OK ||
        module.flags != BEAM_MODULE_CURRENT ||
        beam_module_inspect(vm, name, &query, &snapshot) != BEAM_OK ||
        snapshot.count != 1 || snapshot.items[0].flags != BEAM_MODULE_CURRENT ||
        snapshot.items[0].exports.count == 0 ||
        beam_module_list_names(vm, &modules) != BEAM_OK ||
        beam_scheduler_get_info(vm, &schedulers) != BEAM_OK ||
        schedulers.schedulers != 1 ||
        beam_vm_start(vm, nullptr) != BEAM_OK ||
        beam_scheduler_inspect_all(vm, &scheduler_snapshot) != BEAM_OK ||
        beam_process_list_pids(vm, &process_list) != BEAM_OK ||
        beam_scheduler_set_online(vm, 1, 1, nullptr) != BEAM_OK ||
        beam_vm_call(vm, "bare_example", "answer", {empty, sizeof empty}, &result) != BEAM_OK)
        return 1;
    bool ok = result.kind == BEAM_RETURN && result.value.size == 3 &&
              result.value.data[0] == 131 && result.value.data[1] == 97 &&
              result.value.data[2] == 42;
    int status = -1;
    if (beam_module_unload(vm, "bare_example", nullptr) != BEAM_OK)
        return 1;
    if (beam_vm_stop(vm, &status) != BEAM_OK || status != 0)
        return 1;
    if (beam_vm_terminate(&vm, &status) != BEAM_OK || vm != nullptr ||
        beam_terminate(&system) != BEAM_OK || system != nullptr)
        return 1;
    beam_result_release(&result);
    beam_module_list_release(&modules);
    if (snapshot.count != 1 || snapshot.items[0].name.size != name.size)
        return 1;
    beam_module_snapshot_release(&snapshot);
    if (!scheduler_snapshot.count || !process_list.count) return 1;
    beam_scheduler_snapshot_release(&scheduler_snapshot);
    beam_process_list_release(&process_list);
    return ok ? 0 : 1;
}
