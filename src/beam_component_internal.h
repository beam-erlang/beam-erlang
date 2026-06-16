/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_COMPONENT_INTERNAL_H
#define BEAM_COMPONENT_INTERNAL_H

#include <beam/beam.h>
#include <stdint.h>

/* Private matching-build data contract. Merely mapping a component must not
 * execute runtime initialization. Native initializers are resolved by name. */
#define BEAM_COMPONENT_ABI_VERSION 1
#define BEAM_COMPONENT_BASE_VERSION 1
#define BEAM_COMPONENT_NIF_MARKER "beam_component"

typedef struct beam_component_module {
    const char *name;
    const unsigned char *bytes;
    size_t size;
    const char *nif_init_symbol;
} beam_component_module;

typedef struct beam_component_descriptor {
    uint32_t abi_version;
    const char *build_id;
    beam_component_kind kind;
    size_t module_count;
    const beam_component_module *modules;
    uint32_t base_version;
    uint32_t required_components;
} beam_component_descriptor;

typedef struct beam_component beam_component;

static inline int beam_component_kind_valid(beam_component_kind kind)
{
    switch (kind) {
    case BEAM_COMPONENT_FILE: case BEAM_COMPONENT_NETWORK:
    case BEAM_COMPONENT_ZLIB:
        return 1;
    default:
        return 0;
    }
}

beam_status beam_component_acquire(const beam_component_spec *spec,
                                   beam_component **out, beam_error *error);
void beam_component_retain(beam_component *component);
void beam_component_release(beam_component *component);
const beam_component_descriptor *beam_component_data(const beam_component *component);
const char *beam_component_path(const beam_component *component);

/* The owner thread installs this selection before VM initialization and clears
 * it only after all workers and NIF resources have been reclaimed. */
void beam_components_activate(beam_component *const *components, size_t count);
void beam_components_deactivate(void);
/* Returns one owned reference on success. The caller releases it on every
 * failed load, or transfers it to the loaded module's native-code lifetime. */
beam_component *beam_component_bind_nif(const char *module, void **initializer);

/* Structural and declared-module identity validation without a running VM.
 * The VM loader performs full code validation before executing any bytecode. */
int beam_component_valid_module(const char *name, beam_bytes bytes);

#endif
