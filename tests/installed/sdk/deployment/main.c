#define _GNU_SOURCE
#include <beam/beam.h>
#include "native_counts.h"
#include <beam/boot.h>
#ifdef DEPLOY_RETURNING
#include "bytecode_input.h"
#include "host_observe.h"
#endif
#include "deployment_host_deployment.h"
#include "entry.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "deployment check %d: %s (%s)\n", __LINE__, #x, error.message); goto cleanup; } } while (0)
static unsigned char *read_bytes(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb"); long length; unsigned char *bytes = NULL;
    if (!f) return NULL;
    if (!fseek(f, 0, SEEK_END) && (length = ftell(f)) > 0 && !fseek(f, 0, SEEK_SET)) {
        bytes = malloc((size_t)length);
        if (bytes && fread(bytes, 1, (size_t)length, f) != (size_t)length) { free(bytes); bytes = NULL; }
        if (bytes) *size = (size_t)length;
    }
    fclose(f); return bytes;
}
static beam_output_status output(void *context, beam_bytes destination, beam_bytes bytes)
{
    (void)context; (void)destination;
    return fwrite(bytes.data, 1, bytes.size, stdout) == bytes.size && !fflush(stdout)
        ? BEAM_OUTPUT_OK : BEAM_OUTPUT_FAILED;
}
int main(int argc, char **argv)
{
    beam_error error = {0}; beam_status status;
    char root[PATH_MAX], paths[4][PATH_MAX], native[PATH_MAX], helper[PATH_MAX], origin[PATH_MAX];
    size_t required; int result = 1;
    beam_system *system = NULL; beam_vm *vm = NULL; beam_vm_options vm_options;
    beam_return_boot *returning = NULL;
    beam_outcome outcome = {0}; beam_result bare_result = {0};
    unsigned char *provider = NULL;
#ifdef DEPLOY_RETURNING
    bytecode_input base = {0};
#endif
#ifdef BEAM_TARGET_ACCEPTANCE
    CHECK(argc == 3);
    CHECK(realpath(argv[2], root) != NULL);
    CHECK(beam_library_path(origin, sizeof origin, &required, &error) == BEAM_OK);
    CHECK(snprintf(native, sizeof native, "%s/%s", root, TARGET_RUNTIME) < (int)sizeof native);
    CHECK(realpath(native, paths[0]) != NULL && !strcmp(origin, paths[0]));
#else
    CHECK(argc == 2);
    CHECK(beam_library_path(root, sizeof root, &required, &error) == BEAM_OK);
    char *slash = strrchr(root, '/'); CHECK(slash); *slash = 0;
    slash = strrchr(root, '/'); CHECK(slash); *slash = 0;
    CHECK(getenv("DEPLOY_EXPECT_ROOT") && !strcmp(root, getenv("DEPLOY_EXPECT_ROOT")));
#endif
    CHECK(beam_init(&system, &error) == BEAM_OK);
#ifdef DEPLOY_RETURNING
    beam_return_options options = {0};
    beam_library *library=NULL;
    beam_startup_action startup[2]={{"beam_output","start",{0}}};
    beam_composition_options generic={.struct_size=sizeof(generic)};
    beam_host_options plan={.struct_size=sizeof(plan),.composition=&generic,.startup_count=1,.startup=startup};
    beam_return_module modules[14] = {{0}};
    options.struct_size = sizeof options;
    options.entry_module = "entry"; options.entry_function = argv[1];
    CHECK(bytecode_input_read(&base, root, INPUT_RETURNING, &error) == BEAM_OK);
    for (size_t i = 0; i < base.count; ++i) {
        modules[i+1].name = base.modules[i].name; modules[i+1].bytes = base.modules[i].bytes;
        modules[i+1].origin = base.modules[i].origin;
    }
    options.modules = modules; options.module_count = 14;
    modules[0].name = "entry"; modules[0].bytes = (beam_bytes){entry_bytes, sizeof entry_bytes};
    options.host=&plan;
    if (!strcmp(argv[1], "file") || !strcmp(argv[1], "network") || !strcmp(argv[1], "zlib")) {
        int file = !strcmp(argv[1], "file"), zlib = !strcmp(argv[1], "zlib");
#ifdef BEAM_TARGET_ACCEPTANCE
        CHECK(snprintf(paths[3], sizeof paths[3], "%s/%s", root,
              file ? TARGET_FILE : zlib ? TARGET_ZLIB : TARGET_NETWORK) < (int)sizeof paths[3]);
#else
        CHECK(snprintf(paths[3], sizeof paths[3], "%s/%s", root, beam_test_component(argv[1])) < (int)sizeof paths[3]);
#endif
        CHECK(beam_library_open(paths[3],&library,&error)==BEAM_OK);
        plan.library_count=1;plan.libraries=&library;
        startup[plan.startup_count++]=(beam_startup_action){file?"beam_file":zlib?"zlib":"beam_socket",zlib?"on_load":"start",{0}};
    }
    if (!strcmp(argv[1], "program")) {
        generic.native_admission=BEAM_NATIVE_MANAGED_PORTS;
        CHECK(snprintf(helper, sizeof helper, "%s/libexec/beam/erl_child_setup", root) < (int)sizeof helper);
        generic.program_helper=helper;
    }
    options.output = output;
    for (int pass = 0; pass < 2; ++pass) {
        CHECK(beam_return_prepare(&options, &returning, &error) == BEAM_OK);
        outcome.struct_size = sizeof outcome;
        CHECK(beam_return_run(system, &returning, &outcome, &error) == BEAM_OK);
        if (outcome.kind != BEAM_OUTCOME_RETURN && outcome.term.data)
            fwrite(outcome.term.data, 1, outcome.term.size, stderr);
        CHECK(outcome.kind == BEAM_OUTCOME_RETURN && !outcome.has_cleanup_error);
        CHECK(outcome.term.size == 3 && !memcmp(outcome.term.data, "\203\141\052", 3));
        beam_outcome_release(&outcome);
        CHECK(!beam_file_descriptor_count() && !beam_socket_count() && !beam_zlib_stream_count());
    }
    beam_library_release(&library);
#else
    beam_vm_options_init(&vm_options);
    CHECK(beam_vm_init(system, &vm_options, &vm, &error) == BEAM_OK);
    CHECK(beam_vm_load(vm, (beam_bytes){entry_bytes, sizeof entry_bytes}) == BEAM_OK);
    CHECK(beam_vm_start(vm, &error) == BEAM_OK);
    CHECK(beam_vm_call(vm, "entry", "bare", (beam_bytes){(const unsigned char *)"\203\152", 2}, &bare_result) == BEAM_OK);
    CHECK(bare_result.kind == BEAM_RETURN && bare_result.value.size == 3 && !memcmp(bare_result.value.data, "\203\141\052", 3));
    CHECK(beam_vm_stop(vm, NULL) == BEAM_OK);
    CHECK(beam_vm_terminate(&vm, NULL) == BEAM_OK);
#endif
    CHECK(beam_terminate(&system) == BEAM_OK);
    puts("DEPLOY_OK"); result = 0;
cleanup:
#ifdef DEPLOY_RETURNING
    bytecode_input_release(&base);
#endif
    beam_return_discard(&returning);
    if (vm) beam_vm_terminate(&vm, NULL);
    if (system) beam_terminate(&system);
    beam_result_release(&bare_result); beam_outcome_release(&outcome); free(provider);
    return result;
}
