/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/library.h>
#include <beam/native_image.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct beam_library {
    atomic_uint references;
    beam_native_image *image;
    size_t module_count;
    beam_module_spec *modules;
    size_t nif_count;
    beam_builtin_nif *nifs;
};

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static beam_status invalid(beam_error *error, const char *message)
{
    if (error) snprintf(error->message, sizeof(error->message), "%s", message);
    return BEAM_INVALID_ARGUMENT;
}

void beam_library_retain(beam_library *library)
{
    if (library) atomic_fetch_add_explicit(&library->references, 1, memory_order_relaxed);
}

void beam_library_release(beam_library **out)
{
    if (!out || !*out) return;
    beam_library *library = *out;
    *out = NULL;
    if (atomic_fetch_sub_explicit(&library->references, 1, memory_order_acq_rel) != 1) return;
    for (size_t i = 0; i < library->module_count; ++i) {
        free((void *)library->modules[i].name);
        free((void *)library->modules[i].origin);
    }
    for (size_t i = 0; i < library->nif_count; ++i) {
        free((void *)library->nifs[i].name);
        free((void *)library->nifs[i].build_id);
    }
    free(library->modules);
    free(library->nifs);
    beam_native_image_release(&library->image);
    free(library);
}

static beam_status prepare(const beam_library_descriptor *descriptor, beam_native_image *image,
                           beam_library **out, beam_error *error)
{
    if (!descriptor || descriptor->struct_size != sizeof(*descriptor) ||
        descriptor->format_version != BEAM_LIBRARY_FORMAT_VERSION ||
        !descriptor->build_id || strcmp(descriptor->build_id, beam_build_id()) ||
        !descriptor->name || !*descriptor->name || descriptor->module_count > 4096 ||
        descriptor->nif_count > 1024 ||
        (descriptor->module_count && !descriptor->modules) ||
        (descriptor->nif_count && !descriptor->nifs))
        return invalid(error, "invalid library descriptor or SDK identity");
    beam_library *library = calloc(1, sizeof(*library));
    if (!library) return BEAM_OUT_OF_MEMORY;
    atomic_init(&library->references, 1);
    library->image = image;
    beam_native_image_retain(image);
    library->modules = calloc(descriptor->module_count ? descriptor->module_count : 1, sizeof(*library->modules));
    library->nifs = calloc(descriptor->nif_count ? descriptor->nif_count : 1, sizeof(*library->nifs));
    beam_status status = BEAM_OUT_OF_MEMORY;
    if (!library->modules || !library->nifs) goto failed;
    const char *origin = image ? beam_native_image_path(image) : descriptor->name;
    for (size_t i = 0; i < descriptor->module_count; ++i) {
        const beam_module_spec *source = &descriptor->modules[i];
        if (!source->name || !*source->name || !source->bytes.data || !source->bytes.size) {
            status = invalid(error, "invalid library module"); goto failed;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(source->name, library->modules[j].name)) {
                status = invalid(error, "duplicate library module"); goto failed;
            }
        beam_module_spec *copy = &library->modules[library->module_count++];
        copy->name = copy_text(source->name);
        copy->bytes = source->bytes;
        if (source->origin) {
            copy->origin = copy_text(source->origin);
        } else {
            size_t size = strlen(origin) + strlen(source->name) + 2;
            char *label = malloc(size);
            copy->origin = label;
            if (label) snprintf(label, size, "%s#%s", origin, source->name);
        }
        if (!copy->name || !copy->origin) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    }
    for (size_t i = 0; i < descriptor->nif_count; ++i) {
        const beam_builtin_nif *source = &descriptor->nifs[i];
        if (source->struct_size != sizeof(*source) || !source->name || !*source->name ||
            !source->build_id || strcmp(source->build_id, descriptor->build_id) || !source->initialize ||
            (source->taint != 0 && source->taint != 1)) {
            status = invalid(error, "invalid library native registration"); goto failed;
        }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(source->name, library->nifs[j].name)) {
                status = invalid(error, "duplicate library native registration"); goto failed;
            }
        beam_builtin_nif *copy = &library->nifs[library->nif_count++];
        *copy = *source;
        copy->name = copy_text(source->name);
        copy->build_id = copy_text(source->build_id);
        if (!copy->name || !copy->build_id) { status = BEAM_OUT_OF_MEMORY; goto failed; }
    }
    *out = library;
    return BEAM_OK;
failed:
    beam_library_release(&library);
    return status;
}

beam_status beam_library_open(const char *path, beam_library **out, beam_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!out || *out) return invalid(error, "invalid library output");
    beam_native_image *image = NULL;
    beam_status status = beam_native_image_open(path, &image, error);
    if (status != BEAM_OK) return status;
    void *descriptor = NULL;
    status = beam_native_image_symbol(image, "beam_library_descriptor_v1", &descriptor);
    if (status == BEAM_OK) status = prepare(descriptor, image, out, error);
    else status = invalid(error, "native image has no BEAM library descriptor");
    beam_native_image_release(&image);
    return status;
}

beam_status beam_library_from_static(const beam_library_descriptor *descriptor,
                                     beam_library **out, beam_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!out || *out) return invalid(error, "invalid library output");
    return prepare(descriptor, NULL, out, error);
}

static void owner_retain(void *library) { beam_library_retain(library); }
static void owner_release(void *context)
{
    beam_library *library = context;
    beam_library_release(&library);
}

beam_status beam_library_get_inputs(beam_library *library, beam_library_inputs *out)
{
    if (!library || !out || out->struct_size != sizeof(*out)) return BEAM_INVALID_ARGUMENT;
    *out = (beam_library_inputs){sizeof(*out), library->module_count, library->modules,
        library->nif_count, library->nifs,
        {sizeof(beam_native_owner), library, owner_retain, owner_release}};
    return BEAM_OK;
}
