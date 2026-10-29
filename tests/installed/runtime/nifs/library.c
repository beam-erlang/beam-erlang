/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/runtime.h>
#include <beam/library.h>
#include LIBRARY_HEADER
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "library check %d: %s (%s)\n", __LINE__, #x, error.message); return 1; } } while (0)

int main(int argc, char **argv)
{
    beam_error error = {0};
    beam_library *library = NULL;
    beam_library_descriptor invalid = *LIBRARY_ACCESSOR();
    invalid.build_id = "wrong SDK";
    CHECK(beam_library_from_static(&invalid, &library, &error) == BEAM_INVALID_ARGUMENT && !library);
    invalid = *LIBRARY_ACCESSOR();
    invalid.format_version++;
    CHECK(beam_library_from_static(&invalid, &library, &error) == BEAM_INVALID_ARGUMENT && !library);
    beam_module_spec duplicate[2] = {LIBRARY_ACCESSOR()->modules[0], LIBRARY_ACCESSOR()->modules[0]};
    invalid = *LIBRARY_ACCESSOR();
    invalid.module_count = 2; invalid.modules = duplicate;
    CHECK(beam_library_from_static(&invalid, &library, &error) == BEAM_INVALID_ARGUMENT && !library);
    invalid = *LIBRARY_ACCESSOR();
    beam_module_spec labelled = invalid.modules[0];
    labelled.origin = "author:supplied-origin";
    invalid.module_count = 1; invalid.modules = &labelled;
    invalid.nif_count = 0; invalid.nifs = NULL;
    CHECK(beam_library_from_static(&invalid, &library, &error) == BEAM_OK);
    beam_library_inputs labelled_inputs = {.struct_size=sizeof(labelled_inputs)};
    CHECK(beam_library_get_inputs(library, &labelled_inputs) == BEAM_OK &&
          !strcmp(labelled_inputs.modules[0].origin, labelled.origin));
    beam_library_release(&library);
    beam_system *system = NULL;
    CHECK(beam_init(&system, &error) == BEAM_OK);
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        if (argc == 2) {
            CHECK(beam_library_open(argv[1], &library, &error) == BEAM_OK);
        } else {
            CHECK(beam_library_from_static(LIBRARY_ACCESSOR(), &library, &error) == BEAM_OK);
        }
        beam_library_inputs inputs = {.struct_size = sizeof(inputs)};
        CHECK(beam_library_get_inputs(library, &inputs) == BEAM_OK);
        beam_composition_options selection = {.struct_size = sizeof(selection),
            .module_count = inputs.module_count, .modules = inputs.modules,
            .builtin_nif_count = inputs.nif_count, .builtin_nifs = inputs.nifs,
            .native_owner_count = 1, .native_owners = &inputs.owner};
        beam_composition *composition = NULL;
        CHECK(beam_composition_prepare(&selection, &composition, &error) == BEAM_OK);
        beam_library_release(&library);
        beam_vm_options options;
        beam_vm_options_init(&options);
        options.composition = composition;
        beam_vm *vm = NULL;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        const unsigned char nil[] = {131,106};
        beam_result result = {0};
        CHECK(beam_vm_call(vm, "fourth", "value", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
        CHECK(result.kind == BEAM_RETURN);
        beam_result_release(&result);
        if (inputs.nif_count) {
            const unsigned char arguments[] = {131,108,0,0,0,1,109,0,0,0,7,'i','g','n','o','r','e','d',106};
            CHECK(beam_vm_call(vm, "probe", "implicit", (beam_bytes){arguments,sizeof(arguments)}, &result) == BEAM_OK);
            CHECK(result.kind == BEAM_RETURN && result.value.size >= 2 && !memcmp(result.value.data+result.value.size-2,"ok",2));
            beam_result_release(&result);
            CHECK(beam_vm_call(vm, "probe", "retain", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
            CHECK(result.kind == BEAM_RETURN);
            beam_result_release(&result);
        }
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    CHECK(beam_terminate(&system) == BEAM_OK);
    puts("PASSIVE_LIBRARY_OK");
    return 0;
}
