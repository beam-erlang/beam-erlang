/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE

#include "beam_cli_recipe.h"
#include "beam/compiler/beam_compiler_files.h"
#include <beam/sdk/inventories.h>
#include "beam_run_inventory.h"
#include "beam_cli_inventory.h"
#include "beam_cli_core_inventory.h"
#include "beam_interactive_inventory.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <pthread.h>
#elif !defined(_WIN32)
#include <sys/syscall.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static beam_status
failure(beam_error *error, beam_status status, int native_code, const char *message)
{
    if (error != NULL) {
        error->native_code = native_code;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return status;
}

static int
is_directory(const char *path)
{
    beam_host_stat st;
    return beam_host_stat_path(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static beam_status open_error(int code);

static beam_status
resolve_root(const char *requested, char *root, beam_error *error)
{
    char *slash;
    size_t required;
    beam_status status;

    if (requested != NULL) {
        beam_host_stat st;
        int code;
        if (requested[0] == '\0')
            return failure(error, BEAM_INVALID_ARGUMENT, 0, "empty runtime root");
        if (beam_host_realpath(requested, root) == NULL || beam_host_stat_path(root, &st) != 0) {
            code = errno;
            return failure(error, open_error(code), code, "runtime root is unavailable");
        }
        if (!S_ISDIR(st.st_mode))
            return failure(error, BEAM_RUNTIME_NOT_FOUND, ENOTDIR, "runtime root is not a directory");
        return BEAM_OK;
    }
    /* The helper may be linked statically into a host outside the prefix.
     * Ask the runtime where its own library was loaded, not where we reside. */
    status = beam_library_path(root, PATH_MAX, &required, error);
    if (status != BEAM_OK)
        return status;
    slash = strrchr(root, '/');
    if (slash == NULL)
        return failure(error, BEAM_RUNTIME_NOT_FOUND, 0, "library directory is unavailable");
    *slash = '\0';
    slash = strrchr(root, '/');
    if (slash == NULL)
        return failure(error, BEAM_RUNTIME_NOT_FOUND, 0, "distribution prefix is unavailable");
    if (slash == root)
        slash[1] = '\0';
    else
        *slash = '\0';
    return BEAM_OK;
}

static beam_status
resolve_path(char *out, const char *root, const char *path, beam_error *error)
{
    int length;
    if (path == NULL || path[0] == '\0')
        return failure(error, BEAM_INVALID_ARGUMENT, 0, "empty distribution path");
    length = beam_path_absolute(path) ? snprintf(out, PATH_MAX, "%s", path)
                           : snprintf(out, PATH_MAX, "%s/%s", root, path);
    if (length < 0 || length >= PATH_MAX)
        return failure(error, BEAM_INVALID_ARGUMENT, 0, "distribution path is too long");
    return BEAM_OK;
}

static beam_status
open_error(int code)
{
    switch (code) {
    case ENOENT: case ENOTDIR: case EACCES: case EPERM:
    case ELOOP: case ENAMETOOLONG:
        return BEAM_RUNTIME_NOT_FOUND;
    case ENOMEM:
        return BEAM_OUT_OF_MEMORY;
    default:
        return BEAM_SYSTEM_ERROR;
    }
}

static beam_status
read_module(const char *path, beam_bytes *module, beam_error *error)
{
    beam_host_stat st;
    unsigned char *data = NULL, trailing;
    size_t size, offset = 0;
    ssize_t count;
    int fd, result, native_code = 0;
    beam_status status = BEAM_SYSTEM_ERROR;

    do {
        fd = beam_host_open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return failure(error, open_error(errno), errno, "startup module is unavailable");
    do {
        result = beam_host_fstat(fd, &st);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        native_code = errno;
        goto done;
    }
    if (!S_ISREG(st.st_mode)) {
        status = BEAM_RUNTIME_NOT_FOUND;
        goto done;
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > SIZE_MAX)
        goto done;
    size = (size_t)st.st_size;
    /* Empty and malformed bytes are still inputs for the VM's native loader. */
    data = malloc(size ? size : 1);
    if (data == NULL) {
        status = BEAM_OUT_OF_MEMORY;
        native_code = ENOMEM;
        goto done;
    }
    while (offset < size) {
        size_t remaining = size - offset;
        if (remaining > SSIZE_MAX)
            remaining = SSIZE_MAX;
        count = beam_host_read(fd, data + offset, remaining);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            native_code = count < 0 ? errno : 0;
            goto done;
        }
        offset += (size_t)count;
    }
    do {
        count = beam_host_read(fd, &trailing, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 0) {
        native_code = count < 0 ? errno : 0;
        goto done;
    }
    status = BEAM_OK;

done:
    /* Linux releases the descriptor even when close reports EINTR. */
    if (beam_host_close(fd) != 0) {
        status = BEAM_SYSTEM_ERROR;
        native_code = errno;
    }
    if (status == BEAM_OK) {
        module->data = data;
        module->size = size;
    } else {
        free(data);
        failure(error, status, native_code, "startup module acquisition failed");
    }
    return status;
}

static beam_status
append_module(beam_return_module **modules, size_t *count, size_t *capacity,
              const char *name, beam_bytes bytes, int service, const char *origin,
              beam_error *error)
{
    beam_return_module *grown;
    char *copied, *copied_origin;
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 8;
        grown = realloc(*modules, next * sizeof(*grown));
        if (!grown)
            return failure(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not store returning module");
        *modules = grown;
        *capacity = next;
    }
    copied = strdup(name);
    if (!copied)
        return failure(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not copy returning module name");
    copied_origin = strdup(origin ? origin : "memory");
    if (!copied_origin) {
        free(copied);
        return failure(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not copy returning module origin");
    }
    (*modules)[*count].name = copied;
    (*modules)[*count].bytes = bytes;
    (*modules)[*count].service = service;
    (*modules)[*count].origin = copied_origin;
    (*count)++;
    return BEAM_OK;
}

static beam_status
append_file(beam_return_module **modules, size_t *count, size_t *capacity,
            const char *path, const char *name, int service, beam_error *error)
{
    beam_bytes bytes = {0};
    beam_status status = read_module(path, &bytes, error);
    if (status != BEAM_OK)
        return status;
    status = append_module(modules, count, capacity, name,
                           (beam_bytes){bytes.data, bytes.size}, service, path, error);
    if (status != BEAM_OK)
        free((void *)bytes.data);
    return status;
}

static void
free_return_modules(beam_return_module *modules, size_t count)
{
    size_t index;
    for (index = 0; index < count; ++index) {
        free((void *)modules[index].name);
        free((void *)modules[index].bytes.data);
        free((void *)modules[index].origin);
    }
    free(modules);
}

static beam_status
append_cli_modules(beam_return_module **modules, size_t *count, size_t *capacity,
                   const char *root, const char *directory, const char *const *names, size_t name_count,
                   beam_error *error)
{
    size_t index;
    char path[PATH_MAX];
    for (index = 0; index < name_count; ++index) {
        beam_status status;
        if (snprintf(path, sizeof(path), "%s/lib/beam/%s/ebin/%s.beam",
                     root, directory, names[index]) >= (int)sizeof(path))
            return failure(error, BEAM_INVALID_ARGUMENT, 0, "CLI module path is too long");
        status = append_file(modules, count, capacity, path, names[index], 0, error);
        if (status != BEAM_OK) {
            if (error)
                snprintf(error->message, sizeof(error->message),
                         "BEAM %s support module %s is unavailable or unreadable", strrchr(directory, '/') ? strrchr(directory, '/') + 1 : directory, names[index]);
            return status;
        }
    }
    return BEAM_OK;
}

/* The host acquires explicit inventory bytes; verification and composition use
 * these same buffers. No directory discovery or second read is involved. */
static beam_status
append_inventory(beam_return_module **modules, size_t *count, size_t *capacity,
                 const char *root, const char *label,
                 const beam_bytecode_inventory *inventory,
                 const char *manifest_path, beam_bytes expected_manifest,
                 beam_error *error)
{
    char path[PATH_MAX];
    beam_bytes manifest = {0};
    beam_module_spec *supplied = NULL;
    size_t start = *count;
    beam_status status = resolve_path(path, root, manifest_path, error);
    if (status != BEAM_OK) return status;
    status = read_module(path, &manifest, error);
    if (status != BEAM_OK) {
        if (error) snprintf(error->message, sizeof(error->message),
                            "BEAM %s inventory manifest is unavailable or unreadable", label);
        return status;
    }
    int matching = manifest.size == expected_manifest.size &&
        !memcmp(manifest.data, expected_manifest.data, manifest.size);
    free((void *)manifest.data);
    if (!matching) {
        if (error) snprintf(error->message, sizeof(error->message),
                            "BEAM %s inventory manifest does not match the selected build", label);
        return BEAM_INVALID_ARGUMENT;
    }
    supplied = calloc(inventory->count, sizeof(*supplied));
    if (!supplied) return failure(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not store bytecode verification inputs");
    for (size_t i = 0; i < inventory->count; ++i) {
        const beam_bytecode_record *record = &inventory->records[i];
        status = resolve_path(path, root, record->relative_path, error);
        if (status == BEAM_OK)
            status = append_file(modules, count, capacity, path, record->name, 0, error);
        if (status != BEAM_OK) {
            if (error) snprintf(error->message, sizeof(error->message),
                                "BEAM %s module %s is unavailable or unreadable", label, record->name);
            goto done;
        }
        const beam_return_module *module = &(*modules)[start + i];
        supplied[i] = (beam_module_spec){module->name, module->bytes, module->origin};
    }
    status = beam_bytecode_verify(inventory, supplied, inventory->count, error);
done:
    free(supplied);
    return status;
}

beam_status
beam_cli_recipe_return_prepare(const beam_cli_recipe_return_options *options,
                                beam_return_boot **out, beam_error *error)
{
    char root[PATH_MAX], path[PATH_MAX], bindir[PATH_MAX];
    beam_return_module *modules = NULL;
    beam_return_options prepared = {0};
    beam_status status;
    size_t count = 0, capacity = 0, index;
    beam_component_spec components[3] = {0};
    char component_paths[3][PATH_MAX];
    size_t component_count = 0;
    const char *component_names[] = {"file", "network", "zlib"};
    const unsigned component_services[] = {BEAM_SERVICE_FILE, BEAM_SERVICE_SOCKET, BEAM_SERVICE_ZLIB};
    const beam_component_kind component_kinds[] = {
        BEAM_COMPONENT_FILE, BEAM_COMPONENT_NETWORK, BEAM_COMPONENT_ZLIB
    };

    if (error != NULL)
        *error = (beam_error){0};
#ifdef _WIN32
    if (beam_windows_main_thread() != 1)
#elif defined(__APPLE__)
    if (!pthread_main_np())
#else
    if ((pid_t)syscall(SYS_gettid) != getpid())
#endif
        return failure(error, BEAM_WRONG_THREAD, 0, "distribution preparation requires the main thread");
    if (options == NULL || out == NULL || *out != NULL ||
        options->struct_size != sizeof(*options) ||
        !options->entry_module || !options->entry_function ||
        (options->module_count && !options->modules))
        return failure(error, BEAM_INVALID_ARGUMENT, 0, "invalid returning distribution options");
    status = resolve_root(options->runtime_root, root, error);
    if (status != BEAM_OK)
        return status;
    if (snprintf(bindir, sizeof(bindir), "%s/libexec/beam", root) >= (int)sizeof(bindir))
        return failure(error, BEAM_INVALID_ARGUMENT, 0, "distribution path is too long");
#ifndef _WIN32
    if (options->services & BEAM_SERVICE_PROGRAM) {
        if (snprintf(path, sizeof(path), "%s/erl_child_setup", bindir) >= (int)sizeof(path))
            return failure(error, BEAM_INVALID_ARGUMENT, 0, "program helper path is too long");
        if (access(path, X_OK) != 0)
            return failure(error, BEAM_RUNTIME_NOT_FOUND, errno, "program helper is unavailable");
    }
#endif
    const beam_sdk_inventory_kind selections[] = {
        BEAM_SDK_INVENTORY_BASE, BEAM_SDK_INVENTORY_BOOT, BEAM_SDK_INVENTORY_SERVICES};
    const char *labels[] = {"base", "boot", "services"};
    for (size_t i = 0; i < sizeof(selections) / sizeof(selections[0]); ++i) {
        const beam_sdk_inventory *selected = beam_sdk_inventory_get(selections[i]);
        status = append_inventory(&modules, &count, &capacity, root, labels[i], selected->bytecode,
                                  selected->manifest_path, selected->manifest, error);
        if (status != BEAM_OK) break;
    }
    if (status != BEAM_OK) goto done;
    for (index = 0; index < sizeof(component_names) / sizeof(component_names[0]); ++index) {
        if (component_services[index] && !(options->services & component_services[index]))
            continue;
        if (snprintf(component_paths[component_count], PATH_MAX,
                     "%s/" BEAM_CLI_COMPONENT_DIRECTORY "/" BEAM_CLI_LIBRARY_PREFIX "beam-%s" BEAM_CLI_LIBRARY_SUFFIX, root,
                     component_names[index]) >= PATH_MAX) {
            status = failure(error, BEAM_INVALID_ARGUMENT, 0, "component path is too long");
            goto done;
        }
        components[component_count] = (beam_component_spec){
            .struct_size = sizeof(beam_component_spec),
            .kind = component_kinds[index],
            .path = component_paths[component_count]
        };
        ++component_count;
    }
    if (options->profile == BEAM_CLI_RUN || options->profile == BEAM_CLI_INTERACTIVE || options->profile == BEAM_CLI_APPLICATION) {
        const int interactive = options->profile == BEAM_CLI_INTERACTIVE;
        status = append_cli_modules(&modules, &count, &capacity, root, "runtime/beam/cli",
                                    beam_cli_modules, BEAM_CLI_MODULE_COUNT, error);
        if (status != BEAM_OK)
            goto done;
        /* Resolve retained helpers against selected component ownership before
         * preparation. Formatting alone must never activate networking. */
        if (!(options->services & BEAM_SERVICE_SOCKET)) {
            status = append_cli_modules(&modules, &count, &capacity, root, "runtime/beam/cli",
                                        beam_cli_fallback_modules,
                                        BEAM_CLI_FALLBACK_MODULE_COUNT, error);
            if (status != BEAM_OK)
                goto done;
        }
        if (options->profile == BEAM_CLI_APPLICATION) {
            static const char *const names[] = {"beam_application_run"};
            status = append_cli_modules(&modules, &count, &capacity, root, "runtime/beam/application", names, 1, error);
        } else status = append_cli_modules(&modules, &count, &capacity, root,
                                    interactive ? "runtime/beam/interactive" : "runtime/beam/run",
                                    interactive ? beam_interactive_modules : beam_run_modules,
                                    interactive ? BEAM_INTERACTIVE_MODULE_COUNT : BEAM_RUN_MODULE_COUNT,
                                    error);
        if (status != BEAM_OK)
            goto done;
    }
    for (size_t i = 0; i < options->required_count; ++i) {
        int found = 0;
        if (!options->required_modules || !options->required_modules[i]) { status = BEAM_INVALID_ARGUMENT; goto done; }
        for (size_t j = 0; j < count; ++j) if (!strcmp(options->required_modules[i], modules[j].name)) found = 1;
        for (size_t j = 0; j < sizeof(beam_cli_core_modules)/sizeof(*beam_cli_core_modules); ++j)
            if (!strcmp(options->required_modules[i], beam_cli_core_modules[j])) found = 1;
        if (!found) { status = failure(error, BEAM_INVALID_ARGUMENT, 0, "required host module is absent"); goto done; }
    }
    for (index = 0; index < options->module_count; ++index) {
        const beam_return_module *module = &options->modules[index];
        unsigned char *copied;
        if (!module->name || (module->bytes.size && !module->bytes.data)) {
            status = failure(error, BEAM_INVALID_ARGUMENT, 0, "returning module is invalid");
            goto done;
        }
        copied = NULL;
        if (module->bytes.size) {
            copied = malloc(module->bytes.size);
            if (!copied) {
                status = failure(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not copy returning module");
                goto done;
            }
            memcpy(copied, module->bytes.data, module->bytes.size);
        }
        status = append_module(&modules, &count, &capacity, module->name,
                               (beam_bytes){copied, module->bytes.size}, module->service,
                               module->origin, error);
        if (status != BEAM_OK) {
            free(copied);
            goto done;
        }
    }
    prepared.struct_size = sizeof(prepared);
    prepared.services = options->services;
    prepared.entry_module = options->entry_module;
    prepared.entry_function = options->entry_function;
    prepared.arguments = options->arguments;
    prepared.module_count = count;
    prepared.modules = modules;
    prepared.component_count = component_count;
    prepared.components = components;
    prepared.bindir = bindir;
    prepared.output = options->output;
    prepared.output_context = options->output_context;
    prepared.code = options->code;
    prepared.code_context = options->code_context;
    prepared.input = options->input;
    prepared.input_context = options->input_context;
    prepared.vm = options->vm;
    prepared.nif_count = options->nif_count;
    prepared.nifs = options->nifs;
    status = beam_return_prepare(&prepared, out, error);
done:
    free_return_modules(modules, count);
    return status;
}
