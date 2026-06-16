/* SPDX-License-Identifier: Apache-2.0 */
#include <beam.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "package native consumer: %d: %s (%s)\n", __LINE__, #x, error.message); return 1; } } while (0)
int main(int argc, char **argv)
{
    beam_error error = {0};
    CHECK(argc == 3);
    FILE *input = fopen(argv[1], "rb");
    CHECK(input && !fseek(input, 0, SEEK_END));
    long length = ftell(input);
    CHECK(length > 0 && !fseek(input, 0, SEEK_SET));
    unsigned char *bytes = malloc((size_t)length);
    CHECK(bytes && fread(bytes, 1, (size_t)length, input) == (size_t)length);
    CHECK(!fclose(input));
    beam_module_spec module = {"pkg_acceptance", {bytes, (size_t)length}, argv[1]};
    beam_nif_spec nif = {sizeof nif, "pkg_acceptance", argv[2]};
    beam_composition_options composition_options = {.struct_size = sizeof composition_options,
        .module_count = 1, .modules = &module, .nif_count = 1, .nifs = &nif};
    beam_composition *composition = NULL;
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    CHECK(beam_composition_prepare(&composition_options, &composition, &error) == BEAM_OK);
    free(bytes);
    CHECK(beam_init(&system, &error) == BEAM_OK);
    beam_vm_options_init(&options);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    beam_composition_release(&composition);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(beam_vm_activate(vm, &error) == BEAM_OK);
    const unsigned char nil[] = {131, 106};
    beam_result result = {0};
    CHECK(beam_vm_call(vm, "pkg_acceptance", "value", (beam_bytes){nil, sizeof nil}, &result) == BEAM_OK);
    CHECK(result.kind == BEAM_RETURN && result.value.size == 3 &&
          result.value.data[0] == 131 && result.value.data[1] == 97 && result.value.data[2] == 19);
    beam_result_release(&result);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    CHECK(beam_terminate(&system) == BEAM_OK);
    puts("PKG_NATIVE_OK");
    return 0;
}
