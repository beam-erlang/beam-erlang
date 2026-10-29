/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_HOST_H
#define BEAM_HOST_H
#include "library.h"
#include "native_image.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Explicit host policy, separate from passive library descriptors. */
typedef struct beam_startup_action {
    const char *module;
    const char *function;
    /* ETF proper argument list; an empty span means []. */
    beam_bytes arguments;
} beam_startup_action;

typedef struct beam_host_options {
    size_t struct_size;
    /* Optional generic inputs; copied and merged with library inputs. */
    const beam_composition_options *composition;
    size_t library_count;
    beam_library *const *libraries;
    size_t startup_count;
    const beam_startup_action *startup;
    /* Reviewed deferred initialization, including on_load and rollback.
     * This policy uses explicitly supplied beam_startup bytecode. */
    size_t deferred_count;
    const beam_module_spec *deferred;
} beam_host_options;

typedef struct beam_host beam_host;
beam_status beam_host_prepare(const beam_host_options *, beam_host **, beam_error *);
/* Borrowed until release. The initialized VM independently retains it. */
const beam_composition *beam_host_composition(const beam_host *);
/* Run explicit startup and complete explicit bindings on an already started VM.
 * Failure stops the VM. The caller runs startup once for each VM lifetime. */
beam_status beam_host_start(beam_host *, beam_vm *, beam_error *);
void beam_host_release(beam_host **);

/* Acquire an explicit NIF image without invoking its initializer. The returned
 * binding and owner can be passed to generic composition; release the image
 * after preparation has retained its owner. Output is unchanged on failure. */
beam_status beam_host_open_nif(const char *path, const char *module,
                               beam_native_image **image, beam_nif_binding *binding,
                               beam_native_owner *owner, beam_error *error);
#ifdef __cplusplus
}
#endif
#endif
