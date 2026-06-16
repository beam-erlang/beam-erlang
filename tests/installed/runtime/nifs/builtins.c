/* SPDX-License-Identifier: Apache-2.0 */
#include <beam.h>
#include <erl_nif.h>
#include <erl_driver.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "built-in check %d: %s (%s)\n", __LINE__, #x, error.message); return 1; } } while (0)
extern ErlNifEntry *probe_nif_init(void);
static int nif_calls, driver_calls, driver_started, driver_finished, failure_mode;
static ErlNifEntry *nif_initialize(void)
{
    static ErlNifEntry entry;
    ++nif_calls;
    entry = *probe_nif_init();
    if (failure_mode == 4) entry.name = "wrong_name";
    if (failure_mode == 5) entry.major = ERL_NIF_MAJOR_VERSION + 1;
    return failure_mode == 1 ? NULL : &entry;
}
static int driver_start(void) { ++driver_started; return failure_mode == 3 ? -1 : 0; }
static void driver_finish(void) { ++driver_finished; }
static ErlDrvEntry driver = {
    .init = driver_start, .finish = driver_finish, .driver_name = "linked_probe",
    .extended_marker = ERL_DRV_EXTENDED_MARKER,
    .major_version = ERL_DRV_EXTENDED_MAJOR_VERSION,
    .minor_version = ERL_DRV_EXTENDED_MINOR_VERSION
};
static ErlDrvEntry *driver_initialize(void)
{
    ++driver_calls;
    return failure_mode == 2 ? NULL : &driver;
}
static int is_ok(const beam_result *result)
{
    return result->kind == BEAM_RETURN && result->value.size >= 5 &&
        !memcmp(result->value.data + result->value.size - 2, "ok", 2);
}
int main(int argc, char **argv)
{
    beam_error error = {0};
    CHECK(argc == 2);
    FILE *file = fopen(argv[1], "rb"); CHECK(file);
    CHECK(!fseek(file, 0, SEEK_END)); long length = ftell(file); CHECK(length > 0);
    rewind(file); unsigned char *bytes = malloc(length); CHECK(bytes);
    CHECK(fread(bytes, 1, length, file) == (size_t)length); fclose(file);
    beam_module_spec module = {"probe", {bytes, (size_t)length}, argv[1]};
    char name[32] = "probe", identity[65]; strcpy(identity, beam_build_id());
    beam_builtin_nif nifs[2] = {{sizeof(beam_builtin_nif), name, identity, nif_initialize, 0}};
    nifs[1] = nifs[0];
    beam_composition_options specification = {.struct_size = sizeof(specification),
        .module_count = 1, .modules = &module, .builtin_nif_count = 2, .builtin_nifs = nifs};
    beam_composition *composition = NULL;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    CHECK(!composition && !nif_calls);
    specification.builtin_nif_count = 1;
    strcpy(name, "erlang");
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    CHECK(!composition && !nif_calls);
    name[0] = '\0';
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    strcpy(name, "probe");
    identity[0] = 'x';
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    strcpy(identity, beam_build_id());
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    CHECK(!nif_calls);
    memset(name, 'x', sizeof(name)); memset(identity, 'x', sizeof(identity));
    memset(nifs, 0, sizeof(nifs));
    beam_system *system = NULL; beam_vm *vm = NULL;
    CHECK(beam_init(&system, &error) == BEAM_OK);
    beam_vm_options options; beam_vm_options_init(&options); options.composition = composition;
    const int invalid_nifs[] = {1, 4, 5};
    for (size_t i = 0; i < sizeof(invalid_nifs) / sizeof(invalid_nifs[0]); ++i) {
        failure_mode = invalid_nifs[i];
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(!vm && strstr(error.message, "built-in NIF initialization failed: probe"));
    }
    failure_mode = 0;
    const unsigned char nil[] = {131,106};
    const unsigned char path[] = {131,108,0,0,0,1,107,0,7,'b','u','i','l','t','i','n',106};
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        beam_result result = {0};
        CHECK(beam_vm_call(vm, "probe", "implicit", (beam_bytes){path,sizeof(path)}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        CHECK(beam_vm_call(vm, "probe", "packets", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        CHECK(beam_vm_call(vm, "probe", "retain", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    CHECK(nif_calls == 5);
    beam_composition_release(&composition);
    // A fresh VM without the selection must not retain the native binding.
    specification.builtin_nif_count = 0;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    beam_result result = {0};
    CHECK(beam_vm_call(vm, "probe", "implicit", (beam_bytes){path,sizeof(path)}, &result) == BEAM_OK);
    CHECK(!is_ok(&result)); beam_result_release(&result);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    beam_composition_release(&composition);

    beam_builtin_driver drivers[2] = {{sizeof(beam_builtin_driver), "linked_probe", beam_build_id(), driver_initialize, 0}};
    drivers[1] = drivers[0];
    specification.native_admission = BEAM_NATIVE_DRIVERS;
    specification.builtin_driver_count = 2; specification.builtin_drivers = drivers;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    CHECK(!driver_calls);
    specification.builtin_driver_count = 1;
    drivers[0].name = "forker";
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    CHECK(!composition && !driver_calls);
    drivers[0].name = "linked_probe";
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    memset(drivers, 0, sizeof(drivers));
    beam_entry entry = {"probe", "packets", {nil,sizeof(nil)}};
    options.execution = BEAM_PROCESS_OWNED; options.initial_root = &entry; options.composition = composition;
    for (int mode = 2; mode <= 3; ++mode) {
        failure_mode = mode;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(!vm && strstr(error.message, "built-in driver initialization failed: linked_probe"));
    }
    failure_mode = 0;
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    if (driver_calls != 4 || driver_started != 3 || driver_finished != 2)
        fprintf(stderr, "driver callbacks: initialize=%d start=%d finish=%d\n", driver_calls, driver_started, driver_finished);
    CHECK(driver_calls == 4 && driver_started == 3 && driver_finished == 2);
    beam_composition_release(&composition);
    CHECK(beam_terminate(&system) == BEAM_OK);
    free(bytes);
    puts("BUILTIN_LIFETIMES_OK");
    return 0;
}
