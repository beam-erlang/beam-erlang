/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <beam/sdk/inventories.h>
#include <erl_driver.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include "host_observe.h"
#endif
int main(int argc, char **argv)
{
    if (argc != 2) return 7;
    const beam_sdk_inventory *base = beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE);
    if (!base || base->bytecode->count != 7 ||
        strcmp(base->bytecode->build_id, beam_build_id())) return 14;
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
    beam_component_spec component = {sizeof(component), BEAM_COMPONENT_FILE, "/absent-component.so"};
    beam_composition_options options = {.struct_size = sizeof(options),
        .component_count = 1, .components = &component};
    beam_composition *composition = NULL;
    if (beam_composition_prepare(&options, &composition, &error) != BEAM_UNSUPPORTED || composition)
        return 3;
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
    return 0;
}
