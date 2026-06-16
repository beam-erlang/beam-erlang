/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include "bytecode_input.h"
#include "host_observe.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
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
    bytecode_input input = {0};
    beam_error error = {0};
    require(bytecode_input_read(&input, prefix, INPUT_BASE | INPUT_SERVICES, &error) == BEAM_OK, error.message);
    beam_module_spec modules[13];
    memcpy(modules, input.modules, input.count * sizeof(*modules));
    modules[input.count] = *worker;
    beam_composition_options prep = {.struct_size = sizeof(prep), .modules = modules,
        .module_count = input.count + 1, .services = BEAM_SERVICE_OUTPUT};
    beam_composition *composition = NULL;
    for (size_t missing = 7; missing < input.count; ++missing) {
        beam_module_spec incomplete[12];
        memcpy(incomplete, modules, missing * sizeof(*modules));
        memcpy(incomplete + missing, modules + missing + 1, (input.count - missing) * sizeof(*modules));
        prep.modules = incomplete; prep.module_count = input.count;
        require(beam_composition_prepare(&prep, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition,
                "service selection requires every ordinary support module");
        prep.services = 0; prep.activation_count = 1; prep.activation_modules = &modules[missing];
        require(beam_composition_prepare(&prep, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition,
                "support cannot activate itself");
        prep.services = BEAM_SERVICE_OUTPUT; prep.activation_count = 0;
    }
    prep.modules = modules; prep.module_count = input.count + 2;
    modules[input.count + 1] = modules[7];
    require(beam_composition_prepare(&prep, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition,
            "duplicate support rejected");
    int descriptors = entries("/proc/self/fd"), threads = entries("/proc/self/task");
    for (int mode = 0; mode < 4; ++mode) {
        beam_vm *vm = NULL;
        beam_vm_options options; beam_vm_options_init(&options);
        beam_activation_info info = {.struct_size = sizeof(info)};
        prep.module_count = input.count + (mode != 1);
        prep.services = mode >= 2 ? BEAM_SERVICE_OUTPUT : 0;
        prep.activation_count = mode == 1; prep.activation_modules = worker;
        prep.defer_modules = mode >= 2;
        require(beam_composition_prepare(&prep, &composition, &error) == BEAM_OK, error.message);
        options.composition = composition;
        require(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
        beam_composition_release(&composition);
        require(beam_vm_get_activation(vm, &info) == BEAM_OK &&
                info.state == (mode ? BEAM_ACTIVATION_PREPARED : BEAM_ACTIVATION_ABSENT), "initial activation state");
        beam_activation_info invalid = {.struct_size = sizeof(invalid) - 1, .state = BEAM_ACTIVATION_ACTIVE};
        require(beam_vm_get_activation(vm, &invalid) == BEAM_INVALID_ARGUMENT &&
                invalid.state == BEAM_ACTIVATION_ACTIVE, "invalid activation query preserves output");
        require(entries("/proc/self/task") == threads, "activation observation creates no workers");
        if (mode == 2)
            for (size_t i = 0; i < prep.module_count; ++i)
                require(beam_vm_load(vm, modules[i].bytes) == BEAM_OK, "explicit deferred support loading");
        require(beam_vm_start(vm, &error) == BEAM_OK, error.message);
        beam_result result = {0};
        const unsigned char nil[] = {131, 106};
        beam_bytes args = {nil, sizeof(nil)};
        if (mode) {
            require(beam_vm_call(vm, "component_worker", "activated", args, &result) == BEAM_INVALID_STATE,
                    "selected activation closes entry admission");
        } else {
            result = call(vm, "passive", args);
            require(integer(&result) == 42, "support availability is passive");
            beam_result_release(&result);
        }
        beam_status status = beam_vm_activate(vm, &error);
        require(status == (mode == 3 ? BEAM_SYSTEM_ERROR : BEAM_OK), "activation result");
        require(beam_vm_get_activation(vm, &info) == BEAM_OK &&
                info.state == (mode == 3 ? BEAM_ACTIVATION_FAILED : mode ? BEAM_ACTIVATION_ACTIVE : BEAM_ACTIVATION_ABSENT),
                "activation state after dispatch");
        if (mode != 3) {
            result = call(vm, mode == 0 ? "passive" : mode == 1 ? "activated" : "support_ready", args);
            require(integer(&result) == 42, "support execution result");
            beam_result_release(&result);
        } else {
            require(info.failure.message[0], "failed activation diagnostic retained");
            beam_vm_report report = {.struct_size = sizeof(report)};
            require(beam_vm_get_report(vm, &report) == BEAM_OK &&
                    !strcmp(report.component, "beam_component_services"), "missing support identity retained");
        }
        require(beam_vm_stop(vm, NULL) == BEAM_OK, "stop support VM");
        require(beam_vm_get_activation(vm, &info) == BEAM_OK &&
                info.state == (mode == 3 ? BEAM_ACTIVATION_FAILED : mode ? BEAM_ACTIVATION_STOPPED : BEAM_ACTIVATION_ABSENT),
                "stopped activation state");
        require(beam_vm_terminate(&vm, NULL) == BEAM_OK, "terminate support VM");
        require(entries("/proc/self/fd") == descriptors && entries("/proc/self/task") == threads, "support lifetime cleanup");
    }
    bytecode_input_release(&input);
}

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_composition *composition = NULL;
    beam_component_spec components[3];
    beam_composition_options preparation = {.struct_size = sizeof(preparation)};
    beam_error error = {0};
    char paths[3][4096];
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
    code = read_bytes(argv[2]);
    module = (beam_module_spec){"component_worker", {code.data, code.size}, origin};
    bytecode_input base = {0};
    require(bytecode_input_read(&base, argv[1], INPUT_BASE | INPUT_SERVICES, &error) == BEAM_OK, error.message);
    beam_module_spec modules[14];
    memcpy(modules, base.modules, base.count * sizeof(*base.modules)); modules[base.count] = module;
    preparation.module_count = base.count + 1;
    preparation.modules = modules;
    const char *names[] = {"file", "network"};
    beam_component_kind kinds[] = {BEAM_COMPONENT_FILE, BEAM_COMPONENT_NETWORK};
    for (int i = 0; i < 2; ++i) {
        if ((i == 0 && !file) || (i == 1 && !network)) continue;
        size_t j = preparation.component_count++;
        require(snprintf(paths[j], sizeof(paths[j]), "%s/%s", argv[1], beam_test_component(names[i])) < (int)sizeof(paths[j]), "component path");
        components[j] = (beam_component_spec){sizeof(components[j]), kinds[i], paths[j]};
    }
    preparation.components = components;
    /* Invalid selections must fail without acquiring a VM or output handle. */
    beam_composition_options invalid = preparation;
    beam_component_spec repeated[] = {components[0], components[0]};
    invalid.components = repeated; invalid.component_count = 2;
    require(beam_composition_prepare(&invalid, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition, "reject duplicate component");
    repeated[0] = components[0];
    invalid.component_count = 1;
    invalid.modules = &module; invalid.module_count = 1;
    require(beam_composition_prepare(&invalid, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition, "reject missing activation dependency");
    repeated[0].path = "/unavailable-beam-component.so";
    require(beam_composition_prepare(&invalid, &composition, &error) == BEAM_RUNTIME_NOT_FOUND && !composition, "reject missing mapping");
    require(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK, error.message);
    beam_composition_release(&composition); /* cancellation initializes no VM */
    beam_module_spec conflicting[] = {module, module};
    invalid = preparation; invalid.modules = conflicting; invalid.module_count = 2;
    require(beam_composition_prepare(&invalid, &composition, &error) == BEAM_INVALID_ARGUMENT && !composition, "reject conflicting module names");
    conflicting[0].name = "wrong_name"; invalid.module_count = 1;
    require(beam_composition_prepare(&invalid, &composition, &error) == BEAM_INVALID_BEAM && !composition, "reject wrong module identity");
    require(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK, error.message);
    bytecode_input_release(&base);
    memset(code.data, 0, code.size); free(code.data); code = (beam_buffer){0};
    memset(origin, '?', sizeof(origin)-1);
    const beam_module_spec *inventory = NULL;
    size_t module_count = 0;
    require(beam_composition_get_modules(composition, &inventory, &module_count) == BEAM_OK && module_count > 1, "immutable module inventory");
    require(!strcmp(inventory[module_count-1].name, "component_worker") &&
            !strcmp(inventory[module_count-1].origin, "copied:component-worker") &&
            inventory[module_count-1].bytes.size > 12 &&
            !memcmp(inventory[module_count-1].bytes.data, "FOR1", 4), "copied preparation inputs");
    path_size = strlen(argv[3]);
    require(path_size < sizeof(args) - 16, "file path size");
    args[7] = file; args[9] = network;
    for (int i = 0; i < 4; ++i) args[11+i] = (unsigned char)(path_size >> (24-8*i));
    memcpy(args + 15, argv[3], path_size); args[15+path_size] = 106;
    require(beam_init(&system, &error) == BEAM_OK, error.message);
    if (file && !network && !fault) support_contracts(system, argv[1], &inventory[module_count-1]);
    fd_count = entries("/proc/self/fd"); thread_count = entries("/proc/self/task");
    {
        beam_vm_options options;
        beam_vm *vm = NULL;
        beam_vm_state_info state;
        beam_activation_info info = {.struct_size = sizeof(info)};
        beam_vm_options_init(&options);
        options.composition = composition;
        require(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
        require(entries("/proc/self/task") == thread_count, "initialization starts no workers");
        require(beam_vm_unload(vm, "beam_component_services", &error) == BEAM_OK, "remove activation entry to exercise failure");
        require(beam_vm_start(vm, &error) == BEAM_OK, error.message);
        require(beam_vm_activate(vm, &error) == BEAM_SYSTEM_ERROR && error.message[0], "activation fails with diagnostics");
        require(beam_vm_get_state(vm, &state) == BEAM_OK && state.state == BEAM_VM_STOPPED, "activation failure stops partial VM");
        require(beam_vm_get_activation(vm, &info) == BEAM_OK && info.state == BEAM_ACTIVATION_FAILED, "activation failure inspection");
        require(beam_vm_terminate(&vm, NULL) == BEAM_OK, "terminate failed activation");
        require(entries("/proc/self/fd") == fd_count && entries("/proc/self/task") == thread_count, "failed activation cleanup");
    }
    for (int pass = 0; pass < 3; ++pass) {
        beam_vm_options options;
        beam_vm *vm = NULL;
        beam_component_info info = {.struct_size = sizeof(info)};
        beam_vm_options_init(&options);

        options.composition = composition;
        require(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
        if (pass == 2) beam_composition_release(&composition); /* VM retains mappings */
        require(beam_vm_get_component(vm, file ? BEAM_COMPONENT_FILE : BEAM_COMPONENT_NETWORK, &info) == BEAM_OK && info.state == BEAM_COMPONENT_PREPARED, "prepared state");
        beam_module_info loaded = {.struct_size = sizeof(loaded)};
        require(beam_vm_get_module(vm, "component_worker", &loaded) == BEAM_OK && (loaded.flags & BEAM_MODULE_CURRENT), "composition stages application code");
        require(beam_vm_start(vm, &error) == BEAM_OK, error.message);
        beam_result rejected = {0};
        require(beam_vm_call(vm, "component_worker", "count", empty, &rejected) == BEAM_INVALID_STATE, "closed admission before activation");
        require(beam_vm_activate(vm, &error) == BEAM_OK, error.message);
        require(beam_vm_get_component(vm, file ? BEAM_COMPONENT_FILE : BEAM_COMPONENT_NETWORK, &info) == BEAM_OK && info.state == BEAM_COMPONENT_ACTIVE, "active state");
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
            require(beam_vm_get_component(vm, file ? BEAM_COMPONENT_FILE : BEAM_COMPONENT_NETWORK, &info) == BEAM_OK && info.state == BEAM_COMPONENT_FAILED && info.failure.message[0], "retained failure diagnostics");
        }
        require(beam_vm_terminate(&vm, NULL) == BEAM_OK && !vm, "terminate live owners");
        require(beam_file_descriptor_count() == 0 && beam_socket_count() == 0, "native resource finalization");
        require(entries("/proc/self/fd") == fd_count, "actual descriptor cleanup");
        require(entries("/proc/self/task") == thread_count, "actual worker cleanup");
        require(integer(&saved) == 1, "copied result survives fresh VMs");
    }
    beam_result_release(&saved);
    free(code.data);
    require(beam_terminate(&system) == BEAM_OK, "terminate system");
    puts("component lifetime checks passed");
    return 0;
}
