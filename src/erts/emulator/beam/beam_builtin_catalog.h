/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_BUILTIN_CATALOG_H
#define BEAM_BUILTIN_CATALOG_H
#include <beam/runtime.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Immutable string metadata; these queries create no atoms. */
int beam_builtin_implementation(const char *identifier, unsigned arity);
int beam_builtin_identifier_slot(uintptr_t identifier, unsigned arity);
int beam_builtin_destination_reserved(const char *module, const char *function, unsigned arity);
int beam_builtin_core_destination(const beam_builtin_binding *binding);
int beam_core_export_known(const char *module, const char *function, unsigned arity);
int beam_core_export_selected(const char *module, const char *function, unsigned arity);
/* Called only while preparing/loading code, never from native dispatch. */
int beam_core_export_enabled(uintptr_t module, uintptr_t function, unsigned arity);
/* Private execution/diagnostic metadata has no entry in the public export table. */
void beam_builtin_init_private_exports(void);
int beam_builtin_legacy_primitive(uintptr_t module, uintptr_t function, unsigned arity);
const void *beam_builtin_private_entry(int slot);
const void *beam_builtin_private_export(int slot);
int beam_builtin_entry_matches(const void *entry, int slot);
int beam_builtin_destination_selected(uintptr_t module, uintptr_t function, unsigned arity);
/* -1: unknown/non-guard contract, 0: absent/mismatched binding, 1: matching. */
int beam_builtin_guard_binding(uintptr_t identifier, uintptr_t module,
                               uintptr_t function, unsigned arity);
int beam_builtin_binding_matches(uintptr_t identifier, uintptr_t module,
                                 uintptr_t function, unsigned arity);
enum {
    BEAM_NATIVE_NOFAIL = 1u << 0,
    BEAM_NATIVE_NO_RETURN = 1u << 1,
    BEAM_NATIVE_HIDE_EXCEPTION_FRAME = 1u << 2,
    BEAM_NATIVE_EQ_EXACT = 1u << 3,
    BEAM_NATIVE_NE_EXACT = 1u << 4,
    BEAM_NATIVE_GE = 1u << 5,
    BEAM_NATIVE_LT = 1u << 6
};
unsigned beam_builtin_entry_traits(const void *entry);
unsigned beam_builtin_mfa_traits(const void *mfa);
uintptr_t beam_builtin_error_module(const void *mfa);
/* Stable descriptor slots for the existing instruction entrypoints. */
enum {
#define BEAM_NATIVE_INSTRUCTION(id,f,a,op,ret) BEAM_INSTRUCTION_##id,
#include "beam_native_instructions.def"
#undef BEAM_NATIVE_INSTRUCTION
    BEAM_NATIVE_INSTRUCTION_COUNT
};
extern const void *beam_native_instruction_exports[BEAM_NATIVE_INSTRUCTION_COUNT];
const void *beam_builtin_instruction_mfa(int slot);
void beam_builtin_set_instruction_address(int slot, const void *address);
void beam_builtin_set_instruction_bounds(int slot, const void *start, const void *end);
const void *beam_builtin_instruction_from_pc(uintptr_t pc);
const void *beam_builtin_instruction_address(int slot);
int beam_builtin_selected_instruction(uintptr_t module, uintptr_t function, unsigned arity);
void beam_builtin_install_instruction_exports(void);
/* The VM retains the composition owning these names until teardown. */
void beam_builtin_begin(const beam_builtin_binding *bindings, size_t count,
                        const beam_core_export *core_exports, size_t core_count);
void beam_builtin_install_names(void);
void beam_builtin_reset(void);
/* VM-owned native wrappers, independent of ordinary facade code. */
const void *beam_builtin_export_address(int slot);
#ifdef BEAMASM
void beam_builtin_set_jit_export_address(int slot, const void *address);
#endif
#ifdef __cplusplus
}
#endif
#endif
