/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "beam_runtime_image.h"
#include "beam_platform.h"
#include "../include/beam/native_image.h"
#include <stdint.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <psapi.h>
#endif

struct beam_native_image {
    unsigned references;
    void *handle;
    char *path;
};

int beam_runtime_static(void) { return beam_runtime_is_static(); }

static beam_status image_error(beam_error *error, beam_status status, const char *message)
{
    if (error) snprintf(error->message, sizeof(error->message), "%s", message);
    return status;
}

beam_status beam_native_image_open(const char *location, beam_native_image **out,
                                   beam_error *error)
{
    if (error) memset(error, 0, sizeof(*error));
    if (!out || *out || !location || !beam_path_absolute(location))
        return image_error(error, BEAM_INVALID_ARGUMENT, "native image requires an absolute location");
    beam_native_image *image = calloc(1, sizeof(*image));
    if (!image) return BEAM_OUT_OF_MEMORY;
    image->references = 1;
    image->path = beam_runtime_image_location(location);
    if (!image->path) {
        int code = errno;
        beam_native_image_release(&image);
        if (error) error->native_code = code;
        return image_error(error, code == ENOMEM ? BEAM_OUT_OF_MEMORY : BEAM_RUNTIME_NOT_FOUND,
                           "native image location is unavailable");
    }
    char message[256];
    if (!beam_runtime_preflight(image->path, message, sizeof(message))) {
        beam_native_image_release(&image);
        return image_error(error, BEAM_INVALID_ARGUMENT, message);
    }
    image->handle = beam_dlopen(image->path);
    if (!image->handle) {
        image_error(error, BEAM_RUNTIME_NOT_FOUND, beam_dlerror());
        beam_native_image_release(&image);
        return BEAM_RUNTIME_NOT_FOUND;
    }
    if (!beam_runtime_mapping_matches(image->handle)) {
        beam_native_image_release(&image);
        return image_error(error, BEAM_INVALID_ARGUMENT, BEAM_RUNTIME_IMAGE_ERROR);
    }
    *out = image;
    return BEAM_OK;
}

void beam_native_image_retain(beam_native_image *image)
{
    if (image) beam_reference_add(&image->references, 1);
}

void beam_native_image_release(beam_native_image **out)
{
    if (!out || !*out) return;
    beam_native_image *image = *out;
    *out = NULL;
    if (beam_reference_add(&image->references, -1)) return;
    if (image->handle) beam_dlclose(image->handle);
    free(image->path);
    free(image);
}

const char *beam_native_image_path(const beam_native_image *image)
{
    return image ? image->path : NULL;
}

beam_status beam_native_image_symbol(const beam_native_image *image, const char *name, void **out)
{
    if (!image || !name || !*name || !out) return BEAM_INVALID_ARGUMENT;
    void *symbol = beam_dlsym(image->handle, name);
    if (!symbol) return BEAM_NOT_FOUND;
    *out = symbol;
    return BEAM_OK;
}

static void image_owner_retain(void *image) { beam_native_image_retain(image); }
static void image_owner_release(void *context)
{
    beam_native_image *image = context;
    beam_native_image_release(&image);
}

beam_status beam_native_image_owner(beam_native_image *image, beam_native_owner *out)
{
    if (!image || !out || out->struct_size != sizeof(*out)) return BEAM_INVALID_ARGUMENT;
    *out = (beam_native_owner){sizeof(*out), image, image_owner_retain, image_owner_release};
    return BEAM_OK;
}

int beam_runtime_is_static(void)
{
#ifdef BEAM_STATIC_RUNTIME
    return 1;
#else
    return 0;
#endif
}

int beam_runtime_mapping_matches(void *handle)
{
#ifdef _WIN32
    /* GetProcAddress does not search imported libraries. Inspect the process
     * images so a dependency cannot introduce a second VM into a static host. */
    HMODULE owner, *images = NULL;
    DWORD bytes = 0;
    (void)handle;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&beam_runtime_is_static, &owner)) return 0;
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        DWORD capacity = bytes + 32 * sizeof(*images);
        if (capacity > 8192 * sizeof(*images)) return 0;
        images = malloc(capacity);
        if (!images) return 0;
        if (!EnumProcessModules(GetCurrentProcess(), images, capacity, &bytes)) {
            free(images); return 0;
        }
        if (bytes > capacity) { free(images); continue; }
        int matches = 1;
        for (DWORD i = 0; i < bytes / sizeof(*images); ++i)
            if (images[i] != owner && GetProcAddress(images[i], "beam_build_id")) matches = 0;
        free(images);
        return matches;
    }
    return 0;
#else
    Dl_info current, selected;
    void *symbol;
    dlerror();
    symbol = dlsym(handle, "beam_build_id");
    /* Runtime-neutral extensions import native APIs from the calling host. */
    if (!symbol) return 1;
    return dladdr((void *)&beam_runtime_is_static, &current) &&
        dladdr(symbol, &selected) && current.dli_fbase == selected.dli_fbase;
#endif
}

char *beam_runtime_image_location(const char *location)
{
    char *path;
    if (!location || !*location) { errno = EINVAL; return NULL; }
    /* A real filename containing '!' keeps its ordinary filesystem meaning. */
    path = beam_realpath(location);
    if (path || errno == ENOMEM) return path;
#if defined(__ANDROID__) || defined(BEAM_TEST_ANDROID_IMAGE)
    const char *separator = NULL, *search = location;
    while ((search = strstr(search, "!/")) != NULL) {
        separator = search; search += 2;
    }
    if (location[0] == '/' && separator && separator != location) {
        const char *member = separator + 2, *segment = member;
        if (!*member || strstr(member, "!/") || strchr(member, '\\')) {
            errno = EINVAL; return NULL;
        }
        for (const char *at = member;; ++at) {
            if (*at == '/' || !*at) {
                size_t length = (size_t)(at - segment);
                if (!length || (length == 1 && segment[0] == '.') ||
                    (length == 2 && segment[0] == '.' && segment[1] == '.')) {
                    errno = EINVAL; return NULL;
                }
                if (!*at) break;
                segment = at + 1;
            }
        }
        char *container = strndup(location, (size_t)(separator - location));
        if (!container) return NULL;
        char *canonical = beam_realpath(container);
        free(container);
        if (!canonical) return NULL;
        struct stat metadata;
        if (stat(canonical, &metadata) || !S_ISREG(metadata.st_mode)) {
            free(canonical); errno = EINVAL; return NULL;
        }
        size_t base = strlen(canonical), tail = strlen(member);
        if (base > SIZE_MAX - tail - 3) {
            free(canonical); errno = ENAMETOOLONG; return NULL;
        }
        path = malloc(base + tail + 3);
        if (path) {
            memcpy(path, canonical, base);
            memcpy(path + base, "!/", 2);
            memcpy(path + base + 2, member, tail + 1);
        }
        free(canonical);
    }
#endif
    return path;
}
