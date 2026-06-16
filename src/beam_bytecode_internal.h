/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_BYTECODE_INTERNAL_H
#define BEAM_BYTECODE_INTERNAL_H
#include <beam/sdk/bytecode.h>

/* Shared implementation detail for SDK verification and CLI payload integrity. */
void beam_bytecode_sha256(beam_bytes bytes, unsigned char digest[32]);
#endif
