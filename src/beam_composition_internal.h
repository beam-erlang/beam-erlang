/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_COMPOSITION_INTERNAL_H
#define BEAM_COMPOSITION_INTERNAL_H
#include "beam_component_internal.h"

typedef struct beam_selected_nif {
    char *module;
    char *path;
    void *mapping;
    void *initializer;
} beam_selected_nif;

struct beam_composition {
    unsigned references;
    unsigned services;
    unsigned native_admission;
    unsigned selected;
    size_t count;
    beam_component *components[3];
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
    size_t activation_count;
    beam_module_spec *activation_modules;
};
static const char *const beam_activation_support_modules[] = {
    "beam_component_services", "beam_host", "beam_output", "beam_code"
};
static inline int beam_composition_needs_support(const beam_composition *composition)
{
    return composition && (composition->services || composition->activation_count);
}
static inline int beam_composition_needs_activation(const beam_composition *composition)
{
    return composition && (beam_composition_needs_support(composition) || composition->nif_count);
}
void beam_composition_retain(const beam_composition *composition);
beam_status beam_composition_stage(beam_vm *vm, const beam_composition *composition,
                                   beam_error *error);
beam_status beam_composition_activation_arguments(const beam_composition *composition,
                                                   beam_buffer *out);
#endif
