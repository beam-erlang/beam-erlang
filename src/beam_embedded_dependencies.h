/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_EMBEDDED_DEPENDENCIES_H
#define BEAM_EMBEDDED_DEPENDENCIES_H

/* One inventory for reserved names and reporting. The reporting caller includes
 * config.h first; name-only expansion does not depend on build configuration. */
#ifdef BEAMASM
#define BEAM_EMBEDDED_ASMJIT BEAM_EMBEDDED_INCLUDED
#else
#define BEAM_EMBEDDED_ASMJIT BEAM_EMBEDDED_EXCLUDED
#endif
#ifdef ERTS_USE_BUILTIN_RYU
#define BEAM_EMBEDDED_RYU BEAM_EMBEDDED_INCLUDED
#else
#define BEAM_EMBEDDED_RYU BEAM_EMBEDDED_EXCLUDED
#endif
#ifdef ERTS_USE_BUILTIN_ZLIB
#define BEAM_EMBEDDED_ZLIB BEAM_EMBEDDED_INCLUDED
#else
#define BEAM_EMBEDDED_ZLIB BEAM_EMBEDDED_EXCLUDED
#endif
#define BEAM_VM_EMBEDDED_DEPENDENCIES(X) \
    X("asmjit", BEAM_EMBEDDED_ASMJIT) \
    X("pcre2", BEAM_EMBEDDED_INCLUDED) \
    X("ryu", BEAM_EMBEDDED_RYU) \
    X("STL", BEAM_EMBEDDED_RYU) \
    X("zlib", BEAM_EMBEDDED_ZLIB)
#endif
