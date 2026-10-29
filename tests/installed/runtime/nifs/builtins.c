/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <erl_nif.h>
#include <erl_driver.h>
#include <beam/native.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "built-in check %d: %s (%s)\n", __LINE__, #x, error.message); return 1; } } while (0)
extern ErlNifEntry *probe_nif_init(ERL_NIF_INIT_ARGS);
extern int probe_report_cleanup, probe_reclaimed;
static int nif_calls, driver_calls, driver_started, driver_finished, failure_mode;
static ErlNifEntry *nif_initialize(void)
{
    static ErlNifEntry entry;
    ++nif_calls;
    entry = *probe_nif_init(
#ifdef _WIN32
        NULL
#endif
    );
    if (failure_mode == 4) entry.name = "wrong_name";
    if (failure_mode == 5) entry.major = ERL_NIF_MAJOR_VERSION + 1;
    return failure_mode == 1 ? NULL : &entry;
}
static ErlNifEntry *native_initialize(ERL_NIF_INIT_ARGS)
{
#ifdef _WIN32
    (void)callbacks;
#endif
    return nif_initialize();
}
struct native_owner_state { int references, retained, released; };
static void *fail_runtime(void *unused)
{
    (void)unused;
    char origin[] = "native-monitor", reason[] = "owner exited";
    beam_native_request_failure(origin, reason);
    memset(origin, 0, sizeof(origin)); memset(reason, 0, sizeof(reason));
    beam_native_request_failure("late-monitor", "must not replace first failure");
    return NULL;
}
static void retain_owner(void *context)
{
    struct native_owner_state *owner = context;
    ++owner->references;
    ++owner->retained;
}
static void release_owner(void *context)
{
    struct native_owner_state *owner = context;
    if (probe_report_cleanup && probe_reclaimed != 2) abort();
    --owner->references;
    ++owner->released;
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
static int check_embedded_reporting(beam_system *system, const beam_module_spec *module)
{
    beam_error error = {0};
    char included[] = "fixture_included", excluded[] = "fixture_excluded";
    beam_embedded_dependency entries[] = {
        {sizeof(beam_embedded_dependency), included, BEAM_EMBEDDED_INCLUDED},
        {sizeof(beam_embedded_dependency), excluded, BEAM_EMBEDDED_EXCLUDED}};
    beam_composition_options specification = {.struct_size = sizeof(specification),
        .module_count = 1, .modules = module,
        .embedded_dependency_count = 2, .embedded_dependencies = entries};
    beam_composition *composition = NULL;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    memset(included, 'x', sizeof(included)); memset(excluded, 'x', sizeof(excluded));
    memset(entries, 0, sizeof(entries));
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        beam_vm_options options; beam_vm_options_init(&options); options.composition = composition;
        beam_vm *vm = NULL;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        const char *mode = lifetime ? "absent" : "present";
        unsigned char arguments[20] = {131,108,0,0,0,1,119};
        size_t length = strlen(mode); arguments[7] = (unsigned char)length;
        memcpy(arguments + 8, mode, length); arguments[8 + length] = 106;
        beam_result result = {0};
        CHECK(beam_vm_call(vm, "probe", "embedded_dependencies",
              (beam_bytes){arguments, 9 + length}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
        if (!lifetime) {
            specification.embedded_dependency_count = 0;
            specification.embedded_dependencies = NULL;
            CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
        }
    }
    return 0;
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
    CHECK(check_embedded_reporting(system, &module) == 0);
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

    /* Explicit linked activation must not change the built-in registry path. */
    beam_nif_binding linked = {sizeof(linked), "probe", beam_build_id(), nif_initialize};
    specification.binding_count = 1; specification.bindings = &linked;
    linked.build_id = "wrong-sdk";
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    linked.build_id = beam_build_id();
    int before_validation = nif_calls;
    linked.module = "missing";
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    linked.module = "probe";
    beam_nif_binding duplicates[2] = {linked, linked};
    specification.bindings = duplicates; specification.binding_count = 2;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    specification.bindings = &linked; specification.binding_count = 1;
    beam_builtin_nif conflict = {sizeof(conflict), "probe", beam_build_id(), nif_initialize, 0};
    specification.builtin_nifs = &conflict; specification.builtin_nif_count = 1;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
    specification.builtin_nif_count = 0;
    CHECK(nif_calls == before_validation);
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    /* Preparation copies selection metadata; activation cannot consult this struct. */
    linked.module = "discarded"; linked.initialize = NULL;
    options.composition = composition;
    for (size_t i = 0; i < sizeof(invalid_nifs) / sizeof(invalid_nifs[0]); ++i) {
        failure_mode = invalid_nifs[i];
        int before = nif_calls;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK && nif_calls == before);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) != BEAM_OK && nif_calls == before + 1);
        beam_vm_state_info state;
        CHECK(beam_vm_get_state(vm, &state) == BEAM_OK && state.state == BEAM_VM_STOPPED);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK && !vm);
    }
    failure_mode = 0;
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        int before = nif_calls;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        CHECK(nif_calls == before);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) == BEAM_OK);
        CHECK(nif_calls == before + 1);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) == BEAM_OK && nif_calls == before + 1);
        CHECK(beam_vm_call(vm, "probe", "retain", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    }
    beam_composition_release(&composition);
    specification.binding_count = 0;
    CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
    options.composition = composition;
    CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(beam_vm_call(vm, "probe", "retain", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
    CHECK(!is_ok(&result)); beam_result_release(&result);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
    beam_composition_release(&composition);

    /* Generic bindings retain acquisition owners independently of caller input.
     * Releasing a preparation while its VM runs must not release native code. */
    for (int platform_entry = 0; platform_entry < 2; ++platform_entry) {
        struct native_owner_state owner_state = {0};
        beam_native_owner owner = {sizeof(owner), &owner_state, retain_owner, release_owner};
        beam_nif_binding binding = {.struct_size = sizeof(binding), .module = "probe",
            .build_id = beam_build_id(), .initialize = platform_entry ? NULL : nif_initialize,
            .native_initialize = platform_entry ? native_initialize : NULL};
        specification.binding_count = 1;
        specification.bindings = &binding;
        specification.native_owner_count = 1;
        specification.native_owners = &owner;
        binding.module = "missing";
        CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(!composition && !owner_state.references && owner_state.retained == owner_state.released);
        binding.module = "probe";
        CHECK(beam_composition_prepare(&specification, &composition, &error) == BEAM_OK);
        CHECK(owner_state.references == 1);
        options.composition = composition;
        int before = nif_calls;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        beam_composition_release(&composition);
        memset(&owner, 0, sizeof(owner));
        memset(&binding, 0, sizeof(binding));
        CHECK(owner_state.references == 1 && nif_calls == before);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) == BEAM_INVALID_STATE);
        CHECK(beam_vm_start(vm, &error) == BEAM_OK);
        CHECK(beam_vm_bind_nif(vm, "missing", &error) == BEAM_NOT_FOUND && nif_calls == before);
        CHECK(beam_vm_bind_nif(vm, NULL, &error) == BEAM_INVALID_ARGUMENT);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) == BEAM_OK && nif_calls == before + 1);
        CHECK(beam_vm_bind_nif(vm, "probe", &error) == BEAM_OK && nif_calls == before + 1);
        CHECK(beam_vm_call(vm, "probe", "retain", (beam_bytes){nil,sizeof(nil)}, &result) == BEAM_OK);
        CHECK(is_ok(&result)); beam_result_release(&result);
        probe_report_cleanup = platform_entry;
        if (platform_entry) {
            ErlNifTid monitor;
            CHECK(enif_thread_create("failure_monitor", &monitor, fail_runtime, NULL, NULL) == 0);
            CHECK(enif_thread_join(monitor, NULL) == 0);
            CHECK(beam_native_stop_requested());
            beam_vm_state_info state;
            CHECK(beam_vm_get_state(vm, &state) == BEAM_OK && state.state == BEAM_VM_HALT_REQUESTED);
            beam_vm_report report = {.struct_size = sizeof(report)};
            CHECK(beam_vm_terminate_report(&vm, NULL, &report) == BEAM_OK);
            CHECK(report.cause == BEAM_TERMINAL_FAILURE && !strcmp(report.origin, "native-monitor"));
            CHECK(!strcmp(report.failure.message, "owner exited"));
            CHECK(report.has_cleanup_error && report.cleanup.native_code == 41 &&
                  !strcmp(report.cleanup.message, "injected destructor diagnostic"));
            CHECK(probe_reclaimed == 2);
        } else {
            CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
        }
        CHECK(!owner_state.references && owner_state.retained == owner_state.released);
        probe_report_cleanup = 0;
    }
    specification.binding_count = specification.native_owner_count = 0;
    specification.bindings = NULL;
    specification.native_owners = NULL;

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
