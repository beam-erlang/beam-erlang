/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_LIBRARY_H
#define BEAM_LIBRARY_H
#include "runtime.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental exact-SDK container. It describes data and resident native
 * registrations only; it specifies no startup, dependencies or VM policy. */
#define BEAM_LIBRARY_FORMAT_VERSION 1
typedef struct beam_library_descriptor {
    size_t struct_size;
    unsigned format_version;
    const char *build_id;
    const char *name;
    size_t module_count;
    const beam_module_spec *modules;
    size_t nif_count;
    const beam_builtin_nif *nifs;
} beam_library_descriptor;

typedef struct beam_library beam_library;
/* Shared images export the constant data symbol beam_library_descriptor_v1.
 * Output is unchanged on failure. Mapping may execute native constructors. */
beam_status beam_library_open(const char *path, beam_library **out, beam_error *error);
/* The descriptor, bytecode and native code must remain resident while any
 * library/composition/VM reference exists. Intended for linked executables. */
beam_status beam_library_from_static(const beam_library_descriptor *descriptor,
                                     beam_library **out, beam_error *error);
void beam_library_retain(beam_library *library);
void beam_library_release(beam_library **library);
/* Borrow copied inventory metadata and a lifetime owner. The caller supplies
 * these through composition's ordinary modules, builtin_nifs and native_owners.
 * Successful composition preparation acquires an independent owner reference. */
typedef struct beam_library_inputs {
    size_t struct_size;
    size_t module_count;
    const beam_module_spec *modules;
    size_t nif_count;
    const beam_builtin_nif *nifs;
    beam_native_owner owner;
} beam_library_inputs;
beam_status beam_library_get_inputs(beam_library *library, beam_library_inputs *out);

#ifdef __cplusplus
}
#endif
#endif
