/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "atom.h"
#include "module.h"
#include "code_ix.h"
#include "export.h"
#include "erl_nif.h"
#include "beam_asm.h"
#include "beam_internal.h"
#include "beam_module_inspection.h"

#include <fenv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct inspection_member {
    Eterm atom;
    Module *module;
    Export *export;
} inspection_member;

void beam_module_query_init(beam_module_query *query)
{
    if (query) {
        memset(query, 0, sizeof(*query));
        query->struct_size = sizeof(*query);
        query->scopes = BEAM_MODULE_SCOPE_LOADED;
    }
}

static void release_functions(beam_module_functions *functions)
{
    size_t i;
    for (i = 0; i < functions->count; ++i)
        free((void *)functions->items[i].name.data);
    free((void *)functions->items);
}

static void release_code_info(beam_module_code_info *code)
{
    release_functions(&code->functions);
    release_functions(&code->nifs);
    free((void *)code->attributes_etf.data);
    free((void *)code->compile_info_etf.data);
}

void beam_module_snapshot_release(beam_module_snapshot *snapshot)
{
    size_t i;
    if (!snapshot)
        return;
    for (i = 0; i < snapshot->count; ++i) {
        beam_module_record *record = (beam_module_record *)&snapshot->items[i];
        free((void *)record->name.data);
        release_code_info(&record->current);
        release_code_info(&record->old);
        release_code_info(&record->pending);
        release_functions(&record->exports);
    }
    free((void *)snapshot->items);
    memset(snapshot, 0, sizeof(*snapshot));
}

static beam_status copy_atom_name(Eterm identity, beam_module_name *out)
{
    Atom *atom = atom_tab(atom_val(identity));
    char *name = malloc((size_t)atom->len + 1);
    if (!name)
        return BEAM_OUT_OF_MEMORY;
    memcpy(name, erts_atom_get_name(atom), atom->len);
    name[atom->len] = '\0';
    out->data = name;
    out->size = atom->len;
    return BEAM_OK;
}

static beam_status copy_metadata(const byte *data, UWord size, beam_bytes *out)
{
    unsigned char *copy;
    if (!data || !size)
        return BEAM_OK;
    if ((UWord)(size_t)size != size)
        return BEAM_OUT_OF_MEMORY;
    copy = malloc((size_t)size);
    if (!copy)
        return BEAM_OUT_OF_MEMORY;
    memcpy(copy, data, (size_t)size);
    out->data = copy;
    out->size = (size_t)size;
    return BEAM_OK;
}

static beam_status allocate_functions(size_t count, beam_module_functions *out)
{
    beam_module_function *items;
    if (!count)
        return BEAM_OK;
    if (count > SIZE_MAX / sizeof(*items))
        return BEAM_OUT_OF_MEMORY;
    items = calloc(count, sizeof(*items));
    if (!items)
        return BEAM_OUT_OF_MEMORY;
    out->items = items;
    out->count = count;
    return BEAM_OK;
}

static beam_status copy_code_functions(const BeamCodeHeader *header,
                                       beam_module_functions *out)
{
    size_t i, count = 0, position = 0;
    beam_status status;
    for (i = 0; i < header->num_functions; ++i) {
        const ErtsCodeInfo *info = header->functions[i];
        /* Match functions_in_module: NIL-name BIF stubs have no named entry. */
        if (is_atom(info->mfa.function))
            ++count;
    }
    status = allocate_functions(count, out);
    if (status != BEAM_OK)
        return status;
    for (i = 0; i < header->num_functions; ++i) {
        const ErtsCodeInfo *info = header->functions[i];
        if (is_atom(info->mfa.function)) {
            beam_module_function *function =
                (beam_module_function *)&out->items[position++];
            status = copy_atom_name(info->mfa.function, &function->name);
            if (status != BEAM_OK)
                return status;
            function->arity = info->mfa.arity;
        }
    }
    return BEAM_OK;
}

static beam_status copy_nif_functions(const struct erl_module_nif *nif,
                                      beam_module_functions *out)
{
    ErlNifFunc *functions;
    int count, i;
    beam_status status;
    if (!nif)
        return BEAM_OK;
    count = erts_nif_get_funcs(nif, &functions);
    if (count < 0)
        return BEAM_SYSTEM_ERROR;
    status = allocate_functions((size_t)count, out);
    if (status != BEAM_OK)
        return status;
    for (i = 0; i < count; ++i) {
        beam_module_function *function = (beam_module_function *)&out->items[i];
        Eterm atom;
        /* The NIF loader validates Latin-1 descriptor names against existing
         * code-function atoms. Copy those atoms' UTF-8 rather than interning or
         * treating descriptor bytes as UTF-8. */
        if (!erts_atom_get(functions[i].name, strlen(functions[i].name),
                           &atom, ERTS_ATOM_ENC_LATIN1))
            return BEAM_SYSTEM_ERROR;
        status = copy_atom_name(atom, &function->name);
        if (status != BEAM_OK)
            return status;
        function->arity = functions[i].arity;
    }
    return BEAM_OK;
}

static beam_status copy_code_info(const struct erl_module_instance *instance,
                                  unsigned fields, beam_module_code_info *out)
{
    const BeamCodeHeader *header = instance->code_hdr;
    beam_status status;
    if (!header)
        return BEAM_OK;
    if (instance->code_length < 0 || !header->md5_ptr ||
        (UWord)(size_t)header->num_functions != header->num_functions)
        return BEAM_SYSTEM_ERROR;
    out->loaded_code_bytes = (size_t)instance->code_length;
    out->function_table_count = (size_t)header->num_functions;
    out->has_nif = instance->nif != NULL;
    out->has_line_table = header->line_table != NULL;
    memcpy(out->md5, header->md5_ptr, sizeof(out->md5));
    if (fields & BEAM_MODULE_FIELD_FUNCTIONS) {
        status = copy_code_functions(header, &out->functions);
        if (status != BEAM_OK)
            return status;
    }
    if (fields & BEAM_MODULE_FIELD_NIFS) {
        status = copy_nif_functions(instance->nif, &out->nifs);
        if (status != BEAM_OK)
            return status;
    }
    if (fields & BEAM_MODULE_FIELD_ATTRIBUTES) {
        status = copy_metadata(header->attr_ptr, header->attr_size,
                                &out->attributes_etf);
        if (status != BEAM_OK)
            return status;
    }
    if (fields & BEAM_MODULE_FIELD_COMPILE) {
        status = copy_metadata(header->compile_ptr, header->compile_size,
                                &out->compile_info_etf);
        if (status != BEAM_OK)
            return status;
    }
    return BEAM_OK;
}

static int member_compare(const void *left, const void *right)
{
    Eterm a = ((const inspection_member *)left)->atom;
    Eterm b = ((const inspection_member *)right)->atom;
    return (a > b) - (a < b);
}

static int record_compare(const void *left, const void *right)
{
    const beam_module_name *a = &((const beam_module_record *)left)->name;
    const beam_module_name *b = &((const beam_module_record *)right)->name;
    size_t size = a->size < b->size ? a->size : b->size;
    int comparison = memcmp(a->data, b->data, size);
    return comparison ? comparison : (a->size > b->size) - (a->size < b->size);
}

static int group_selected(const inspection_member *first,
                          const inspection_member *last, unsigned scopes)
{
    for (; first < last; ++first)
        if (((scopes & BEAM_MODULE_SCOPE_LOADED) && first->module) ||
            ((scopes & BEAM_MODULE_SCOPE_CALLABLE) && first->export))
            return 1;
    return 0;
}

static beam_status copy_record(const inspection_member *first,
                               const inspection_member *last, unsigned fields,
                               beam_module_record *out)
{
    const inspection_member *entry;
    Module *module = NULL;
    size_t exports = 0, position = 0;
    beam_status status = copy_atom_name(first->atom, &out->name);
    if (status != BEAM_OK)
        return status;
    out->fields = fields;
    /* A binary identity containing NUL must not match a core-name prefix. */
    if (!memchr(out->name.data, '\0', out->name.size) &&
        erts_bare_is_core_module(out->name.data))
        out->flags |= BEAM_MODULE_PROTECTED;
    for (entry = first; entry < last; ++entry) {
        if (entry->module)
            module = entry->module;
        if (entry->export)
            ++exports;
    }
    if (module) {
        if (module->curr.code_hdr) {
            out->flags |= BEAM_MODULE_CURRENT;
            status = copy_code_info(&module->curr, fields, &out->current);
            if (status != BEAM_OK)
                return status;
        }
        if (module->old.code_hdr) {
            out->flags |= BEAM_MODULE_OLD;
            status = copy_code_info(&module->old, fields, &out->old);
            if (status != BEAM_OK)
                return status;
        }
        if (module->on_load) {
            out->flags |= BEAM_MODULE_ON_LOAD;
            status = copy_code_info(module->on_load, fields, &out->pending);
            if (status != BEAM_OK)
                return status;
        }
    }
    if (fields & BEAM_MODULE_FIELD_EXPORTS) {
        status = allocate_functions(exports, &out->exports);
        if (status != BEAM_OK)
            return status;
        for (entry = first; entry < last; ++entry) {
            if (entry->export) {
                const ErtsCodeMFA *mfa = &entry->export->info.mfa;
                beam_module_function *function =
                    (beam_module_function *)&out->exports.items[position++];
                status = copy_atom_name(mfa->function, &function->name);
                if (status != BEAM_OK)
                    return status;
                function->arity = mfa->arity;
            }
        }
    }
    return BEAM_OK;
}

beam_status erts_bare_inspect_modules(const beam_module_name *name,
                                      const beam_module_query *query,
                                      int initial,
                                      beam_module_snapshot *out)
{
    static const unsigned valid_scopes =
        BEAM_MODULE_SCOPE_LOADED | BEAM_MODULE_SCOPE_CALLABLE;
    static const unsigned valid_fields =
        BEAM_MODULE_FIELD_EXPORTS | BEAM_MODULE_FIELD_FUNCTIONS |
        BEAM_MODULE_FIELD_NIFS | BEAM_MODULE_FIELD_ATTRIBUTES |
        BEAM_MODULE_FIELD_COMPILE;
    beam_module_snapshot snapshot = {0};
    inspection_member *members = NULL;
    beam_module_record *records;
    beam_status status = BEAM_OK;
    ErtsCodeIndex ix;
    Eterm wanted = THE_NON_VALUE;
    size_t capacity, count = 0, group, end, result_count = 0, position = 0;
    int modules, exports, i;
    fenv_t environment;

    if (!query || !out || query->struct_size != sizeof(*query) ||
        !query->scopes || (query->scopes & ~valid_scopes) ||
        (query->fields & ~valid_fields))
        return BEAM_INVALID_ARGUMENT;
    if (name) {
        const byte *text_end;
        Uint characters;
        if (!name->data || name->size > MAX_ATOM_SZ_LIMIT ||
            erts_analyze_utf8_x((const byte *)name->data, name->size, &text_end,
                                &characters, NULL, NULL,
                                MAX_ATOM_CHARACTERS) != ERTS_UTF8_OK)
            return BEAM_INVALID_ARGUMENT;
    }
    ERTS_LC_ASSERT(initial || erts_has_code_load_permission());
    if (initial && (fegetenv(&environment) || fesetenv(FE_DFL_ENV)))
        ERTS_INTERNAL_ERROR("cannot enter native module inspection");
    ix = initial ? erts_staging_code_ix() : erts_active_code_ix();
    if (name && !erts_atom_get(name->data, name->size, &wanted,
                               ERTS_ATOM_ENC_UTF8)) {
        status = BEAM_NOT_FOUND;
        goto done;
    }
    modules = module_code_size(ix);
    exports = (query->scopes & BEAM_MODULE_SCOPE_CALLABLE) ||
              (query->fields & BEAM_MODULE_FIELD_EXPORTS)
        ? export_list_size(ix) : 0;
    if ((size_t)modules > SIZE_MAX - (size_t)exports) {
        status = BEAM_OUT_OF_MEMORY;
        goto done;
    }
    capacity = (size_t)modules + (size_t)exports;
    if (capacity > SIZE_MAX / sizeof(*members)) {
        status = BEAM_OUT_OF_MEMORY;
        goto done;
    }
    if (capacity) {
        members = malloc(capacity * sizeof(*members));
        if (!members) {
            status = BEAM_OUT_OF_MEMORY;
            goto done;
        }
    }
    for (i = 0; i < modules; ++i) {
        Module *module = module_code(i, ix);
        Eterm atom = make_atom(module->module);
        if ((!name || atom == wanted) &&
            (module->curr.code_hdr || module->old.code_hdr || module->on_load))
            members[count++] = (inspection_member){atom, module, NULL};
    }
    for (i = 0; i < exports; ++i) {
        Export *export = export_list(i, ix);
        if (name && export->info.mfa.module != wanted)
            continue;
        /* Match exported_from_module, including callable traced exports and
         * selected native-only destinations, excluding unresolved stubs. */
        if (erts_is_export_trampoline_active(export, ix) &&
            BeamIsOpCode(export->trampoline.common.op, op_call_error_handler))
            continue;
        members[count++] = (inspection_member){export->info.mfa.module,
                                                NULL, export};
    }
    if (count)
        qsort(members, count, sizeof(*members), member_compare);
    for (group = 0; group < count; group = end) {
        for (end = group + 1; end < count &&
             members[end].atom == members[group].atom; ++end)
            ;
        if (group_selected(members + group, members + end, query->scopes))
            ++result_count;
    }
    if (name && !result_count) {
        status = BEAM_NOT_FOUND;
        goto done;
    }
    if (result_count > SIZE_MAX / sizeof(*records)) {
        status = BEAM_OUT_OF_MEMORY;
        goto done;
    }
    records = result_count ? calloc(result_count, sizeof(*records)) : NULL;
    if (result_count && !records) {
        status = BEAM_OUT_OF_MEMORY;
        goto done;
    }
    snapshot.items = records;
    snapshot.count = result_count;
    for (group = 0; group < count; group = end) {
        for (end = group + 1; end < count &&
             members[end].atom == members[group].atom; ++end)
            ;
        if (group_selected(members + group, members + end, query->scopes)) {
            status = copy_record(members + group, members + end, query->fields,
                                  &records[position++]);
            if (status != BEAM_OK)
                goto done;
        }
    }
    if (result_count)
        qsort(records, result_count, sizeof(*records), record_compare);
done:
    free(members);
    if (initial && fesetenv(&environment))
        ERTS_INTERNAL_ERROR("cannot restore host floating-point environment");
    if (status == BEAM_OK)
        *out = snapshot;
    else
        beam_module_snapshot_release(&snapshot);
    return status;
}
