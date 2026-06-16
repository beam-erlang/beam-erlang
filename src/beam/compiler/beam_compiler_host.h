/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_COMPILER_HOST_H
#define BEAM_COMPILER_HOST_H

#include <beam/beam.h>

typedef struct beam_compiled_source {
    char *module;
    beam_buffer bytes;
} beam_compiled_source;

/* Runs one isolated BEAM VM. A source selects in-memory source execution;
 * otherwise arguments are the compile command's literal arguments. The source
 * result must be zero-initialized and is owned by the caller on success. */
int beam_compiler_execute(beam_system *system, const char *root,
                          const char *source, int argc, char **argv,
                          beam_compiled_source *compiled);
void beam_compiled_source_release(beam_compiled_source *compiled);

#endif
