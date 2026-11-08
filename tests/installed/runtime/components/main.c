/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include "native_counts.h"
#include <beam/boot.h>
#ifdef BEAM_TEST_STATIC_LIBRARIES
#include <beam/libraries/file.h>
#include <beam/libraries/network.h>
#include <beam/libraries/zlib.h>
#include <beam/libraries/tracing.h>
#endif
#include "bytecode_input.h"
#include "builtin_bindings.h"
#include "host_observe.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void require(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "components: %s\n", message); exit(1); }
}

static int entries(const char *path)
{
#ifdef __APPLE__
    return beam_test_host_entries(path);
#else
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;
    require(dir != NULL, "open resource inventory");
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') ++count;
    closedir(dir);
    return count;
#endif
}

static void require_worker_cleanup(int expected)
{
    int actual = entries("/proc/self/task");
    if (actual != expected) {
        struct timespec deadline, now;
        require(!clock_gettime(CLOCK_MONOTONIC, &deadline), "worker inventory deadline");
        ++deadline.tv_sec;
        /* A joined thread's kernel task entry can briefly outlive pthread_join.
         * Still require the exact OS baseline, with a bounded observation wait. */
        do {
            usleep(1000);
            actual = entries("/proc/self/task");
            require(!clock_gettime(CLOCK_MONOTONIC, &now), "worker inventory clock");
        } while (actual != expected && (now.tv_sec < deadline.tv_sec ||
                 (now.tv_sec == deadline.tv_sec && now.tv_nsec < deadline.tv_nsec)));
    }
    if (actual != expected) {
        fprintf(stderr, "components: worker inventory expected=%d actual=%d\n", expected, actual);
#ifdef __linux__
        DIR *dir = opendir("/proc/self/task");
        struct dirent *entry;
        if (dir) {
            while ((entry = readdir(dir))) {
                char path[512], line[512];
                FILE *status;
                if (entry->d_name[0] == '.') continue;
                snprintf(path, sizeof(path), "/proc/self/task/%s/stat", entry->d_name);
                status = fopen(path, "r");
                if (status) {
                    if (fgets(line, sizeof(line), status)) fprintf(stderr, "task: %s", line);
                    fclose(status);
                } else fprintf(stderr, "task %s exited during observation\n", entry->d_name);
            }
            closedir(dir);
        }
#endif
        require(0, "actual worker cleanup");
    }
}

static beam_buffer read_bytes(const char *path)
{
    FILE *file = fopen(path, "rb");
    beam_buffer bytes = {0};
    long size;
    require(file && !fseek(file, 0, SEEK_END), "open input");
    size = ftell(file);
    require(size > 0 && !fseek(file, 0, SEEK_SET), "size input");
    bytes.size = (size_t)size;
    bytes.data = malloc(bytes.size);
    require(bytes.data && fread(bytes.data, 1, bytes.size, file) == bytes.size, "read input");
    fclose(file);
    return bytes;
}

static unsigned integer(const beam_result *result)
{
    const unsigned char *p = result->value.data;
    require(result->kind == BEAM_RETURN && result->value.size >= 3 && p[0] == 131,
            "ordinary copied return");
    if (p[1] == 97 && result->value.size == 3) return p[2];
    require(p[1] == 98 && result->value.size == 6, "integer result");
    return (unsigned)p[2] << 24 | (unsigned)p[3] << 16 | (unsigned)p[4] << 8 | p[5];
}

static beam_result call(beam_vm *vm, const char *function, beam_bytes args)
{
    beam_result result = {0};
    require(beam_vm_call(vm, "component_worker", function, args, &result) == BEAM_OK,
            "call worker");
    if (result.kind != BEAM_RETURN) {
        fprintf(stderr,"component_worker:%s ETF=",function);
        for(size_t i=0;i<result.value.size && i<512;++i) fprintf(stderr,"%02x",result.value.data[i]);
        fputc('\n',stderr);
    }
    return result;
}

static void exchange(unsigned port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval timeout = {5, 0};
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port),
                                  .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    char reply[4];
    require(fd >= 0, "create client socket");
    require(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) &&
            !setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)), "socket deadline");
    require(!connect(fd, (struct sockaddr *)&address, sizeof(address)), "connect retained listener");
    require(send(fd, "ping", 4, 0) == 4 && recv(fd, reply, 4, MSG_WAITALL) == 4 &&
            !memcmp(reply, "pong", 4), "background loopback exchange");
    close(fd);
}

static void support_contracts(beam_system *system, const char *prefix, const beam_module_spec *worker)
{
    bytecode_input input={0};beam_error error={0};
    require(bytecode_input_read(&input,prefix,INPUT_BASE|INPUT_SERVICES,&error)==BEAM_OK,error.message);
    beam_module_spec modules[32];memcpy(modules,input.modules,input.count*sizeof(*modules));
    modules[input.count]=*worker;
    int descriptors=entries("/proc/self/fd"),threads=entries("/proc/self/task");
    beam_builtin_binding selected[BEAM_TEST_RETENTION_BINDING_COUNT+BEAM_TEST_RETURNING_BINDING_COUNT+2];
    size_t selected_count=0;
    if (!beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
            beam_test_retention_bindings,BEAM_TEST_RETENTION_BINDING_COUNT) ||
        !beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
            beam_test_returning_bindings,BEAM_TEST_RETURNING_BINDING_COUNT)) abort();
    for(int mode=0;mode<4;++mode){
        beam_composition_options generic={.struct_size=sizeof(generic),.modules=modules,
            .module_count=input.count+(mode!=1),
            .builtin_bindings=selected,.builtin_binding_count=selected_count};
        beam_startup_action action={"beam_output","start",{0}};
        beam_host_options plan={.struct_size=sizeof(plan),.composition=&generic,
            .startup_count=mode>=2,.startup=&action,.deferred_count=mode==1,.deferred=worker};
        beam_host *host=NULL;beam_vm *vm=NULL;beam_vm_options options;beam_vm_options_init(&options);
        require(beam_host_prepare(&plan,&host,&error)==BEAM_OK,error.message);
        options.composition=beam_host_composition(host);
        require(beam_vm_init(system,&options,&vm,&error)==BEAM_OK,error.message);
        require(entries("/proc/self/task")==threads,"passive preparation and initialization");
        if(mode==3)require(beam_module_unload(vm,"beam_startup",&error)==BEAM_OK,"remove startup controller");
        require(beam_vm_start(vm,&error)==BEAM_OK,error.message);
        const unsigned char nil[]={131,106};beam_bytes args={nil,sizeof(nil)};
        if(mode==0){beam_result result=call(vm,"passive",args);require(integer(&result)==42,"passive support");beam_result_release(&result);}
        beam_status status=beam_host_start(host,vm,&error);
        require(status==(mode==3?BEAM_SYSTEM_ERROR:BEAM_OK),"explicit startup result");
        if(mode==1||mode==2){beam_result result=call(vm,mode==1?"activated":"support_ready",args);require(integer(&result)==42,"explicit startup behavior");beam_result_release(&result);}
        if(mode==3){beam_vm_state_info state;require(error.message[0]&&beam_vm_get_state(vm,&state)==BEAM_OK&&state.state==BEAM_VM_STOPPED,"failed startup stops VM");}
        beam_host_release(&host);
        require(beam_vm_terminate(&vm,NULL)==BEAM_OK,"terminate support VM");
        require(entries("/proc/self/fd")==descriptors,"support descriptor cleanup");
        require_worker_cleanup(threads);
    }
    /* An output-only host does not require the code acquisition module. */
    size_t count=0;
    for(size_t i=0;i<input.count;++i)if(strcmp(input.modules[i].name,"beam_code"))modules[count++]=input.modules[i];
    beam_composition_options generic={.struct_size=sizeof(generic),.module_count=count,.modules=modules};
    beam_startup_action action={"beam_output","start",{0}};
    beam_host_options plan={.struct_size=sizeof(plan),.composition=&generic,.startup_count=1,.startup=&action};
    beam_host *host=NULL;require(beam_host_prepare(&plan,&host,&error)==BEAM_OK,error.message);beam_host_release(&host);
    bytecode_input_release(&input);
}

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    const beam_composition *composition = NULL;
    beam_host *host = NULL;
    beam_library *libraries[4] = {0};
    size_t library_count=0;
    beam_startup_action startup[4];
    size_t startup_count=0;
    beam_composition_options preparation = {.struct_size = sizeof(preparation)};
    beam_error error = {0};
    char paths[4][4096];
    unsigned char args[8192] = {131, 108, 0, 0, 0, 3, 97, 0, 97, 0, 109};
    unsigned char nil[] = {131, 106};
    beam_bytes empty = {nil, sizeof(nil)};
    beam_buffer code;
    beam_module_spec module;
    char origin[] = "copied:component-worker";
    beam_result saved = {0};
    int file, network, fault, fd_count, thread_count;
    size_t path_size;
    require(argc == 6, "usage: host prefix module file profile fault");
    file = strcmp(argv[4], "network") != 0;
    network = strcmp(argv[4], "file") != 0;
    fault = atoi(argv[5]);
    beam_builtin_binding selected[BEAM_TEST_NETWORK_BINDING_COUNT+BEAM_TEST_FILE_BINDING_COUNT+
        BEAM_TEST_RETENTION_BINDING_COUNT+BEAM_TEST_RETURNING_BINDING_COUNT+2];
    size_t selected_count=0;
    require(beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
        network ? beam_test_network_bindings : beam_test_file_bindings,
        network ? BEAM_TEST_NETWORK_BINDING_COUNT : BEAM_TEST_FILE_BINDING_COUNT), "support selection");
    if (network && file)
        require(beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
            beam_test_file_bindings,BEAM_TEST_FILE_BINDING_COUNT), "file selection");
    require(beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
        beam_test_retention_bindings,BEAM_TEST_RETENTION_BINDING_COUNT), "retention selection");
    require(beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,
        beam_test_returning_bindings,BEAM_TEST_RETURNING_BINDING_COUNT), "controller selection");
    preparation.builtin_bindings = selected;
    preparation.builtin_binding_count = selected_count;

    code = read_bytes(argv[2]);
    module = (beam_module_spec){"component_worker", {code.data, code.size}, origin};
    bytecode_input base = {0};
    require(bytecode_input_read(&base, argv[1], INPUT_BASE | INPUT_SERVICES, &error) == BEAM_OK, error.message);
    if (network) bytecode_input_partition_network(&base);
    beam_module_spec modules[32];
    memcpy(modules, base.modules, base.count * sizeof(*base.modules)); modules[base.count] = module;
    preparation.module_count = base.count + 1;
    preparation.modules = modules;
    const char *names[] = {"file", "network"};

    for (int i = 0; i < 2; ++i) {
        if ((i == 0 && !file) || (i == 1 && !network)) continue;
        size_t j = library_count++;
        require(snprintf(paths[j], sizeof(paths[j]), "%s/%s", argv[1], beam_test_component(names[i])) < (int)sizeof(paths[j]), "component path");
#ifdef BEAM_TEST_STATIC_LIBRARIES
        require(beam_library_from_static(i?beam_network_library():beam_file_library(),&libraries[j],&error)==BEAM_OK,error.message);
#else
        require(beam_library_open(paths[j],&libraries[j],&error)==BEAM_OK,error.message);
#endif
        startup[startup_count++]=(beam_startup_action){i?"beam_socket":"beam_file","start",{0}};
    }
    int zlib = !strcmp(argv[4], "all");
    if (zlib) {
        size_t j = library_count++;
#ifdef BEAM_TEST_STATIC_LIBRARIES
        require(beam_library_from_static(beam_zlib_library(),&libraries[j],&error)==BEAM_OK,error.message);
#else
        require(snprintf(paths[j], sizeof(paths[j]), "%s/%s", argv[1], beam_test_component("zlib")) < (int)sizeof(paths[j]), "zlib path");
        require(beam_library_open(paths[j],&libraries[j],&error)==BEAM_OK,error.message);
#endif
        startup[startup_count++]=(beam_startup_action){"zlib","on_load",{0}};
    }
    if (zlib) {
        size_t j = library_count++;
#ifdef BEAM_TEST_STATIC_LIBRARIES
        require(beam_library_from_static(beam_tracing_library(),&libraries[j],&error)==BEAM_OK,error.message);
#else
        require(snprintf(paths[j], sizeof(paths[j]), "%s/%s", argv[1], beam_test_component("tracing")) < (int)sizeof(paths[j]), "tracing path");
        require(beam_library_open(paths[j],&libraries[j],&error)==BEAM_OK,error.message);
#endif
        const beam_builtin_binding trace_bindings[] = {
            {sizeof(beam_builtin_binding),"vm_erts_internal_trace_session_create_3","beam_trace","trace_session_create",3},
            {sizeof(beam_builtin_binding),"vm_erts_internal_trace_session_destroy_1","beam_trace","trace_session_destroy",1}};
        require(beam_test_append_bindings(selected,sizeof selected/sizeof *selected,&selected_count,trace_bindings,2),"trace selection");
        preparation.builtin_binding_count = selected_count;
        preparation.default_tracer_module = "beam_trace";
        startup[startup_count++]=(beam_startup_action){"beam_trace","load_nif",{0}};
    }
    beam_host_options plan={.struct_size=sizeof(plan),.composition=&preparation,
        .library_count=library_count,.libraries=libraries,.startup_count=startup_count,.startup=startup};
    beam_library *repeated[]={libraries[0],libraries[0]};
    beam_host_options invalid=plan;invalid.libraries=repeated;invalid.library_count=2;
    require(beam_host_prepare(&invalid,&host,&error)==BEAM_INVALID_ARGUMENT&&!host,"reject duplicate library inputs");
    beam_library *missing=NULL;
    require(beam_library_open("/unavailable-beam-library.so",&missing,&error)==BEAM_RUNTIME_NOT_FOUND&&!missing,"reject missing image");
    require(beam_host_prepare(&plan,&host,&error)==BEAM_OK,error.message);
    beam_host_release(&host);
    require(beam_host_prepare(&plan,&host,&error)==BEAM_OK,error.message);
    composition=beam_host_composition(host);
    bytecode_input_release(&base);
    memset(code.data, 0, code.size); free(code.data); code = (beam_buffer){0};
    memset(origin, '?', sizeof(origin)-1);
    const beam_module_spec *inventory = NULL;
    size_t module_count = 0;
    require(beam_composition_get_modules(composition, &inventory, &module_count) == BEAM_OK && module_count > 1, "immutable module inventory");
    const beam_module_spec *copied_worker=NULL;
    for(size_t i=0;i<module_count;++i)if(!strcmp(inventory[i].name,"component_worker"))copied_worker=&inventory[i];
    require(copied_worker&&!strcmp(copied_worker->origin,"copied:component-worker")&&
        copied_worker->bytes.size>12&&!memcmp(copied_worker->bytes.data,"FOR1",4),"copied preparation inputs");
    path_size = strlen(argv[3]);
    require(path_size < sizeof(args) - 16, "file path size");
    args[7] = file; args[9] = network;
    for (int i = 0; i < 4; ++i) args[11+i] = (unsigned char)(path_size >> (24-8*i));
    memcpy(args + 15, argv[3], path_size); args[15+path_size] = 106;
    require(beam_init(&system, &error) == BEAM_OK, error.message);
    if (file && !network && !fault) support_contracts(system, argv[1], copied_worker);
    fd_count = entries("/proc/self/fd"); thread_count = entries("/proc/self/task");
    {
        beam_vm_options options;
        beam_vm *vm = NULL;
        beam_vm_state_info state;
        beam_vm_options_init(&options);
        options.composition = composition;
        require(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
        require(entries("/proc/self/task") == thread_count, "initialization starts no workers");
        require(beam_module_unload(vm, "beam_startup", &error) == BEAM_OK, "remove activation entry to exercise failure");
        require(beam_vm_start(vm, &error) == BEAM_OK, error.message);
        require(beam_host_start(host, vm, &error) == BEAM_SYSTEM_ERROR && error.message[0], "activation fails with diagnostics");
        require(beam_vm_get_state(vm, &state) == BEAM_OK && state.state == BEAM_VM_STOPPED, "activation failure stops partial VM");
        require(beam_vm_terminate(&vm, NULL) == BEAM_OK, "terminate failed activation");
        require(entries("/proc/self/fd") == fd_count, "failed activation descriptor cleanup");
        require_worker_cleanup(thread_count);
    }
    for (int pass = 0; pass < 3; ++pass) {
        beam_vm_options options;
        beam_vm *vm = NULL;
        beam_vm_options_init(&options);

        options.composition = composition;
        require(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
        beam_module_info loaded = {.struct_size = sizeof(loaded)};
        require(beam_module_get_info(vm, "component_worker", &loaded) == BEAM_OK && (loaded.flags & BEAM_MODULE_CURRENT), "composition stages application code");
        require(beam_vm_start(vm, &error) == BEAM_OK, error.message);
        require(beam_host_start(host, vm, &error) == BEAM_OK, error.message);
        if(pass==2)beam_host_release(&host);
        if (zlib) {
            beam_result traced = call(vm, "tracing", empty);
            require(integer(&traced)==42,"explicit tracing library activation");
            beam_result_release(&traced);
            beam_result compressed = call(vm, "compress", empty);
            require(integer(&compressed)==42 && beam_zlib_stream_count()>0,"combined compression and retained stream");
            beam_result_release(&compressed);
        }
        beam_result result = call(vm, "start", (beam_bytes){args, path_size+16});
        unsigned port = integer(&result);
        beam_result_release(&result);
        for (int round = 1; round <= 2; ++round) {
            result = call(vm, "arm", empty);
            require(integer(&result) == (unsigned)round, "retained worker state");
            if (!pass && round == 1) saved = result;
            else beam_result_release(&result);
            if (network) exchange(port);
            result = call(vm, "count", empty);
            require(integer(&result) == (unsigned)round, "separate call after background exchange");
            beam_result_release(&result);
        }
        if (file) {
            beam_buffer contents = read_bytes(argv[3]);
            require(contents.size == 18 && !memcmp(contents.data, "retained\nretained\n", 18), "observe actual file writes");
            free(contents.data);
            require(beam_file_descriptor_count() > 0, "live file retained");
        }
        if (network) require(beam_socket_count() > 0, "live listener retained");
        if (fault) {
            result = call(vm, file ? "fail_file" : "fail_socket", empty);
            require(result.kind == BEAM_HALT && result.halt_status == 1, "actual helper failure stops VM");
            beam_result_release(&result);
            beam_vm_report report={.struct_size=sizeof(report)};
            require(beam_vm_get_report(vm,&report)==BEAM_OK&&report.cause==BEAM_TERMINAL_FAILURE&&report.failure.message[0]&&!strcmp(report.origin,file?"file":"socket"),"retained owner failure");
        }
        require(beam_vm_terminate(&vm, NULL) == BEAM_OK && !vm, "terminate live owners");
        require(beam_file_descriptor_count() == 0 && beam_socket_count() == 0 && beam_zlib_stream_count() == 0, "native resource finalization");
        require(entries("/proc/self/fd") == fd_count, "actual descriptor cleanup");
        require_worker_cleanup(thread_count);
        require(integer(&saved) == 1, "copied result survives fresh VMs");
    }
    for(size_t i=0;i<library_count;++i)beam_library_release(&libraries[i]);
    beam_result_release(&saved);
    free(code.data);
    require(beam_terminate(&system) == BEAM_OK, "terminate system");
    puts("component lifetime checks passed");
    return 0;
}
