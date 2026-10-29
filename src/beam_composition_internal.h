/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_COMPOSITION_INTERNAL_H
#define BEAM_COMPOSITION_INTERNAL_H
#include <beam/runtime.h>

typedef struct beam_selected_nif {
    char *module;
    void *initializer;
    int resident_initializer;
} beam_selected_nif;

struct beam_composition {
    unsigned references;
    unsigned native_admission;
    size_t module_count;
    beam_module_spec *modules;
    size_t nif_count;
    beam_selected_nif *nifs;
    size_t builtin_nif_count;
    beam_builtin_nif *builtin_nifs;
    size_t builtin_driver_count;
    beam_builtin_driver *builtin_drivers;
    char *program_helper;
    size_t staged_count;
    size_t preload_count;
    beam_module_spec *preloads;
    size_t native_owner_count;
    beam_native_owner *native_owners;
    size_t embedded_dependency_count;
    beam_embedded_dependency *embedded_dependencies;
};
void beam_composition_retain(const beam_composition *composition);
beam_status beam_composition_stage(beam_vm *vm, const beam_composition *composition,
                                   beam_error *error);
#endif
