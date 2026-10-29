/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"
#include "native_counts.h"
#include <unistd.h>
#include "bytecode_input.h"

void completion_mode(const char *root, const char *fixture)
{
    beam_return_module modules[4];
    size_t count = 0;
    sink output = {0};
    beam_outcome outcome = {0};
    const unsigned char value[] = "ok";
    const unsigned char *items[] = {value};
    size_t sizes[] = {2};
    const unsigned char dummy_text[] = "_";
    const unsigned char *dummy_items[] = {dummy_text};
    size_t dummy_sizes[] = {1};
    beam_bytes arguments = list_of_binaries(items, sizes, 1);
    beam_bytes empty = list_of_binaries(dummy_items, dummy_sizes, 1);
    add_program(modules, &count, fixture);
    start_system();
    run_entry(root, "ret", arguments, TEST_OUTPUT, modules, count, &output, &outcome);
    expect_return_text(&outcome, "ok");
    check(!outcome.has_cleanup_error, "cleanup on return");
    beam_outcome_release(&outcome);
    memset(&output, 0, sizeof(output));
    run_entry(root, "boom", empty, 0, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_EXCEPTION, "exception kind");
    beam_outcome_release(&outcome);
    run_entry(root, "finish", empty, 0, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_FINISH && outcome.halt_status == 4, "finish");
    beam_outcome_release(&outcome);
    run_entry(root, "halt", empty, 0, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_HALT && outcome.halt_status == 9, "halt");
    beam_outcome_release(&outcome);
    run_entry(root, "kill_output", empty, TEST_OUTPUT, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_FAILURE, "owner death");
    check(strcmp(outcome.origin, "output") == 0, "failed service");
    beam_outcome_release(&outcome);
    run_entry(root, "cleanup", empty, 0, modules, count, &output, &outcome);
    check(outcome.kind == BEAM_OUTCOME_RETURN && outcome.has_cleanup_error, "cleanup flag");
    check(strcmp(outcome.cleanup_error, "cleanup-fault") == 0, "cleanup bytes");
    beam_outcome_release(&outcome);
    for (int race = 0; race < 20; ++race) {
        run_entry(root, "return_then_halt", empty, 0, modules, count, &output, &outcome);
        expect_return_text(&outcome, "won");
        beam_outcome_release(&outcome);
        run_entry(root, "exception_then_halt", empty, 0, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_EXCEPTION, "entry exception wins late child halt");
        beam_outcome_release(&outcome);
        run_entry(root, "finish_then_halt", empty, 0, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_FINISH && outcome.halt_status == 6, "finish wins late child halt");
        beam_outcome_release(&outcome);
        run_entry(root, "service_then_halt", empty, TEST_OUTPUT, modules, count, &output, &outcome);
        check(outcome.kind == BEAM_OUTCOME_FAILURE && !strcmp(outcome.origin,"output"), "service failure wins late child halt");
        beam_outcome_release(&outcome);
    }
    stop_system();
    free((void *)arguments.data);
    free((void *)empty.data);
    free((void *)modules[0].bytes.data);
    printf("completion ok\n");
}

static int task_count(void)
{
#ifdef __APPLE__
    int count=beam_test_host_threads(getpid()); check(count>0,"process tasks"); return count;
#else
    DIR *directory = opendir("/proc/self/task");
    struct dirent *entry;
    int count = 0;
    check(directory != NULL, "process tasks");
    while ((entry = readdir(directory)) != NULL)
        if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count;
#endif
}

static void reject_preparation(beam_return_options *options, beam_status expected)
{
    beam_return_boot *boot = NULL;
    beam_error error = {0};
    int descriptors[1024], descriptor_count, tasks = task_count();
    int files = beam_file_descriptor_count(), sockets = beam_socket_count(), streams = beam_zlib_stream_count();
    snapshot_fds(descriptors, 1024, &descriptor_count);
    beam_status status = beam_return_prepare(options, &boot, &error);
    if (status != expected)
        fprintf(stderr, "preparation status %d, expected %d: %s\n", status, expected, error.message);
    check(status == expected, "raw preparation rejection status");
    check(boot == NULL && error.message[0], "raw rejection changed output or omitted diagnostic");
    check(task_count() == tasks && files == beam_file_descriptor_count() &&
          sockets == beam_socket_count() && streams == beam_zlib_stream_count(), "preparation activated resources");
    check_fds(descriptors, descriptor_count);
}

static void library_options(const char *root, beam_return_options *options,
                            beam_host_options *host, beam_library **libraries,
                            char paths[2][4096], beam_return_module *modules)
{
    static const char *names[]={"file","network"};
    static const beam_startup_action startup[]={{"beam_file","start",{0}},{"beam_socket","start",{0}}};
    beam_error error={0};
    *options=(beam_return_options){.struct_size=sizeof(*options),.entry_module="returning_programs",
        .entry_function="ret",.modules=modules,.module_count=13,.host=host};
    *host=(beam_host_options){.struct_size=sizeof(*host),.library_count=2,.libraries=libraries,.startup_count=2,.startup=startup};
    for(int i=0;i<2;++i){
        snprintf(paths[i],4096,"%s/%s",root,beam_test_component(names[i]));
        check(beam_library_open(paths[i],&libraries[i],&error)==BEAM_OK,error.message);
    }
}

static void component_validation(const char *root, const char *fixture)
{
    beam_return_options options;beam_host_options host;beam_library *libraries[2]={0};
    beam_return_module modules[15]={{0}};
    beam_return_module *module=&modules[13];
    beam_return_boot *boot=NULL;beam_error error={0};
    char paths[2][4096],path[4096];bytecode_input base={0};
    check(bytecode_input_read(&base,root,INPUT_RETURNING,&error)==BEAM_OK,error.message);
    for(size_t i=0;i<base.count;++i){modules[i].name=base.modules[i].name;modules[i].bytes=base.modules[i].bytes;modules[i].origin=base.modules[i].origin;}
    library_options(root,&options,&host,libraries,paths,modules);
    --options.struct_size;reject_preparation(&options,BEAM_INVALID_ARGUMENT);++options.struct_size;
    host.libraries=NULL;reject_preparation(&options,BEAM_INVALID_ARGUMENT);host.libraries=libraries;
    beam_library *bad=NULL;
    check(beam_library_open("/no-such-library/selected.so",&bad,&error)==BEAM_RUNTIME_NOT_FOUND&&!bad,"missing shared library");
    snprintf(path,sizeof(path),"%s/mismatched-component.so",fixture);
    check(beam_library_open(path,&bad,&error)==BEAM_INVALID_ARGUMENT&&!bad,"incompatible descriptor");
    beam_library *duplicate[]={libraries[0],libraries[0]};host.libraries=duplicate;
    reject_preparation(&options,BEAM_INVALID_ARGUMENT);host.libraries=libraries;
    snprintf(path,sizeof(path),"%s/returning_programs.beam",fixture);
    *module=module_from_file(path,"returning_programs",0);options.module_count=14;
    const char *conflicts[]={"beam_map","erlang","beam_boot","beam_host","prim_file"};
    for(size_t i=0;i<5;++i){module->name=conflicts[i];reject_preparation(&options,BEAM_INVALID_ARGUMENT);}
    module->name="wrong_identity";reject_preparation(&options,BEAM_INVALID_BEAM);
    module->name="returning_programs";modules[14]=*module;options.module_count=15;
    reject_preparation(&options,BEAM_INVALID_ARGUMENT);options.module_count=14;
    beam_bytes original=module->bytes;module->bytes=(beam_bytes){(const unsigned char*)"invalid bytecode",16};
    reject_preparation(&options,BEAM_INVALID_BEAM);module->bytes=original;
    /* Deferred code still participates in conflict and bytecode validation. */
    modules[14]=*module;modules[14].deferred=1;options.module_count=15;
    reject_preparation(&options,BEAM_INVALID_ARGUMENT);options.module_count=14;
    check(beam_return_prepare(&options,&boot,&error)==BEAM_OK,error.message);
    beam_return_boot *retained=boot;
    check(beam_return_prepare(&options,&boot,&error)==BEAM_INVALID_ARGUMENT&&boot==retained,"occupied output changed");
    check(beam_return_discard(&boot)==BEAM_OK&&!boot,"discard passive preparation");
    for(size_t i=0;i<2;++i)beam_library_release(&libraries[i]);
    free((void*)module->bytes.data);bytecode_input_release(&base);
}

void components_mode(const char *root, const char *fixture)
{
    beam_return_options options;
    beam_library *libraries[2]={0};
    beam_host_options host;
    beam_return_module modules[15] = {{0}};
    size_t count = 13;
    bytecode_input base = {0};
    char paths[2][4096], hidden[2][4112], rejected[4096];
    beam_error error = {0};
    const unsigned char *items[] = {(const unsigned char *)"ok"};
    const size_t sizes[] = {2};
    beam_bytes arguments = list_of_binaries(items, sizes, 1);
    component_validation(root, fixture);
    check(bytecode_input_read(&base, root, INPUT_RETURNING, &error) == BEAM_OK, error.message);
    for (size_t i = 0; i < base.count; ++i) {
        modules[i].name = base.modules[i].name; modules[i].bytes = base.modules[i].bytes;
        modules[i].origin = base.modules[i].origin;
    }
    library_options(root, &options, &host, libraries, paths, modules);
    add_program(modules, &count, fixture);
    snprintf(rejected, sizeof rejected, "%s/onload_rejected.beam", fixture);
    modules[14] = module_from_file(rejected, "onload_rejected", 1);
    options.module_count = count;
    options.modules = modules;
    options.arguments = arguments;
    start_system();
    for (int failure = 0; failure < 3; ++failure) {
        beam_return_boot *first = NULL, *second = NULL;
        beam_outcome first_outcome = {0}, second_outcome = {0};
        int descriptors[1024], descriptor_count, tasks = task_count();
        snapshot_fds(descriptors, 1024, &descriptor_count);
        options.entry_function = failure == 1 ? "boom" : "ret";
        options.module_count = failure == 2 ? 15 : 14;
        check(beam_return_prepare(&options, &first, &error) == BEAM_OK, error.message);
        options.entry_function = "ret";
        /* Generic preloads are already loaded by VM initialization. The Host
         * must preserve that input without attempting a second ordinary load. */
        beam_composition_options preloaded = {.struct_size=sizeof(preloaded),
            .preload_count=1,.preloads=&base.modules[0]};
        host.composition = &preloaded;
        options.modules = modules + 1;
        options.module_count = 13;
        check(beam_return_prepare(&options, &second, &error) == BEAM_OK, error.message);
        host.composition = NULL;
        options.modules = modules;
        options.module_count = 14;
        check(task_count() == tasks, "preparation started workers");
        check_fds(descriptors, descriptor_count);
        for (int index = 0; index < 2; ++index) {
            snprintf(hidden[index], sizeof hidden[index], "%s.retained", paths[index]);
            check(rename(paths[index], hidden[index]) == 0, "hide prepared component");
        }
        first_outcome.struct_size = sizeof first_outcome;
        second_outcome.struct_size = sizeof second_outcome;
        check(beam_return_run(acceptance_system, &first, &first_outcome, &error) == BEAM_OK && !first, error.message);
        check(first_outcome.kind == (failure == 2 ? BEAM_OUTCOME_FAILURE :
              failure == 1 ? BEAM_OUTCOME_EXCEPTION : BEAM_OUTCOME_RETURN), "first retained outcome");
        if (failure == 2)
            check(!strcmp(first_outcome.origin, "onload_rejected"), "first retained activation failure");
        check(!first_outcome.has_cleanup_error, "first retained cleanup");
        check(beam_return_run(acceptance_system, &second, &second_outcome, &error) == BEAM_OK && !second, error.message);
        expect_return_text(&second_outcome, "ok");
        check(!second_outcome.has_cleanup_error, "second retained cleanup");
        for (int index = 0; index < 2; ++index)
            check(rename(hidden[index], paths[index]) == 0, "restore prepared component");
        check(beam_file_descriptor_count() == 0 && beam_socket_count() == 0, "retained component resource cleanup");
        check_fds(descriptors, descriptor_count);
        beam_outcome_release(&first_outcome);
        beam_outcome_release(&second_outcome);
    }
    stop_system();
    for(size_t i=0;i<2;++i)beam_library_release(&libraries[i]);
    free((void *)arguments.data);
    free((void *)modules[13].bytes.data);
    free((void *)modules[14].bytes.data);
    bytecode_input_release(&base);
    puts("components ok");
}

void pin_mode(const char *root, const char *fixture)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_return_options returning = {0};
    beam_return_boot *boot = NULL;
    beam_error error = {0};
    component_validation(root, fixture);
    check(beam_init(&system, &error) == BEAM_OK, "pin init");
    returning.struct_size = sizeof(returning);
    beam_host_options invalid={.struct_size=sizeof(invalid),.library_count=1};
    returning.host=&invalid;
    returning.entry_module = "returning_programs";
    returning.entry_function = "ret";
    check(beam_return_prepare(&returning, &boot, &error) != BEAM_OK, "bad prefix accepted");
    beam_vm_options_init(&options);

    check(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, "VM init after prepare error");
    check(beam_vm_terminate(&vm, NULL) == BEAM_OK, "VM terminate");

    check(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, "fresh VM after termination");
    check(beam_vm_terminate(&vm, NULL) == BEAM_OK, "fresh VM terminate");
    check(beam_terminate(&system) == BEAM_OK, "pin terminate");
    (void)root;
    printf("pin ok\n");
}
