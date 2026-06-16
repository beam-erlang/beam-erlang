/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static beam_error error;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "preloads line %d: %s: %s\n", \
    __LINE__, #x, error.message); exit(1); } } while (0)

static beam_buffer read_bytes(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    CHECK(file && !fseek(file, 0, SEEK_END) && (size = ftell(file)) > 0 &&
          !fseek(file, 0, SEEK_SET));
    beam_buffer bytes = {malloc((size_t)size), (size_t)size};
    CHECK(bytes.data && fread(bytes.data, 1, bytes.size, file) == bytes.size);
    CHECK(!fclose(file));
    return bytes;
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    beam_buffer bytes = read_bytes(argv[1]), on_load = read_bytes(argv[2]);
    beam_module_spec probe = {"preload_probe", {bytes.data, bytes.size}, "copied:probe"};
    beam_module_spec hook = {"preload_on_load", {on_load.data, on_load.size}, NULL};
    beam_composition_options spec = {.struct_size = sizeof(spec), .preload_count = 1};
    beam_composition *composition = NULL;
    beam_vm *vm = NULL;
    beam_system *system = NULL;
    beam_vm_options options;
    beam_vm_options_init(&options);
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_INVALID_ARGUMENT);
    spec.preloads = &probe;
    spec.modules = &probe; spec.module_count = 1;
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_INVALID_ARGUMENT);
    spec.modules = NULL; spec.module_count = 0;
    beam_module_spec duplicate[] = {probe, probe};
    spec.preloads = duplicate; spec.preload_count = 2;
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_INVALID_ARGUMENT);
    spec.preload_count = 1;
    duplicate[0].name = "erlang";
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_INVALID_ARGUMENT);
    duplicate[0] = probe; duplicate[0].bytes.size = 4;
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_INVALID_BEAM);
    CHECK(!composition && beam_init(&system, &error) == BEAM_OK);
    spec.preloads = &hook;
    CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_OK);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_UNSUPPORTED && !vm);
    beam_composition_release(&composition);
    for (int pass = 0; pass < 3; ++pass) {
        const int selected = pass != 1;
        options.composition = NULL;
        if (selected) {
            unsigned char *copy = malloc(bytes.size);
            char name[] = "preload_probe", origin[] = "copied:probe";
            CHECK(copy);
            memcpy(copy, bytes.data, bytes.size);
            beam_module_spec input = {name, {copy, bytes.size}, origin};
            spec.preloads = &input; spec.defer_modules = 1;
            CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_OK);
            memset(copy, 0, bytes.size); free(copy);
            memset(name, 0, sizeof(name)); memset(origin, 0, sizeof(origin));
            const beam_module_spec *inventory;
            size_t count;
            CHECK(beam_composition_get_modules(composition, &inventory, &count) == BEAM_OK &&
                  count == 1 && !strcmp(inventory[0].name, "preload_probe") &&
                  !strcmp(inventory[0].origin, "copied:probe"));
            options.composition = composition;
        }
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_vm_load(vm, probe.bytes) == (selected ? BEAM_ALREADY_LOADED : BEAM_OK));
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        const unsigned char nil[] = {131, 106};
        beam_result result = {0};
        CHECK(beam_vm_call(vm, "preload_probe", "probe", (beam_bytes){nil, sizeof(nil)}, &result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN && result.value.size == 3 &&
              result.value.data[0] == 131 && result.value.data[1] == 97 &&
              result.value.data[2] == (selected ? 10 : 9));
        beam_result_release(&result);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    CHECK(beam_terminate(&system) == BEAM_OK);
    free(bytes.data); free(on_load.data);
    puts("composition preload lifetime checks passed");
    return 0;
}
