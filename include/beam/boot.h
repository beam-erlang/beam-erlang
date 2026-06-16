/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_BOOT_H
#define BEAM_BOOT_H

#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returning boot is a copied, discardable preparation layered on the bare VM.
 * Preparation initializes nothing. Each run uses a fresh VM with its own
 * explicit composition.
 * A run retires every child before the caller observes the outcome. Outcomes
 * contain copied bytes only. */
typedef enum beam_outcome_kind {
    BEAM_OUTCOME_RETURN = 0,
    BEAM_OUTCOME_EXCEPTION = 1,
    BEAM_OUTCOME_FINISH = 2,
    BEAM_OUTCOME_HALT = 3,
    BEAM_OUTCOME_SERVICE_FAILURE = 4
} beam_outcome_kind;

typedef struct beam_outcome {
    size_t struct_size;
    beam_outcome_kind kind;
    int halt_status;
    int has_cleanup_error;
    char cleanup_error[256];
    char service[1024];
    beam_buffer term;
} beam_outcome;

typedef enum beam_output_status {
    BEAM_OUTPUT_OK = 0,
    BEAM_OUTPUT_UNKNOWN = 1,
    BEAM_OUTPUT_NOT_READY = 2,
    BEAM_OUTPUT_BACKPRESSURE = 3,
    BEAM_OUTPUT_FAILED = 4
} beam_output_status;

typedef enum beam_code_status {
    BEAM_CODE_OK = 0,
    BEAM_CODE_NOT_FOUND = 1,
    BEAM_CODE_INVALID = 2,
    BEAM_CODE_UNAVAILABLE = 3
} beam_code_status;

typedef enum beam_input_status {
    BEAM_INPUT_LINE = 0,
    BEAM_INPUT_EOF = 1,
    /* No complete input is available yet. The caller may request input again;
     * this result is neither an empty line nor end of input. */
    BEAM_INPUT_PENDING = 2
} beam_input_status;

/* Handlers run on the owner thread inside the VM call, without VM locks.
 * They must return and must not call back into the VM. Pointers they return
 * stay valid until the handler returns; the library copies them. */
typedef beam_output_status (*beam_output_handler)(void *context,
                                                 beam_bytes destination,
                                                 beam_bytes bytes);
typedef beam_code_status (*beam_code_handler)(void *context, beam_bytes source,
                                             beam_bytes module,
                                             const unsigned char **bytes,
                                             size_t *size);
typedef beam_input_status (*beam_input_handler)(void *context,
                                               const unsigned char **bytes,
                                               size_t *size);

typedef struct beam_return_module {
    const char *name;
    beam_bytes bytes;
    /* Nonzero modules are activated by the controller, including reviewed
     * on_load. Zero modules are loaded before the entry and cannot use on_load. */
    int service;
    /* Optional copied source label for code provenance; NULL means "memory".
     * The VM does not interpret this label as a path or acquire its contents. */
    const char *origin;
} beam_return_module;

typedef struct beam_return_options {
    size_t struct_size;
    unsigned services;
    const char *entry_module;
    const char *entry_function;
    /* Uncompressed ETF proper list, including the version byte. Empty means []. */
    beam_bytes arguments;
    size_t module_count;
    const beam_return_module *modules;
    const char *bindir;
    beam_output_handler output;
    void *output_context;
    beam_code_handler code;
    void *code_context;
    beam_input_handler input;
    void *input_context;
    /* NULL selects conservative VM defaults with one normal scheduler. */
    const beam_vm_options *vm;
    /* Complete ordinary base, boot and services inventories are required.
     * FILE, NETWORK and ZLIB exactly match their service bits; zero native
     * components are valid when those capabilities are unselected.
     * Preparation retains each mapped unit and copies its module bytes without
     * starting a VM or activating native implementations. */
    size_t component_count;
    const beam_component_spec *components;
    /* Explicit dynamic NIF bindings, copied and admitted by composition. */
    size_t nif_count;
    const beam_nif_spec *nifs;
} beam_return_options;

typedef struct beam_return_boot beam_return_boot;

beam_status beam_return_prepare(const beam_return_options *options,
                                beam_return_boot **out, beam_error *error);
beam_status beam_return_discard(beam_return_boot **boot);
/* Initializes a VM when preparation is valid. Initialization failure leaves
 * *boot for retry without reserving a VM lifetime.
 * Successful initialization consumes *boot after the VM is terminated, including
 * when the entry itself fails. */
beam_status beam_return_run(beam_system *system, beam_return_boot **boot,
                            beam_outcome *out, beam_error *error);
void beam_outcome_release(beam_outcome *outcome);

/* Diagnostics: semantic counts of returning-boot native resources. They survive VM teardown
 * and are not cleared by the next initialization. */
int beam_zlib_stream_count(void);
int beam_file_descriptor_count(void);
int beam_socket_count(void);

#ifdef __cplusplus
}
#endif
#endif
