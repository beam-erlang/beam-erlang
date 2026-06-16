/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "beam_runtime_image.h"
#include "beam_platform.h"
#include <stdint.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <psapi.h>
#endif

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
