/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_composition_internal.h"
#include "beam_component_build.h"
#include "beam_platform.h"
#include "beam_core_modules.h"
#include "beam_embedded_dependencies.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

static uint32_t read_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int valid_module(const char *name, beam_bytes bytes)
{
    size_t offset = 12;
    int found = 0;
    if (!name || !*name || !bytes.data || bytes.size < 12 ||
        memcmp(bytes.data, "FOR1", 4) || memcmp(bytes.data + 8, "BEAM", 4) ||
        read_u32(bytes.data + 4) != bytes.size - 8)
        return 0;
    while (offset < bytes.size) {
        size_t size, padded;
        const unsigned char *chunk;
        if (bytes.size - offset < 8)
            return 0;
        size = read_u32(bytes.data + offset + 4);
        chunk = bytes.data + offset + 8;
        if (size > bytes.size - offset - 8)
            return 0;
        if (!memcmp(bytes.data + offset, "AtU8", 4)) {
            size_t length, skip = 5;
            uint32_t count;
            unsigned first;
            if (found || size < 5 || !(count = read_u32(chunk)))
                return 0;
            first = chunk[4];
            if (!(count & 0x80000000u)) {
                length = first;
            } else if (!(first & 8)) {
                length = first >> 4;
            } else if (!(first & 16)) {
                if (size < 6)
                    return 0;
                length = ((first & 0xe0u) << 3) | chunk[5];
                skip = 6;
            } else {
                size_t i, length_bytes = (first >> 5) + 2;
                if (length_bytes > sizeof(size_t) || length_bytes > size - 5)
                    return 0;
                length = 0;
                for (i = 0; i < length_bytes; ++i)
                    length = (length << 8) | chunk[5 + i];
                skip = 5 + length_bytes;
            }
            if (length > size - skip || length != strlen(name) ||
                memcmp(chunk + skip, name, length))
                return 0;
            found = 1;
        }
        padded = (size + 3) & ~(size_t)3;
        if (padded < size || padded > bytes.size - offset - 8)
            return 0;
        offset += 8 + padded;
    }
    return found;
}


const char *beam_build_id(void) { return BEAM_COMPONENT_BUILD_ID; }

static beam_status fail(beam_error *error, beam_status status, const char *message)
{
    if (error) snprintf(error->message, sizeof(error->message), "%s", message);
    return status;
}

void beam_composition_retain(const beam_composition *composition)
{
    if (composition)
        beam_reference_add(&((beam_composition *)composition)->references, 1);
}

void beam_composition_release(beam_composition **out)
{
    beam_composition *composition;
    if (!out || !*out) return;
    composition = *out;
    *out = NULL;
    if (beam_reference_add(&composition->references, -1)) return;
    for (size_t i = 0; i < composition->module_count; ++i) {
        free((void *)composition->modules[i].name);
        free((void *)composition->modules[i].bytes.data);
        free((void *)composition->modules[i].origin);
    }
    for (size_t i = 0; i < composition->nif_count; ++i) {
        free(composition->nifs[i].module);
    }
    for (size_t i = 0; i < composition->builtin_nif_count; ++i) {
        free((void *)composition->builtin_nifs[i].name);
        free((void *)composition->builtin_nifs[i].build_id);
    }
    for (size_t i = 0; i < composition->builtin_driver_count; ++i) {
        free((void *)composition->builtin_drivers[i].name);
        free((void *)composition->builtin_drivers[i].build_id);
    }
    free(composition->builtin_nifs);
    free(composition->builtin_drivers);
    for (size_t i = 0; i < composition->embedded_dependency_count; ++i)
        free((void *)composition->embedded_dependencies[i].name);
    free(composition->embedded_dependencies);
    free(composition->program_helper);
    free(composition->nifs);
    free(composition->modules);
    while (composition->native_owner_count) {
        beam_native_owner *owner = &composition->native_owners[--composition->native_owner_count];
        owner->release(owner->context);
    }
    free(composition->native_owners);
    free(composition);
}

static int valid_utf8(const char *text, size_t maximum_characters)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t characters = 0;
    if (!p || !*p) return 0;
    while (*p) {
        unsigned value = *p++, minimum = 0, rest = 0;
        if (value >= 0xc2 && value <= 0xdf) { value &= 31; rest = 1; minimum = 0x80; }
        else if (value >= 0xe0 && value <= 0xef) { value &= 15; rest = 2; minimum = 0x800; }
        else if (value >= 0xf0 && value <= 0xf4) { value &= 7; rest = 3; minimum = 0x10000; }
        else if (value >= 0x80) return 0;
        while (rest--) {
            if ((*p & 0xc0) != 0x80) return 0;
            value = (value << 6) | (*p++ & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
        if (++characters > maximum_characters) return 0;
    }
    return 1;
}

static beam_status copy_module(beam_composition *composition, const beam_module_spec *module,
                               beam_error *error)
{
    beam_module_spec *copy;
    const char *origin = module->origin ? module->origin : "memory";
    if (!valid_module(module->name, module->bytes))
        return fail(error, BEAM_INVALID_BEAM, "invalid composition module bytes or identity");
    copy = &composition->modules[composition->module_count++];
    copy->name = strdup(module->name);
    copy->origin = strdup(origin);
    copy->bytes.data = malloc(module->bytes.size);
    if (!copy->name || !copy->origin || !copy->bytes.data)
        return fail(error, BEAM_OUT_OF_MEMORY, "could not copy composition module");
    memcpy((void *)copy->bytes.data, module->bytes.data, module->bytes.size);
    copy->bytes.size = module->bytes.size;
    return BEAM_OK;
}

beam_status beam_composition_prepare(const beam_composition_options *options,
                                     beam_composition **out, beam_error *error)
{
    beam_composition *composition;
    beam_status status = BEAM_OK;
    if (error) memset(error, 0, sizeof(*error));
    if (!out || *out || !options || options->struct_size != sizeof(*options) ||
        (options->native_admission & ~31u) ||
        options->embedded_dependency_count > 1024 ||
        (options->embedded_dependency_count && !options->embedded_dependencies) ||
        options->native_owner_count > 4096 ||
        (options->native_owner_count && !options->native_owners) ||
        options->binding_count > 1024 || (options->binding_count && !options->bindings) ||
        options->builtin_nif_count > 1024 || options->builtin_driver_count > 1024 ||
        (options->builtin_nif_count && !options->builtin_nifs) ||
        (options->builtin_driver_count && (!options->builtin_drivers ||
            !(options->native_admission & BEAM_NATIVE_DRIVERS))) ||
        options->module_count > 4096 ||
        options->preload_count > 4096 || (options->preload_count && !options->preloads) ||
        (options->module_count && !options->modules))
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid composition specification");
    composition = calloc(1, sizeof(*composition));
    if (!composition) return fail(error, BEAM_OUT_OF_MEMORY, "could not allocate composition");
    composition->references = 1;
    if (options->embedded_dependency_count) {
        static const char *const reserved[] = {
#define BEAM_EMBEDDED_NAME(name, state) name,
            BEAM_VM_EMBEDDED_DEPENDENCIES(BEAM_EMBEDDED_NAME)
#undef BEAM_EMBEDDED_NAME
        };
        composition->embedded_dependencies = calloc(options->embedded_dependency_count,
                                                     sizeof(beam_embedded_dependency));
        if (!composition->embedded_dependencies) { status = BEAM_OUT_OF_MEMORY; goto failed; }
        for (size_t i = 0; i < options->embedded_dependency_count; ++i) {
            const beam_embedded_dependency *entry = &options->embedded_dependencies[i];
            int valid = entry->struct_size == sizeof(*entry) && valid_utf8(entry->name, 255) &&
                (entry->state == BEAM_EMBEDDED_INCLUDED || entry->state == BEAM_EMBEDDED_EXCLUDED);
            if (valid) {
                for (size_t j = 0; j < sizeof(reserved) / sizeof(*reserved); ++j)
                    if (!strcmp(entry->name, reserved[j])) valid = 0;
                for (size_t j = 0; j < i; ++j)
                    if (!strcmp(entry->name, options->embedded_dependencies[j].name)) valid = 0;
            }
            if (!valid) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "invalid embedded dependency declaration");
                goto failed;
            }
            beam_embedded_dependency *copy =
                &composition->embedded_dependencies[composition->embedded_dependency_count++];
            *copy = *entry;
            copy->name = strdup(entry->name);
            if (!copy->name) { status = BEAM_OUT_OF_MEMORY; goto failed; }
        }
    }
    composition->native_admission = options->native_admission;
    composition->native_owners = calloc(options->native_owner_count ? options->native_owner_count : 1,
                                        sizeof(*composition->native_owners));
    if (!composition->native_owners) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    for (size_t i = 0; i < options->native_owner_count; ++i) {
        const beam_native_owner *owner = &options->native_owners[i];
        if (owner->struct_size != sizeof(*owner) || !owner->retain || !owner->release) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid native lifetime owner"); goto failed;
        }
    }
    for (size_t i = 0; i < options->native_owner_count; ++i) {
        beam_native_owner *owner = &composition->native_owners[composition->native_owner_count++];
        *owner = options->native_owners[i];
        owner->retain(owner->context);
    }
    if (options->program_helper) {
        if (!valid_utf8(options->program_helper, (size_t)-1) || !beam_path_absolute(options->program_helper)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "program helper requires an absolute path");
            goto failed;
        }
        if (!(composition->native_admission & (BEAM_NATIVE_PORTS | BEAM_NATIVE_MANAGED_PORTS))) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "helper requires port admission");
            goto failed;
        }
        int helper_error = 0;
#ifdef _WIN32
        wchar_t *helper = beam_windows_path(options->program_helper);
        DWORD attributes = helper ? GetFileAttributesW(helper) : INVALID_FILE_ATTRIBUTES;
        free(helper);
        if (attributes == INVALID_FILE_ATTRIBUTES) helper_error = ENOENT;
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY) helper_error = EINVAL;
#else
        struct stat helper;
        if (stat(options->program_helper, &helper)) helper_error = errno;
        else if (!S_ISREG(helper.st_mode)) helper_error = EINVAL;
        else if (access(options->program_helper, X_OK)) helper_error = errno;
#endif
        if (helper_error) {
            if (error) error->native_code = helper_error;
            status = fail(error, BEAM_RUNTIME_NOT_FOUND, "program helper is missing or not executable");
            goto failed;
        }
        composition->program_helper = strdup(options->program_helper);
        if (!composition->program_helper) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    }
    size_t total = options->module_count + options->preload_count;
    /* Resolve name conflicts across the whole inventory before validating
     * bytecode identities, independently of host merge order. Owners are
     * already retained because inventory metadata can reside in native images. */
    for (size_t i = 0; i < total; ++i) {
        const beam_module_spec *module = i < options->preload_count
            ? &options->preloads[i] : &options->modules[i - options->preload_count];
        const char *origin = module->origin ? module->origin : "memory";
        if (!valid_utf8(module->name, 255) || !valid_utf8(origin, (size_t)-1)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "module name and origin must be nonempty UTF-8");
            goto failed;
        }
        for (size_t j = 0; j < sizeof(beam_core_module_names) / sizeof(beam_core_module_names[0]); ++j)
            if (!strcmp(module->name, beam_core_module_names[j])) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "composition cannot replace a core module");
                goto failed;
            }
        for (size_t j = 0; j < i; ++j) {
            const beam_module_spec *previous = j < options->preload_count
                ? &options->preloads[j] : &options->modules[j - options->preload_count];
            if (!strcmp(module->name, previous->name)) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "conflicting composition module names");
                goto failed;
            }
        }
    }
    composition->modules = calloc(total ? total : 1, sizeof(*composition->modules));
    if (!composition->modules) {
        status = fail(error, BEAM_OUT_OF_MEMORY, "could not allocate composition inventory");
        goto failed;
    }
    composition->preload_count = options->preload_count;
    composition->preloads = composition->modules + composition->module_count;
    for (size_t i = 0; i < options->preload_count; ++i) {
        status = copy_module(composition, &options->preloads[i], error);
        if (status != BEAM_OK) goto failed;
    }
    composition->staged_count = composition->module_count;
    for (size_t i = 0; i < options->module_count; ++i) {
        status = copy_module(composition, &options->modules[i], error);
        if (status != BEAM_OK) goto failed;
    }
    if (!options->defer_modules) composition->staged_count = composition->module_count;
    composition->nifs = calloc(options->binding_count ? options->binding_count : 1, sizeof(*composition->nifs));
    if (!composition->nifs) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    for (size_t i = 0; i < options->binding_count; ++i) {
        const beam_nif_binding *spec = &options->bindings[i];
        if (spec->struct_size != sizeof(*spec) || !valid_utf8(spec->module, 255) ||
            !spec->build_id || strcmp(spec->build_id, BEAM_COMPONENT_BUILD_ID) ||
            (!!spec->initialize == !!spec->native_initialize)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid native binding or SDK identity"); goto failed;
        }
        int supplied = 0;
        for (size_t j = 0; j < options->module_count; ++j)
            if (!strcmp(spec->module, options->modules[j].name)) supplied = 1;
        for (size_t j = 0; j < composition->nif_count; ++j)
            if (!strcmp(spec->module, composition->nifs[j].module)) supplied = 0;
        if (!supplied) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "native binding requires a unique ordinary module"); goto failed;
        }
        beam_selected_nif *nif = &composition->nifs[composition->nif_count++];
        nif->module = strdup(spec->module);
        if (!nif->module) { status = BEAM_OUT_OF_MEMORY; goto failed; }
        nif->resident_initializer = !!spec->initialize;
        nif->initializer = spec->initialize ? (void *)spec->initialize : (void *)spec->native_initialize;
    }
    composition->builtin_nifs = calloc(options->builtin_nif_count ? options->builtin_nif_count : 1,
                                        sizeof(*composition->builtin_nifs));
    composition->builtin_drivers = calloc(options->builtin_driver_count ? options->builtin_driver_count : 1,
                                           sizeof(*composition->builtin_drivers));
    if (!composition->builtin_nifs || !composition->builtin_drivers) {
        status = fail(error, BEAM_OUT_OF_MEMORY, "could not copy built-in native inventory"); goto failed;
    }
    for (size_t i = 0; i < options->builtin_nif_count; ++i) {
        const beam_builtin_nif *spec = &options->builtin_nifs[i];
        if (spec->struct_size != sizeof(*spec) || !valid_utf8(spec->name, 255) ||
            !spec->build_id || strcmp(spec->build_id, BEAM_COMPONENT_BUILD_ID) ||
            !spec->initialize || (spec->taint != 0 && spec->taint != 1)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid built-in NIF descriptor or SDK identity"); goto failed;
        }
        int supplied = 1;
        for (size_t j = 0; j < sizeof(beam_core_module_names) / sizeof(beam_core_module_names[0]); ++j)
            if (!strcmp(spec->name, beam_core_module_names[j])) supplied = 0;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(spec->name, composition->builtin_nifs[j].name)) supplied = 0;
        for (size_t j = 0; j < composition->nif_count; ++j)
            if (!strcmp(spec->name, composition->nifs[j].module)) supplied = 0;
        if (!supplied) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "built-in NIF conflicts with another native or core selection"); goto failed;
        }
        beam_builtin_nif *copy = &composition->builtin_nifs[composition->builtin_nif_count++];
        *copy = *spec; copy->name = strdup(spec->name); copy->build_id = strdup(spec->build_id);
        if (!copy->name || !copy->build_id) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    }
    for (size_t i = 0; i < options->builtin_driver_count; ++i) {
        const beam_builtin_driver *spec = &options->builtin_drivers[i];
        if (spec->struct_size != sizeof(*spec) || !valid_utf8(spec->name, 255) ||
            !spec->build_id || strcmp(spec->build_id, BEAM_COMPONENT_BUILD_ID) ||
            !spec->initialize || (spec->taint != 0 && spec->taint != 1) ||
            !strcmp(spec->name, "fd") || !strcmp(spec->name, "spawn") || !strcmp(spec->name, "forker")) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid built-in driver descriptor or SDK identity"); goto failed;
        }
        for (size_t j = 0; j < i; ++j) {
            if (!strcmp(spec->name, composition->builtin_drivers[j].name)) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "duplicate built-in driver"); goto failed;
            }
        }
        beam_builtin_driver *copy = &composition->builtin_drivers[composition->builtin_driver_count++];
        *copy = *spec; copy->name = strdup(spec->name); copy->build_id = strdup(spec->build_id);
        if (!copy->name || !copy->build_id) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    }
    *out = composition;
    return BEAM_OK;
failed:
    beam_composition_release(&composition);
    return status;
}

beam_status beam_composition_stage(beam_vm *vm, const beam_composition *composition,
                                   beam_error *error)
{
    beam_status status;
    if (!composition) return BEAM_OK;
    for (size_t i = 0; i < composition->staged_count; ++i) {
        status = beam_vm_load(vm, composition->modules[i].bytes);
        if (status != BEAM_OK) return fail(error, status, "could not stage composition module");
    }
    return BEAM_OK;
}

beam_status beam_composition_get_modules(const beam_composition *composition,
                                         const beam_module_spec **modules, size_t *count)
{
    if (!composition || !modules || !count) return BEAM_INVALID_ARGUMENT;
    *modules = composition->modules;
    *count = composition->module_count;
    return BEAM_OK;
}
