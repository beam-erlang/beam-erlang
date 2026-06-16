/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_BYTECODE_H
#define BEAM_BYTECODE_H

#include <beam.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Producer-generated expectations. Paths are relative to the selected prefix;
 * they describe acquisition inputs and are never opened by the verifier. */
typedef struct beam_bytecode_record {
    const char *name;
    const char *relative_path;
    size_t size;
    unsigned char sha256[32];
} beam_bytecode_record;

typedef struct beam_bytecode_inventory {
    const char *build_id;
    size_t count;
    const beam_bytecode_record *records;
} beam_bytecode_inventory;

/* Verify a complete inventory in any order against the loaded runtime and the
 * producer's expectations. No I/O, allocation, initialization or loading occurs.
 * Origins are caller-owned provenance labels. Pass these same verified buffers
 * to composition before releasing them. This source helper is compiled into the
 * host; it is not an exported libbeam function or a bytecode authenticity check. */
beam_status beam_bytecode_verify(const beam_bytecode_inventory *inventory,
                                const beam_module_spec *modules, size_t count,
                                beam_error *error);

#ifdef __cplusplus
}
#endif
#endif
