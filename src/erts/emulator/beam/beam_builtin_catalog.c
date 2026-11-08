/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "global.h"
#include "erl_process.h"
#include "atom.h"
#include "export.h"
#include "erl_db.h"
#include "erl_bif_table.h"
#include "beam_builtin_catalog.h"
#include "beam_core_modules.h"
#include "beam_opcodes.h"
#include <string.h>

static const beam_builtin_binding *bindings;
static size_t binding_count;
static const beam_core_export *core_exports;
static size_t core_export_count;
static BifEntry private_bifs[BIF_SIZE];
static Eterm origin_modules[BIF_SIZE];
const void *beam_native_instruction_exports[BEAM_NATIVE_INSTRUCTION_COUNT];
static const unsigned native_traits[BIF_SIZE] = {
#define BEAM_NATIVE_TRAITS(id,flags) [BIF_IMPL_##id] = flags,
#include "beam_native_traits.def"
#undef BEAM_NATIVE_TRAITS
};
static const struct {
    const char *identifier, *function;
    unsigned arity;
} instruction_descriptors[] = {
#define BEAM_NATIVE_INSTRUCTION(id,f,a,op,ret) {#id,#f,a},
#include "beam_native_instructions.def"
#undef BEAM_NATIVE_INSTRUCTION
};
static struct {
    ErtsCodeMFA mfa;
    const void *address;
    uintptr_t code_start, code_end;
    int selected;
} instruction_bindings[BEAM_NATIVE_INSTRUCTION_COUNT];
static Export private_instruction_exports[BEAM_NATIVE_INSTRUCTION_COUNT];

typedef struct {
    const char *implementation, *module, *function;
    unsigned arity;
} CoreExportSlot;
static const CoreExportSlot core_slots[] = {
#define BEAM_CORE_NATIVE(i,m,f,a) {i,m,f,a},
#define BEAM_CORE_ORDINARY(m,f,a) {NULL,m,f,a},
#include "beam_core_exports.def"
#undef BEAM_CORE_NATIVE
#undef BEAM_CORE_ORDINARY
};
#ifdef BEAMASM
static const void *native_export_addresses[BIF_SIZE];

void beam_builtin_set_jit_export_address(int slot, const void *address)
{
    ASSERT(slot >= 0 && slot < BIF_SIZE && *bif_descriptors[slot].implementation);
    native_export_addresses[slot] = address;
}
#endif

const void *beam_builtin_export_address(int slot)
{
    ASSERT(slot >= 0 && slot < BIF_SIZE && *bif_descriptors[slot].implementation);
#ifdef BEAMASM
    ASSERT(native_export_addresses[slot]);
    return native_export_addresses[slot];
#else
    return &bif_trap_exports__[slot].trampoline;
#endif
}

int beam_builtin_implementation(const char *identifier, unsigned arity)
{
    if (!identifier || !*identifier)
        return -1;
    for (int i = 0; i < BIF_SIZE; ++i) {
        const ErtsBifDescriptor *d = &bif_descriptors[i];
        if (*d->implementation && d->arity == arity &&
            !strcmp(identifier, d->implementation))
            return i;
    }
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i)
        if (instruction_descriptors[i].arity == arity &&
            !strcmp(identifier, instruction_descriptors[i].identifier)) return BIF_SIZE + i;
    return -1;
}

int beam_builtin_destination_reserved(const char *module, const char *function, unsigned arity)
{
    for (int i = 0; i < BIF_SIZE; ++i) {
        const ErtsBifDescriptor *d = &bif_descriptors[i];
        if (!*d->implementation && d->arity == arity &&
            !strcmp(module, d->module) && !strcmp(function, d->name))
            return 1;
    }
    return 0;
}

int beam_builtin_core_destination(const beam_builtin_binding *binding)
{
    for (size_t i = 0; i < sizeof core_slots / sizeof *core_slots; ++i) {
        const CoreExportSlot *slot = &core_slots[i];
        if (slot->implementation && slot->arity == binding->arity &&
            !strcmp(slot->module, binding->module) &&
            !strcmp(slot->function, binding->function) &&
            !strcmp(slot->implementation, binding->implementation)) return 1;
    }
    return 0;
}

int beam_core_export_known(const char *module, const char *function, unsigned arity)
{
    for (size_t i = 0; i < sizeof core_slots / sizeof *core_slots; ++i) {
        const CoreExportSlot *slot = &core_slots[i];
        if (!slot->implementation && slot->arity == arity &&
            !strcmp(slot->module, module) && !strcmp(slot->function, function)) return 1;
    }
    return 0;
}

int beam_core_export_selected(const char *module, const char *function, unsigned arity)
{
    for (size_t i = 0; i < core_export_count; ++i)
        if (core_exports[i].arity == arity && !strcmp(core_exports[i].module, module) &&
            !strcmp(core_exports[i].function, function)) return 1;
    return 0;
}

static int atom_named(Eterm atom, const char *name)
{
    const Atom *a = atom_tab(atom_val(atom));
    size_t length = strlen(name);
    return a->len == length && !memcmp(erts_atom_get_name(a), name, length);
}

static int core_module(Eterm module)
{
    for (size_t i = 0; i < sizeof beam_core_module_names / sizeof *beam_core_module_names; ++i)
        if (atom_named(module, beam_core_module_names[i])) return 1;
    return 0;
}

int beam_builtin_identifier_slot(uintptr_t identifier, unsigned arity)
{
    for (int i = 0; i < BIF_SIZE; ++i)
        if (bif_descriptors[i].arity == arity &&
            atom_named(identifier, bif_descriptors[i].implementation)) return i;
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i)
        if (instruction_descriptors[i].arity == arity &&
            atom_named(identifier, instruction_descriptors[i].identifier)) return BIF_SIZE + i;
    return -1;
}

const void *beam_builtin_instruction_mfa(int slot)
{
    ASSERT(slot >= 0 && slot < BEAM_NATIVE_INSTRUCTION_COUNT);
    return &instruction_bindings[slot].mfa;
}

void beam_builtin_set_instruction_address(int slot, const void *address)
{
    ASSERT(slot >= 0 && slot < BEAM_NATIVE_INSTRUCTION_COUNT);
    instruction_bindings[slot].address = address;
}

void beam_builtin_set_instruction_bounds(int slot, const void *start, const void *end)
{
    ASSERT(slot >= 0 && slot < BEAM_NATIVE_INSTRUCTION_COUNT);
    instruction_bindings[slot].code_start = (uintptr_t)start;
    instruction_bindings[slot].code_end = (uintptr_t)end;
}

const void *beam_builtin_instruction_from_pc(uintptr_t pc)
{
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i)
        if (pc >= instruction_bindings[i].code_start && pc < instruction_bindings[i].code_end)
            return &instruction_bindings[i].mfa;
    return NULL;
}

const void *beam_builtin_instruction_address(int slot)
{
    ASSERT(slot >= 0 && slot < BEAM_NATIVE_INSTRUCTION_COUNT);
    return instruction_bindings[slot].address;
}

int beam_builtin_selected_instruction(uintptr_t module, uintptr_t function, unsigned arity)
{
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i) {
        const ErtsCodeMFA *mfa = &instruction_bindings[i].mfa;
        if (instruction_bindings[i].selected && mfa->module == module &&
            mfa->function == function && mfa->arity == arity) return i;
    }
    return -1;
}

void beam_builtin_install_instruction_exports(void)
{
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i) {
        Export *private = &private_instruction_exports[i];
        private->info.mfa = instruction_bindings[i].mfa;
        private->bif_number = -1;
        for (int j = 0; j < ERTS_NUM_CODE_IX; ++j)
            private->dispatch.addresses[j] = (ErtsCodePtr)instruction_bindings[i].address;
        if (!instruction_bindings[i].selected) continue;
        ErtsCodeMFA *mfa = &instruction_bindings[i].mfa;
        erts_put_module(mfa->module);
        Export *ep = erts_export_put(mfa->module,mfa->function,mfa->arity);
        beam_native_instruction_exports[i] = ep;
        memset(&ep->info.u,0,sizeof ep->info.u);
#ifndef BEAMASM
        ep->info.u.op = BeamOpCodeAddr(op_i_func_info_IaaI);
#endif
        ep->info.mfa = *mfa;
        for (int j = 0; j < ERTS_NUM_CODE_IX; ++j)
            ep->dispatch.addresses[j] = (ErtsCodePtr)instruction_bindings[i].address;
    }
}

int beam_builtin_destination_selected(uintptr_t module, uintptr_t function, unsigned arity)
{
    for (size_t i = 0; i < binding_count; ++i)
        if (bindings[i].arity == arity && atom_named(module, bindings[i].module) &&
            atom_named(function, bindings[i].function)) return 1;
    return 0;
}

int beam_builtin_guard_binding(uintptr_t identifier, uintptr_t module,
                               uintptr_t function, unsigned arity)
{
    int slot = -1;
#define BEAM_NATIVE_CONTRACT(id,m,f,a,g) \
    if ((g) && arity == (a) && atom_named(identifier, id)) \
        slot = beam_builtin_implementation(id, a);
#include "beam_native_contracts.def"
#undef BEAM_NATIVE_CONTRACT
    if (slot < 0) return -1;
    return beam_builtin_binding_matches(identifier,module,function,arity);
}

int beam_builtin_binding_matches(uintptr_t identifier, uintptr_t module,
                                 uintptr_t function, unsigned arity)
{
    int slot = beam_builtin_identifier_slot(identifier,arity);
    if (slot < 0) return -1;
    if (slot >= BIF_SIZE)
        return beam_builtin_selected_instruction(module,function,arity) == slot - BIF_SIZE;
    const Export *selected = erts_active_export_entry(module,function,arity);
    return selected && selected->bif_number == slot;
}

int beam_builtin_legacy_primitive(uintptr_t module, uintptr_t function, unsigned arity)
{
    for (size_t i = 0; i < sizeof core_slots / sizeof *core_slots; ++i) {
        const CoreExportSlot *slot = &core_slots[i];
        if (slot->implementation && slot->arity == arity &&
            atom_named(module, slot->module) && atom_named(function, slot->function)) {
            int index = beam_builtin_implementation(slot->implementation, arity);
            return index < BIF_SIZE ? index : -1;
        }
    }
    return -1;
}

const void *beam_builtin_private_entry(int slot)
{
    ASSERT(slot >= 0 && slot < BIF_SIZE);
    return &private_bifs[slot];
}

int beam_builtin_entry_matches(const void *entry, int slot)
{
    ASSERT(slot >= 0 && slot < BIF_SIZE);
    return entry == &bif_table[slot] || entry == &private_bifs[slot];
}

unsigned beam_builtin_entry_traits(const void *entry)
{
    if (!entry) return 0;
    for (int i = 0; i < BIF_SIZE; ++i)
        if (beam_builtin_entry_matches(entry,i)) return native_traits[i];
    return 0;
}

static int mfa_slot(const ErtsCodeMFA *mfa)
{
    for (int i = 0; i < BIF_SIZE; ++i)
        if (mfa == &BIF_TRAP_EXPORT(i)->info.mfa) return i;
    const Export *ep = erts_active_export_entry(mfa->module,mfa->function,mfa->arity);
    return ep ? ep->bif_number : -1;
}

unsigned beam_builtin_mfa_traits(const void *metadata)
{
    int slot = mfa_slot(metadata);
    return slot >= 0 && slot < BIF_SIZE ? native_traits[slot] : 0;
}

uintptr_t beam_builtin_error_module(const void *metadata)
{
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i)
        if (metadata == &instruction_bindings[i].mfa ||
            (beam_native_instruction_exports[i] &&
             metadata == &((const Export *)beam_native_instruction_exports[i])->info.mfa))
            return am_erlang;
    int slot = mfa_slot(metadata);
    const ErtsCodeMFA *mfa = metadata;
    return slot >= 0 && slot < BIF_SIZE && is_atom(origin_modules[slot])
        ? origin_modules[slot] : mfa->module;
}

const void *beam_builtin_private_export(int slot)
{
    if (slot >= BIF_SIZE) {
        ASSERT(slot < BIF_SIZE + BEAM_NATIVE_INSTRUCTION_COUNT);
        return &private_instruction_exports[slot - BIF_SIZE];
    }
    ASSERT(slot >= 0 && slot < BIF_SIZE);
    return BIF_TRAP_EXPORT(slot);
}

void beam_builtin_init_private_exports(void)
{
    for (int i = 0; i < BIF_SIZE; ++i) {
        Eterm module = bif_table[i].module;
        Eterm function = bif_table[i].name;
        const CoreExportSlot *origin = NULL;
        for (size_t j = 0; j < sizeof core_slots / sizeof *core_slots; ++j)
            if (core_slots[j].implementation &&
                !strcmp(core_slots[j].implementation,bif_descriptors[i].implementation)) {
                origin = &core_slots[j];
                origin_modules[i] = am_atom_put(origin->module,strlen(origin->module));
                break;
            }
        if (!is_atom(module)) {
            const char *m = origin ? origin->module : "implementation";
            const char *f = origin ? origin->function : bif_descriptors[i].implementation;
            module = erts_atom_put((const byte *)m, strlen(m), ERTS_ATOM_ENC_UTF8, 0);
            function = erts_atom_put((const byte *)f, strlen(f), ERTS_ATOM_ENC_UTF8, 0);
        }
        private_bifs[i] = bif_table[i];
        private_bifs[i].module = module;
        private_bifs[i].name = function;
        erts_init_trap_export(BIF_TRAP_EXPORT(i), module, function,
                              bif_table[i].arity, bif_table[i].f);
    }
}

int beam_core_export_enabled(uintptr_t module, uintptr_t function, unsigned arity)
{
    if (!core_module(module)) return 1;
    for (size_t i = 0; i < sizeof core_slots / sizeof *core_slots; ++i) {
        const CoreExportSlot *slot = &core_slots[i];
        if (slot->arity != arity || !atom_named(module, slot->module) ||
            !atom_named(function, slot->function)) continue;
        if (!slot->implementation)
            return beam_core_export_selected(slot->module, slot->function, arity);
        int index = beam_builtin_implementation(slot->implementation, arity);
        ASSERT(index >= 0);
        if (index >= BIF_SIZE)
            return beam_builtin_selected_instruction(module,function,arity) == index - BIF_SIZE;
        return bif_table[index].module == module && bif_table[index].name == function;
    }
    return 1;
}

void beam_builtin_reset(void)
{
    erts_db_reset_bif_state();
    bindings = NULL;
    binding_count = 0;
    core_exports = NULL;
    core_export_count = 0;
    memcpy(bif_table, bif_defaults, sizeof(BifEntry) * BIF_SIZE);
    memset(bif_trap_exports__, 0, sizeof(Export) * BIF_SIZE);
    memset(private_bifs, 0, sizeof private_bifs);
    memset(instruction_bindings, 0, sizeof instruction_bindings);
    memset(private_instruction_exports, 0, sizeof private_instruction_exports);
    memset(origin_modules,0,sizeof origin_modules);
    memset(beam_native_instruction_exports,0,sizeof beam_native_instruction_exports);
#ifdef BEAMASM
    memset(native_export_addresses, 0, sizeof(native_export_addresses));
#endif
}

void beam_builtin_begin(const beam_builtin_binding *selected, size_t count,
                        const beam_core_export *ordinary, size_t ordinary_count)
{
    beam_builtin_reset();
    bindings = selected;
    binding_count = count;
    core_exports = ordinary;
    core_export_count = ordinary_count;
}

void beam_builtin_install_names(void)
{
    for (int i = 0; i < BEAM_NATIVE_INSTRUCTION_COUNT; ++i) {
        instruction_bindings[i].mfa.module = am_atom_put("erlang",6);
        const char *function = instruction_descriptors[i].function;
        instruction_bindings[i].mfa.function = am_atom_put(function,strlen(function));
        instruction_bindings[i].mfa.arity = instruction_descriptors[i].arity;
    }
    for (size_t i = 0; i < binding_count; ++i) {
        const beam_builtin_binding *b = &bindings[i];
        int slot = beam_builtin_implementation(b->implementation, b->arity);
        ASSERT(slot >= 0);
        if (slot >= BIF_SIZE) {
            int instruction = slot - BIF_SIZE;
            instruction_bindings[instruction].selected = 1;
            instruction_bindings[instruction].mfa.module =
                erts_atom_put((const byte *)b->module,strlen(b->module),ERTS_ATOM_ENC_UTF8,0);
            instruction_bindings[instruction].mfa.function =
                erts_atom_put((const byte *)b->function,strlen(b->function),ERTS_ATOM_ENC_UTF8,0);
            continue;
        }
        bif_table[slot].module = erts_atom_put((const byte *)b->module,
                                              strlen(b->module), ERTS_ATOM_ENC_UTF8, 0);
        bif_table[slot].name = erts_atom_put((const byte *)b->function,
                                            strlen(b->function), ERTS_ATOM_ENC_UTF8, 0);
    }
}
