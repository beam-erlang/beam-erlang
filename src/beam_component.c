/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_component_internal.h"
#include "beam_runtime_image.h"
#include "beam_component_build.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct beam_component {
    unsigned references;
    void *handle;
    char *path;
    const beam_component_descriptor *descriptor;
};

static beam_component *active_components[3];
static size_t active_count;

const char *beam_build_id(void)
{
    return BEAM_COMPONENT_BUILD_ID;
}
static const char *const zlib_names[] = {"zlib"};
static const char *const file_names[] = {"beam_file", "prim_buffer", "prim_file"};
static const char *const network_names[] = {
    "beam_socket", "socket_registry", "socket", "maps", "lists", "prim_socket"
};

static beam_status fail(beam_error *error, beam_status status, const char *message)
{
    if (error)
        snprintf(error->message, sizeof(error->message), "%s", message);
    return status;
}

static uint32_t read_u32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int beam_component_valid_module(const char *name, beam_bytes bytes)
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

static const char *native_symbol(const char *module)
{
    if (!strcmp(module, "prim_buffer")) return "beam_prim_buffer_nif_init";
    if (!strcmp(module, "prim_file")) return "beam_prim_file_nif_init";
    if (!strcmp(module, "prim_socket")) return "beam_prim_socket_nif_init";
    if (!strcmp(module, "zlib")) return "beam_zlib_nif_init";
    return NULL;
}

beam_status beam_component_acquire(const beam_component_spec *spec,
                                   beam_component **out, beam_error *error)
{
    beam_component *component;
    const beam_component_descriptor *descriptor;
    const char *const *names;
    size_t count, i;
    if (!spec || spec->struct_size != sizeof(*spec) || !out || *out ||
        !spec->path || !*spec->path)
        return fail(error, BEAM_INVALID_ARGUMENT, "invalid component specification");
    switch (spec->kind) {
    case BEAM_COMPONENT_FILE: names = file_names; count = 3; break;
    case BEAM_COMPONENT_NETWORK: names = network_names; count = 6; break;
    case BEAM_COMPONENT_ZLIB: names = zlib_names; count = 1; break;
    default: return fail(error, BEAM_INVALID_ARGUMENT, "unknown runtime component");
    }
    if (beam_runtime_is_static())
        return fail(error, BEAM_UNSUPPORTED, "modern components require a shared runtime host");
    component = calloc(1, sizeof(*component));
    if (!component)
        return fail(error, BEAM_OUT_OF_MEMORY, "could not allocate runtime component");
    component->references = 1;
    component->path = realpath(spec->path, NULL);
    if (!component->path) {
        beam_component_release(component);
        return fail(error, BEAM_RUNTIME_NOT_FOUND, "runtime component path is unavailable");
    }
    component->handle = dlopen(component->path, RTLD_NOW | RTLD_LOCAL);
    if (!component->handle) {
        beam_component_release(component);
        return fail(error, BEAM_RUNTIME_NOT_FOUND, "could not map runtime component");
    }
    descriptor = dlsym(component->handle, "beam_component_descriptor_v1");
    if (!descriptor || descriptor->abi_version != BEAM_COMPONENT_ABI_VERSION ||
        !descriptor->build_id || strcmp(descriptor->build_id, BEAM_COMPONENT_BUILD_ID) ||
        descriptor->kind != spec->kind || descriptor->module_count != count ||
        !descriptor->modules || descriptor->base_version != BEAM_COMPONENT_BASE_VERSION ||
        descriptor->required_components != 0u)
        goto invalid_descriptor;
    for (i = 0; i < count; ++i) {
        const beam_component_module *module = &descriptor->modules[i];
        const char *symbol = native_symbol(names[i]);
        if (!module->name || strcmp(module->name, names[i]) ||
            (!!symbol != !!module->nif_init_symbol) ||
            (symbol && (strcmp(symbol, module->nif_init_symbol) ||
                        !dlsym(component->handle, symbol))))
            goto invalid_descriptor;
        if (!beam_component_valid_module(module->name,
                                          (beam_bytes){module->bytes, module->size})) {
            beam_component_release(component);
            return fail(error, BEAM_INVALID_BEAM, "invalid runtime component module bytes");
        }
    }
    component->descriptor = descriptor;
    *out = component;
    return BEAM_OK;
invalid_descriptor:
    beam_component_release(component);
    return fail(error, BEAM_INVALID_ARGUMENT, "runtime component does not match this build and recipe");
}

void beam_component_retain(beam_component *component)
{
    __atomic_add_fetch(&component->references, 1, __ATOMIC_RELAXED);
}

void beam_component_release(beam_component *component)
{
    if (component && !__atomic_sub_fetch(&component->references, 1, __ATOMIC_ACQ_REL)) {
        if (component->handle)
            dlclose(component->handle);
        free(component->path);
        free(component);
    }
}

const beam_component_descriptor *beam_component_data(const beam_component *component)
{
    return component->descriptor;
}

const char *beam_component_path(const beam_component *component)
{
    return component->path;
}

void beam_components_activate(beam_component *const *components, size_t count)
{
    size_t i;
    active_count = count;
    for (i = 0; i < count; ++i)
        active_components[i] = components[i];
}

void beam_components_deactivate(void)
{
    memset(active_components, 0, sizeof(active_components));
    active_count = 0;
}

beam_component *beam_component_bind_nif(const char *module, void **initializer)
{
    size_t i, j;
    for (i = 0; i < active_count; ++i) {
        beam_component *component = active_components[i];
        const beam_component_descriptor *descriptor = component->descriptor;
        for (j = 0; j < descriptor->module_count; ++j) {
            const beam_component_module *candidate = &descriptor->modules[j];
            if (candidate->nif_init_symbol && !strcmp(candidate->name, module)) {
                *initializer = dlsym(component->handle, candidate->nif_init_symbol);
                if (*initializer) {
                    beam_component_retain(component);
                    return component;
                }
            }
        }
    }
    return NULL;
}
