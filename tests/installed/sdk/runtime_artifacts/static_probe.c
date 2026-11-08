/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/native_image.h>
#include <beam/sdk/inventories.h>
#include <erl_driver.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#ifdef BEAM_TEST_DRIVER_PROBE
#include "builtin_bindings.h"
#include "driver_bytecode.h"
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include "host_observe.h"
#endif
static const char *base_names[] = {
    "beam", "beam_format", "beam_time", "beam_atomics", "beam_counters", "beam_persistent_term", "beam_binary", "beam_lists", "beam_maps", "beam_records", "beam_ets", "beam_string", "beam_unicode", "beam_runtime_unicode_util", "beam_math", "beam_ddll", "beam_debugger"
};
#ifdef BEAM_TEST_DRIVER_PROBE
static int driver_check(const char *driver_path)
{
    const char *end = strrchr(driver_path, '/');
    if (!end) return 15;
    size_t length = (size_t)(end - driver_path);
    if (length > UINT32_MAX) return 15;
    unsigned char *arguments = malloc(length + 12);
    if (!arguments) return 15;
    const unsigned char head[] = {131,108,0,0,0,1,109};
    memcpy(arguments, head, sizeof head);
    arguments[7] = (unsigned char)(length >> 24);
    arguments[8] = (unsigned char)(length >> 16);
    arguments[9] = (unsigned char)(length >> 8);
    arguments[10] = (unsigned char)length;
    memcpy(arguments + 11, driver_path, length);
    arguments[length + 11] = 106;
    beam_composition_options spec = {.struct_size = sizeof spec,
        .modules = driver_modules,
        .module_count = sizeof driver_modules / sizeof *driver_modules,
        .builtin_bindings = beam_test_modern_bindings,
        .builtin_binding_count = BEAM_TEST_MODERN_BINDING_COUNT,
        .native_admission = BEAM_NATIVE_DRIVERS | BEAM_NATIVE_PORTS};
    beam_error error = {0};
    beam_composition *composition = NULL;
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    if (beam_composition_prepare(&spec, &composition, &error) != BEAM_OK ||
        beam_init(&system, &error) != BEAM_OK) goto failed;
    beam_entry entry = {"driver_probe", "start", {arguments, length + 12}};
    beam_vm_options options;
    beam_vm_options_init(&options);
    options.execution = BEAM_PROCESS_OWNED;
    options.initial_root = &entry;
    options.composition = composition;
    if (beam_vm_init(system, &options, &vm, &error) != BEAM_OK) goto failed;
    free(arguments);
    arguments = NULL;
    beam_composition_release(&composition);
    beam_vm_enter(vm, &error);
failed:
    fprintf(stderr, "driver execution failed: %s\n", error.message);
    free(arguments);
    beam_vm_terminate(&vm, NULL);
    beam_composition_release(&composition);
    beam_terminate(&system);
    return 15;
}
#endif
int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) return 7;
#ifndef BEAM_TEST_DRIVER_PROBE
    if (argc == 3) return 7;
#endif
    const beam_sdk_inventory *base = beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE);
    if (!base || base->bytecode->count != sizeof base_names / sizeof base_names[0] ||
        strcmp(base->bytecode->build_id, beam_build_id())) return 14;
    for (size_t i = 0; i < base->bytecode->count; ++i)
        if (strcmp(base->bytecode->records[i].name, base_names[i])) return 14;
    const beam_sdk_inventory *stdlib = beam_sdk_inventory_get(BEAM_SDK_INVENTORY_STDLIB);
    static const char *stdlib_names[] = {"array", "dict", "graph", "gb_sets", "gb_trees",
        "orddict", "ordsets", "proplists", "queue", "sets", "sofs", "digraph", "digraph_utils"};
    if (!stdlib || stdlib->bytecode->count != sizeof stdlib_names / sizeof stdlib_names[0] ||
        strcmp(stdlib->bytecode->build_id, beam_build_id()) ||
        strcmp(stdlib->manifest_path, "lib/beam/stdlib/modules.txt")) return 14;
    for (size_t i = 0; i < stdlib->bytecode->count; ++i)
        if (strcmp(stdlib->bytecode->records[i].name, stdlib_names[i])) return 14;
    char path[PATH_MAX], executable[PATH_MAX];
    size_t required = 0;
    beam_error error = {0};
#ifdef __APPLE__
    uint32_t capacity=sizeof(executable);
    char raw[PATH_MAX];
    if (_NSGetExecutablePath(raw,&capacity) || !realpath(raw,executable)) return 1;
#else
    ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (size < 0) return 1;
    executable[size] = 0;
#endif
    if (chdir("/")) return 6;
    if (beam_library_path(path, sizeof(path), &required, &error) != BEAM_OK ||
        strcmp(path, executable) || required != strlen(path) + 1) return 2;
    if (!beam_runtime_static()) return 3;
    void *native = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!native) { fprintf(stderr, "%s\n", dlerror()); return 8; }
    ErlDrvTermData (*nil)(void) = dlsym(native, "sdk_probe_nil");
    if (!nil || nil() != driver_mk_term_nil()) return 9;
    if (dlclose(native)) return 10;
    /* Archive retention must not capture the host's system-library calls. */
#ifdef __APPLE__
    void *compression = dlopen("/usr/lib/libz.1.dylib", RTLD_NOW | RTLD_LOCAL);
#else
    void *compression = dlopen("libz.so.1", RTLD_NOW | RTLD_LOCAL);
#endif
    void *patterns = dlopen(SYSTEM_PCRE_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (!compression || !patterns ||
        dlsym(compression, "inflate") != (void *)inflate ||
        dlsym(patterns, "pcre2_config_8") != (void *)pcre2_config) {
        fprintf(stderr,"system-library binding mismatch: zlib=%p pcre=%p error=%s\n",compression,patterns,dlerror());
        return 11;
    }
    unsigned char compressed[128], plain[128];
    const unsigned char original[] = "host compression remains independent";
    uLongf compressed_size = sizeof(compressed), plain_size = sizeof(plain);
    if (compress(compressed, &compressed_size, original, sizeof(original)) != Z_OK ||
        uncompress(plain, &plain_size, compressed, compressed_size) != Z_OK ||
        plain_size != sizeof(original) || memcmp(original, plain, sizeof(original))) return 12;
    int pcre_unicode = 0;
    if (pcre2_config(PCRE2_CONFIG_UNICODE, &pcre_unicode) || !pcre_unicode) return 13;
    dlclose(patterns); dlclose(compression);
#ifdef __APPLE__
    if (beam_test_host_runtime_images() != 0) return 5;
#else
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 4;
    while (fgets(path, sizeof(path), maps))
        if (strstr(path, "libbeam")) { fclose(maps); return 5; }
    fclose(maps);
#endif
    puts(beam_build_id());
#ifdef BEAM_TEST_DRIVER_PROBE
    if (argc == 3) {
        if (strcmp(argv[2], "driver")) return 7;
        fflush(stdout);
        return driver_check(argv[1]);
    }
#endif
    return 0;
}
