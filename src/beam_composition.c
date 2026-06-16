/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_composition_internal.h"
#include "beam_runtime_image.h"
#include "beam_component_build.h"
#include "beam_platform.h"
#include "beam_core_modules.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#include <stdint.h>

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
    for (size_t i = 0; i < composition->count; ++i)
        beam_component_release(composition->components[i]);
    for (size_t i = 0; i < composition->module_count; ++i) {
        free((void *)composition->modules[i].name);
        free((void *)composition->modules[i].bytes.data);
        free((void *)composition->modules[i].origin);
    }
    for (size_t i = 0; i < composition->nif_count; ++i) {
        if (composition->nifs[i].mapping) beam_dlclose(composition->nifs[i].mapping);
        free(composition->nifs[i].module);
        free(composition->nifs[i].path);
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
    free(composition->program_helper);
    free(composition->nifs);
    free(composition->modules);
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
    if (!valid_utf8(module->name, 255) || !valid_utf8(origin, (size_t)-1))
        return fail(error, BEAM_INVALID_ARGUMENT, "module name and origin must be nonempty UTF-8");
    for (size_t i = 0; i < sizeof(beam_core_module_names) / sizeof(beam_core_module_names[0]); ++i)
        if (!strcmp(module->name, beam_core_module_names[i]))
            return fail(error, BEAM_INVALID_ARGUMENT, "composition cannot replace a core module");
    for (size_t i = 0; i < composition->module_count; ++i)
        if (!strcmp(module->name, composition->modules[i].name))
            return fail(error, BEAM_INVALID_ARGUMENT, "conflicting composition module names");
    if (!beam_component_valid_module(module->name, module->bytes))
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
        (options->native_admission & ~15u) ||
        options->linked_nif_count > 1024 ||
        (options->linked_nif_count && !options->linked_nifs) ||
        options->builtin_nif_count > 1024 || options->builtin_driver_count > 1024 ||
        (options->builtin_nif_count && !options->builtin_nifs) ||
        (options->builtin_driver_count && (!options->builtin_drivers ||
            !(options->native_admission & BEAM_NATIVE_DRIVERS))) ||
        options->component_count > 3 || options->module_count > 4096 || options->nif_count > 1024 ||
        options->activation_count > 4096 || (options->activation_count && !options->activation_modules) ||
        options->preload_count > 4096 || (options->preload_count && !options->preloads) ||
        (options->services & ~(BEAM_SERVICE_OUTPUT | BEAM_SERVICE_CODE | BEAM_SERVICE_ZLIB |
                               BEAM_SERVICE_FILE | BEAM_SERVICE_SOCKET | BEAM_SERVICE_PROGRAM)) ||
        (options->nif_count && !options->nifs) ||
        (options->component_count && !options->components) ||
        (options->module_count && !options->modules))
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid composition specification");
    composition = calloc(1, sizeof(*composition));
    if (!composition) return fail(error, BEAM_OUT_OF_MEMORY, "could not allocate composition");
    composition->references = 1;
    composition->native_admission = options->native_admission;
    for (size_t i = 0; i < options->component_count; ++i) {
        const beam_component_spec *spec = &options->components[i];
        if (!beam_component_kind_valid(spec->kind) ||
            (composition->selected & (1u << spec->kind))) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid or duplicate component");
            goto failed;
        }
        status = beam_component_acquire(spec, &composition->components[composition->count], error);
        if (status != BEAM_OK) goto failed;
        ++composition->count;
        composition->selected |= 1u << spec->kind;
        if (spec->kind == BEAM_COMPONENT_FILE) composition->services |= BEAM_SERVICE_FILE;
        if (spec->kind == BEAM_COMPONENT_NETWORK) composition->services |= BEAM_SERVICE_SOCKET;
        if (spec->kind == BEAM_COMPONENT_ZLIB) composition->services |= BEAM_SERVICE_ZLIB;
    }
    if ((options->services & (BEAM_SERVICE_FILE | BEAM_SERVICE_SOCKET | BEAM_SERVICE_ZLIB)) &
        ~composition->services) {
        status = fail(error, BEAM_INVALID_ARGUMENT, "service selection requires its native component");
        goto failed;
    }
    composition->services |= options->services;
    if (
#ifndef _WIN32
        (composition->services & BEAM_SERVICE_PROGRAM) ||
#endif
        options->program_helper) {
        if (!valid_utf8(options->program_helper, (size_t)-1) || !beam_path_absolute(options->program_helper)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "program service requires an absolute helper path");
            goto failed;
        }
        if (!(composition->services & BEAM_SERVICE_PROGRAM) &&
            !(composition->native_admission & BEAM_NATIVE_PORTS)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "helper requires program or native port admission");
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
    } else if (options->program_helper) {
        status = fail(error, BEAM_INVALID_ARGUMENT, "program helper requires program selection");
        goto failed;
    }
    size_t total = options->module_count + options->activation_count + options->preload_count;
    for (size_t i = 0; i < composition->count; ++i)
        total += beam_component_data(composition->components[i])->module_count;
    composition->modules = calloc(total ? total : 1, sizeof(*composition->modules));
    if (!composition->modules) {
        status = fail(error, BEAM_OUT_OF_MEMORY, "could not allocate composition inventory");
        goto failed;
    }
    for (size_t i = 0; i < composition->count; ++i) {
        const beam_component *component = composition->components[i];
        const beam_component_descriptor *descriptor = beam_component_data(component);
        for (size_t j = 0; j < descriptor->module_count; ++j) {
            const beam_component_module *source = &descriptor->modules[j];
            size_t size = strlen(beam_component_path(component)) + strlen(source->name) + 2;
            char *origin = malloc(size);
            if (!origin) {
                status = fail(error, BEAM_OUT_OF_MEMORY, "could not copy component origin");
                goto failed;
            }
            snprintf(origin, size, "%s#%s", beam_component_path(component), source->name);
            beam_module_spec module = {source->name, {source->bytes, source->size}, origin};
            status = copy_module(composition, &module, error);
            free(origin);
            if (status != BEAM_OK) goto failed;
        }
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
    if (composition->services || options->activation_count) {
        for (size_t i = 0; i < sizeof(beam_activation_support_modules) / sizeof(beam_activation_support_modules[0]); ++i) {
            int found = 0;
            for (size_t j = 0; j < options->module_count; ++j)
                if (!strcmp(options->modules[j].name, beam_activation_support_modules[i])) found = 1;
            if (!found) {
                char message[256];
                snprintf(message, sizeof(message), "required ordinary activation support is missing: %s",
                         beam_activation_support_modules[i]);
                status = fail(error, BEAM_INVALID_ARGUMENT, message);
                goto failed;
            }
        }
    }
    if (!options->defer_modules) composition->staged_count = composition->module_count;
    composition->activation_count = options->activation_count;
    composition->activation_modules = composition->modules + composition->module_count;
    for (size_t i = 0; i < options->activation_count; ++i) {
        status = copy_module(composition, &options->activation_modules[i], error);
        if (status != BEAM_OK) goto failed;
    }
    size_t selected_nifs = options->nif_count + options->linked_nif_count;
    composition->nifs = calloc(selected_nifs ? selected_nifs : 1, sizeof(*composition->nifs));
    if (!composition->nifs) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    for (size_t i = 0; i < options->nif_count; ++i) {
        const beam_nif_spec *spec = &options->nifs[i];
        beam_selected_nif *nif = &composition->nifs[composition->nif_count++];
        int supplied = 0;
        if (spec->struct_size != sizeof(*spec) || !valid_utf8(spec->module, 255) ||
            !valid_utf8(spec->path, (size_t)-1) || !beam_path_absolute(spec->path)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid NIF specification"); goto failed;
        }
        nif->path = beam_runtime_image_location(spec->path);
        if (!nif->path) {
            status = fail(error, errno == ENOMEM ? BEAM_OUT_OF_MEMORY : BEAM_RUNTIME_NOT_FOUND,
                          "NIF image location is unavailable"); goto failed;
        }
        for (size_t j = 0; j < options->module_count; ++j)
            if (!strcmp(spec->module, options->modules[j].name)) supplied = 1;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(spec->module, composition->nifs[j].module) ||
                !strcmp(nif->path, composition->nifs[j].path)) supplied = 0;
        if (!supplied) { status = fail(error, BEAM_INVALID_ARGUMENT, "NIF needs a unique ordinary module and library"); goto failed; }
        nif->module = strdup(spec->module);
        if (!nif->module || !nif->path) { status = BEAM_OUT_OF_MEMORY; goto failed; }
        char inspection_error[256];
        if (!beam_runtime_preflight(nif->path, inspection_error, sizeof(inspection_error))) {
            status = fail(error, BEAM_INVALID_ARGUMENT, inspection_error); goto failed;
        }
        nif->mapping = beam_dlopen(nif->path);
        if (!nif->mapping) { status = fail(error, BEAM_RUNTIME_NOT_FOUND, beam_dlerror()); goto failed; }
        if (!beam_runtime_mapping_matches(nif->mapping)) {
            status = fail(error, BEAM_INVALID_ARGUMENT, BEAM_RUNTIME_IMAGE_ERROR); goto failed;
        }
        for (size_t j = 0; j < i; ++j) {
            if (nif->mapping == composition->nifs[j].mapping) {
                status = fail(error, BEAM_INVALID_ARGUMENT, "native library already selected"); goto failed;
            }
        }
        const char *stamp = beam_dlsym(nif->mapping, "beam_nif_sdk_v1");
        nif->initializer = beam_dlsym(nif->mapping, "nif_init");
        if (!stamp || strcmp(stamp, BEAM_COMPONENT_BUILD_ID) || !nif->initializer) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "NIF SDK provenance or initializer mismatch"); goto failed;
        }
    }
    for (size_t i = 0; i < options->linked_nif_count; ++i) {
        const beam_linked_nif_spec *spec = &options->linked_nifs[i];
        int supplied = 0;
        if (spec->struct_size != sizeof(*spec) || !valid_utf8(spec->module, 255) ||
            !spec->build_id || strcmp(spec->build_id, BEAM_COMPONENT_BUILD_ID) ||
            !spec->initialize) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "invalid linked NIF descriptor or SDK identity");
            goto failed;
        }
        for (size_t j = 0; j < options->module_count; ++j)
            if (!strcmp(spec->module, options->modules[j].name)) supplied = 1;
        for (size_t j = 0; j < composition->nif_count; ++j)
            if (!strcmp(spec->module, composition->nifs[j].module)) supplied = 0;
        if (!supplied) {
            status = fail(error, BEAM_INVALID_ARGUMENT, "linked NIF requires a unique supplied ordinary module");
            goto failed;
        }
        beam_selected_nif *nif = &composition->nifs[composition->nif_count++];
        nif->module = strdup(spec->module);
        if (!nif->module) { status = BEAM_OUT_OF_MEMORY; goto failed; }
        nif->initializer = (void *)spec->initialize;
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
        for (size_t j = 0; j < composition->count; ++j) {
            const beam_component_descriptor *component = beam_component_data(composition->components[j]);
            for (size_t k = 0; k < component->module_count; ++k)
                if (!strcmp(spec->name, component->modules[k].name)) supplied = 0;
        }
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

/* Private serializer for the fixed activation protocol. No VM terms or
 * allocator storage escape preparation/owner-thread execution. */
typedef struct activation_buffer {
    beam_buffer bytes;
    size_t capacity;
} activation_buffer;

static int append(activation_buffer *b, const void *data, size_t size)
{
    if (size > SIZE_MAX - b->bytes.size) return 0;
    size_t needed = b->bytes.size + size;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 128;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        void *grown = realloc(b->bytes.data, capacity);
        if (!grown) return 0;
        b->bytes.data = grown;
        b->capacity = capacity;
    }
    memcpy(b->bytes.data + b->bytes.size, data, size);
    b->bytes.size = needed;
    return 1;
}
static int byte(activation_buffer *b, unsigned value)
{
    unsigned char c = value;
    return append(b, &c, 1);
}
static int length(activation_buffer *b, size_t value)
{
    if (value > UINT32_MAX) return 0;
    unsigned char bytes[] = {value >> 24, value >> 16, value >> 8, value};
    return append(b, bytes, 4);
}
static int atom(activation_buffer *b, const char *name)
{
    size_t n = strlen(name);
    return n <= 65535 && byte(b, 118) && byte(b, n >> 8) && byte(b, n) && append(b, name, n);
}
static int binary(activation_buffer *b, const void *data, size_t size)
{
    return byte(b, 109) && length(b, size) && append(b, data, size);
}
beam_status beam_composition_activation_arguments(const beam_composition *c, beam_buffer *out)
{
    activation_buffer b = {0};
    if (!byte(&b, 131) || !byte(&b, 108) || !length(&b, 3) ||
        !byte(&b, 97) || !byte(&b, c->services) ||
        !byte(&b, 108) || !length(&b, c->activation_count)) goto failed;
    for (size_t i = 0; i < c->activation_count; ++i) {
        const beam_module_spec *m = &c->activation_modules[i];
        if (!byte(&b, 104) || !byte(&b, 3) || !atom(&b, m->name) ||
            !binary(&b, m->bytes.data, m->bytes.size) ||
            !binary(&b, m->origin, strlen(m->origin))) goto failed;
    }
    if (!byte(&b, 106) || !byte(&b, 108) || !length(&b, c->module_count)) goto failed;
    for (size_t i = 0; i < c->module_count; ++i) {
        const beam_module_spec *m = &c->modules[i];
        if (!byte(&b, 104) || !byte(&b, 2) || !atom(&b, m->name) ||
            !binary(&b, m->origin, strlen(m->origin))) goto failed;
    }
    if (!byte(&b, 106) || !byte(&b, 106)) goto failed;
    *out = b.bytes;
    return BEAM_OK;
failed:
    free(b.bytes.data);
    return BEAM_OUT_OF_MEMORY;
}
