/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_CLI_RECIPE_H
#define BEAM_CLI_RECIPE_H

#include "beam.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Discover the installed returning-boot composition and call beam_return_prepare.
 * Base, boot and services are explicitly verified ordinary bytecode. Native
 * file, network and zlib components follow the service bits. Only the CLI
 * composition acquires compiler/shell adapters.
 * Installed path policy belongs to this helper, not to the VM library. */
typedef enum beam_cli_profile { BEAM_CLI_NONE, BEAM_CLI_RUN, BEAM_CLI_INTERACTIVE, BEAM_CLI_PKG } beam_cli_profile;

typedef struct beam_cli_recipe_return_options {
    beam_cli_profile profile;
    size_t struct_size;
    unsigned services;
    const char *runtime_root;
    const char *entry_module;
    const char *entry_function;
    beam_bytes arguments;
    size_t module_count;
    const beam_return_module *modules;
    beam_output_handler output;
    void *output_context;
    beam_code_handler code;
    void *code_context;
    beam_input_handler input;
    void *input_context;
    const beam_vm_options *vm;
} beam_cli_recipe_return_options;

beam_status beam_cli_recipe_return_prepare(const beam_cli_recipe_return_options *options,
                                            beam_return_boot **out, beam_error *error);


/* Private acquisition and reserved inventory for the public-lifecycle project host. */
beam_status beam_cli_project_acquire(const char *root, int shell, beam_return_module **modules,
                                     size_t *count, beam_error *error);
void beam_cli_project_release(beam_return_module *modules, size_t count);
const char *const *beam_cli_project_reserved(size_t *count);

#ifdef __cplusplus
}
#endif
#endif
