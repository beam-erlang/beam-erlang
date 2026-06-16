/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_SDK_INVENTORIES_H
#define BEAM_SDK_INVENTORIES_H

#include "bytecode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Separate ordinary bytecode selections; lookup selects no runtime facilities. */
typedef enum beam_sdk_inventory_kind {
    BEAM_SDK_INVENTORY_BASE,
    BEAM_SDK_INVENTORY_BOOT,
    BEAM_SDK_INVENTORY_SERVICES
} beam_sdk_inventory_kind;

typedef struct beam_sdk_inventory {
    const beam_bytecode_inventory *bytecode;
    const char *manifest_path; /* Relative to the explicitly selected prefix. */
    beam_bytes manifest;       /* Exact expected bytes, excluding terminating NUL. */
} beam_sdk_inventory;

/* Borrow immutable producer expectations for the host image's lifetime. Invalid
 * kinds return NULL. No I/O, allocation, runtime identity check, VM initialization
 * or bytecode loading occurs. Link BEAM::SDKSupport with a selected runtime. */
const beam_sdk_inventory *beam_sdk_inventory_get(beam_sdk_inventory_kind kind);

#ifdef __cplusplus
}
#endif
#endif
