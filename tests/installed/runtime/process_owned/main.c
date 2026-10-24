/* SPDX-License-Identifier: Apache-2.0 */
#include <beam.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include "host_observe.h"
#endif

static int selected_break = -1;
static int alter_terminal;
static unsigned ready_calls;
static struct termios running_terminal;
static int running_flags[2];

static void check(int test, const char *message)
{
    if (!test) { fprintf(stderr, "process-owned: %s\n", message); exit(1); }
}

static unsigned threads(void)
{
#ifdef __APPLE__
    int count=beam_test_host_threads(getpid()); check(count>0,"thread inventory"); return (unsigned)count;
#else
    DIR *directory = opendir("/proc/self/task");
    struct dirent *entry;
    unsigned count = 0;
    check(directory != NULL, "thread inventory");
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count;
#endif
}

static int stream_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL);
#ifdef __APPLE__
    /* XNU's FWASWRITTEN is descriptor history, not a mutable stream option. */
    if (flags >= 0) flags &= ~0x00010000;
#endif
    return flags;
}

static beam_status ready(void *context, beam_bytes request, beam_host_reply *reply)
{
    static const unsigned char response[] = {131,119,2,'o','k'};
    static const char name[] = "process_root_ready";
    size_t prefix = request.size >= 3 && request.data[1] == 119 ? 3 : 4;
    (void)context;
    if (selected_break >= 0 && !ready_calls) {
        struct sigaction action;
        check(sigaction(SIGINT, NULL, &action) == 0, "inspect interrupt disposition");
        if (selected_break == BEAM_BREAK_IGNORE)
            check(action.sa_handler == SIG_IGN, "interrupts are ignored");
        else if (selected_break == BEAM_BREAK_DISABLED)
            check(action.sa_handler == SIG_DFL, "break handler stays disabled");
        else
            check(action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN,
                  "break handler is installed");
        if (selected_break == BEAM_BREAK_INPUT) {
            struct termios terminal;
            check(isatty(0) && tcgetattr(0, &terminal) == 0 && terminal.c_cc[VINTR] == 0,
                  "interrupt character becomes terminal input");
        }
    }
    check(request.size == prefix + sizeof(name) - 1 && request.data[0] == 131 &&
          (request.data[1] == 119 || request.data[1] == 118) &&
          request.data[prefix-1] == sizeof(name)-1 &&
          (prefix == 3 || request.data[2] == 0) &&
          !memcmp(request.data+prefix, name, sizeof(name)-1), "root callback request");
    if (alter_terminal) {
        struct termios current;
        check(tcgetattr(0, &current) == 0, "inspect running terminal");
        if (!ready_calls) {
            current.c_lflag &= ~(ICANON | ECHO);
            current.c_cc[VMIN] = 1;
            current.c_cc[VTIME] = 0;
            check(tcsetattr(0, TCSANOW, &current) == 0, "change running terminal");
            check(tcgetattr(0, &running_terminal) == 0, "capture changed terminal");
            int flags = fcntl(0, F_GETFL);
            check(flags >= 0 && fcntl(0, F_SETFL, flags ^ O_NONBLOCK) == 0,
                  "change running stream flags");
            for (int fd = 0; fd < 2; ++fd) {
                running_flags[fd] = stream_flags(fd);
                check(running_flags[fd] >= 0, "capture running stream flags");
            }
        } else {
            check(current.c_iflag == running_terminal.c_iflag &&
                  current.c_oflag == running_terminal.c_oflag &&
                  current.c_cflag == running_terminal.c_cflag &&
                  current.c_lflag == running_terminal.c_lflag &&
                  !memcmp(current.c_cc, running_terminal.c_cc, sizeof(current.c_cc)),
                  "continue restores running terminal mode");
            for (int fd = 0; fd < 2; ++fd)
                check(stream_flags(fd) == running_flags[fd],
                      "continue restores running stream flags");
        }
    }
    ++ready_calls;
    puts(name);
    fflush(stdout);
    return beam_host_reply_copy(reply, (beam_bytes){response, sizeof(response)});
}

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error;
    beam_composition *composition = NULL;
    unsigned char context[] = {131,108,0,0,0,1,97,42,106};
    unsigned char arguments[] = {131,108,0,0,0,2,109,0,0,0,14,
        'c','o','p','i','e','d',' ','r','o','o','t',' ',0xce,0xbb,97,0,106};
    char module[] = "process_root", function[16] = "start";
    beam_entry root = {module, function, {arguments, sizeof(arguments)}};
    FILE *file;
    unsigned char *bytes;
    long size;
    check(argc == 3 || argc == 4, "usage: process_owned module.beam status [break-policy]");
    if (argc == 4) selected_break = atoi(argv[3]);
    int requested = atoi(argv[2]);
    alter_terminal = requested == 250 || requested == 251;
    arguments[26] = (unsigned char)requested;
    if (requested == 254) strcpy(function, "missing");
    if (requested == 252) arguments[27] = 97; /* Truncated/improper argument tail. */
    file = fopen(argv[1], "rb");
    check(file && !fseek(file, 0, SEEK_END), "open root bytecode");
    size = ftell(file);
    check(size > 0 && !fseek(file, 0, SEEK_SET), "root bytecode size");
    bytes = malloc((size_t)size);
    check(bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size, "read root bytecode");
    fclose(file);
    check(beam_init(&system, &error) == BEAM_OK, error.message);
    unsigned before = threads();
    beam_vm_options_init(&options);
    options.initial_root = &root;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "host-controlled execution rejects a supplied root");
    options.initial_root = NULL;
    beam_module_spec preload = {"process_root", {bytes, (size_t)size}, "copied:process-root"};
    beam_composition_options spec = {.struct_size = sizeof(spec),
        .preload_count = 1, .preloads = &preload,
        .native_admission = BEAM_NATIVE_NIFS | BEAM_NATIVE_DRIVERS | BEAM_NATIVE_PORTS | BEAM_NATIVE_DISTRIBUTION};
    check(beam_composition_prepare(&spec, &composition, &error) == BEAM_OK, error.message);
    options.composition = composition;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "host-controlled VM rejects process-lifetime admission");
    if (requested != 0) {
        beam_composition_release(&composition);
        spec.native_admission = 0;
        check(beam_composition_prepare(&spec, &composition, &error) == BEAM_OK, error.message);
    }
    options.composition = NULL;
    options.process_policy = BEAM_PROCESS_SIGNALS;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "host-controlled VM rejects process policy");
    options.process_policy = 0;
    options.break_policy = BEAM_BREAK_IGNORE;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "break policy requires signal ownership");
    options.break_policy = BEAM_BREAK_DEFAULT;
    options.startup_context = (beam_bytes){context, sizeof(context) - 1};
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_TERM && !vm,
          "truncated context is rejected before execution");
    unsigned char compressed[] = {131,80,0,0,0,0};
    options.startup_context = (beam_bytes){compressed, sizeof(compressed)};
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_TERM && !vm,
          "compressed context is rejected");
    options.startup_context = (beam_bytes){context, sizeof(context)};
    options.initial_root = &root;
    options.execution = BEAM_PROCESS_OWNED;
    options.composition = composition;
    options.process_policy = BEAM_PROCESS_SIGNALS;
    options.break_policy = BEAM_BREAK_INPUT;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "terminal input policy requires stdio ownership");
    options.break_policy = (beam_break_policy)99;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_INVALID_ARGUMENT && !vm,
          "invalid break policy is rejected");
    options.process_policy = selected_break < 0 ? 0 : BEAM_PROCESS_SIGNALS | BEAM_PROCESS_STDIO;
    options.break_policy = selected_break < 0 ? BEAM_BREAK_DEFAULT : (beam_break_policy)selected_break;
    check(beam_vm_init(system, &options, &vm, &error) == BEAM_OK, error.message);
    if (selected_break >= 0) {
        struct sigaction action;
        check(sigaction(SIGINT, NULL, &action) == 0 && action.sa_handler == SIG_DFL,
              "initialization preserves interrupt disposition");
    }
    options.break_policy = BEAM_BREAK_DEFAULT;
    beam_composition_release(&composition);
    memset(context, 0, sizeof(context));
    memset(arguments, 0, sizeof(arguments));
    memset(module, 0, sizeof(module)); memset(function, 0, sizeof(function));
    check(threads() == before, "initialization starts no workers");
    check(beam_vm_load(vm, (beam_bytes){bytes, (size_t)size}) == BEAM_ALREADY_LOADED,
          "composition staged the root before execution");
    free(bytes);
    check(beam_vm_set_host_handler(vm, ready, NULL) == BEAM_OK, "register root callback");
    check(beam_vm_start(vm, &error) == BEAM_INVALID_STATE, "explicit process transfer required");
    if (requested == 253) {
        check(beam_vm_terminate(&vm, NULL) == BEAM_OK, "cancel process-owned preparation");
        check(threads() == before && beam_terminate(&system) == BEAM_OK, "cancelled root cleanup");
        puts("process_root_cancelled");
        return 0;
    }
    beam_status status = beam_vm_enter(vm, &error);
    if (requested == 252 || requested == 254) {
        beam_vm_state_info state;
        check(status == (requested == 252 ? BEAM_INVALID_TERM : BEAM_NOT_FOUND), "root validation status");
        check(beam_vm_get_state(vm, &state) == BEAM_OK && state.state == BEAM_VM_INITIALIZED,
              "rejected root remains initialized");
        check(threads() == before, "rejected root starts no workers");
        check(beam_vm_terminate(&vm, NULL) == BEAM_OK && beam_terminate(&system) == BEAM_OK,
              "rejected root cleanup");
        puts("process_root_rejected");
        return 0;
    }
    fprintf(stderr, "process-owned entry returned: %s: %s\n", beam_status_message(status), error.message);
    return 1;
}
