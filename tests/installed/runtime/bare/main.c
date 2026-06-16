/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <beam.h>
#include <dirent.h>
#include <fenv.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "module.h"
#include "fixtures.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "bare:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

static const unsigned char empty[] = {131, 106};
static const beam_bytes no_args = {empty, sizeof empty};
static beam_system *runtime_system;
static beam_vm *vm;
static volatile sig_atomic_t signals_received;
static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
static int worker_ready, worker_done;

static void signal_handler(int sig) { (void)sig; ++signals_received; }

static void *host_worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&worker_mutex);
    worker_ready = 1;
    pthread_cond_signal(&worker_cond);
    while (!worker_done)
        pthread_cond_wait(&worker_cond, &worker_mutex);
    pthread_mutex_unlock(&worker_mutex);
    return NULL;
}

static int entries(const char *path)
{
    DIR *dir = opendir(path);
    struct dirent *entry;
    int count = 0;
    if (!dir) return -1;
    while ((entry = readdir(dir)))
        if (entry->d_name[0] != '.') ++count;
    closedir(dir);
    return count;
}

static int small_result(const beam_result *r, unsigned value)
{
    return r->kind == BEAM_RETURN && r->value.size == 3 &&
           r->value.data[0] == 131 && r->value.data[1] == 97 &&
           r->value.data[2] == value;
}

static int read_atom(const unsigned char *data, size_t size, size_t *pos,
                     const char *expected)
{
    size_t n;
    unsigned tag;
    if (*pos >= size) return 0;
    tag = data[(*pos)++];
    if (tag == 119 || tag == 115) {
        if (*pos >= size) return 0;
        n = data[(*pos)++];
    } else if (tag == 118 || tag == 100) {
        if (*pos + 2 > size) return 0;
        n = ((size_t)data[*pos] << 8) | data[*pos + 1];
        *pos += 2;
    } else return 0;
    if (n > size - *pos || n != strlen(expected) ||
        memcmp(data + *pos, expected, n)) return 0;
    *pos += n;
    return 1;
}

static int atom_result(const beam_result *r, const char *atom)
{
    size_t pos = 1;
    return r->kind == BEAM_RETURN && r->value.size > 1 &&
           r->value.data[0] == 131 &&
           read_atom(r->value.data, r->value.size, &pos, atom) &&
           pos == r->value.size;
}

static int check_exception(const char *mod, const char *fun,
                           const char *class_name, const char *reason)
{
    beam_result r = {0};
    size_t pos = 3;
    OK(beam_vm_call(vm, mod, fun, no_args, &r));
    CHECK(r.kind == BEAM_EXCEPTION && r.value.size > 3 &&
          r.value.data[0] == 131 && r.value.data[1] == 104 && r.value.data[2] == 3);
    CHECK(read_atom(r.value.data, r.value.size, &pos, class_name));
    CHECK(read_atom(r.value.data, r.value.size, &pos, reason));
    CHECK(pos < r.value.size && (r.value.data[pos] == 108 || r.value.data[pos] == 106));
    beam_result_release(&r);
    return 0;
}

static int call_atom(const char *fun, const char *atom)
{
    beam_result r = {0};
    OK(beam_vm_call(vm, "bare_fixture", fun, no_args, &r));
    CHECK(atom_result(&r, atom));
    beam_result_release(&r);
    return 0;
}

static void *wrong_create_thread(void *arg)
{
    beam_vm *other = (beam_vm *)runtime_system;
    beam_system *other_system = runtime_system;
    int *ok = arg;
    *ok = beam_vm_init(runtime_system, NULL, &other, NULL) == BEAM_WRONG_THREAD &&
          other == (beam_vm *)runtime_system &&
          beam_init(&other_system, NULL) == BEAM_WRONG_THREAD &&
          other_system == runtime_system;
    return NULL;
}

static void *wrong_thread(void *arg)
{
    beam_result result = {0};
    beam_module_info module = {sizeof(module), 0};
    beam_module_list modules = {0};
    beam_scheduler_info schedulers = {sizeof(schedulers), 0};
    int *ok = arg, status;
    *ok = beam_vm_call(vm, "bare_example", "answer", no_args, &result) == BEAM_WRONG_THREAD &&
          beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}) == BEAM_WRONG_THREAD &&
          beam_vm_get_module(vm, "bare_example", &module) == BEAM_WRONG_THREAD &&
          beam_vm_list_modules(vm, &modules) == BEAM_WRONG_THREAD &&
          beam_vm_unload(vm, "bare_example", NULL) == BEAM_WRONG_THREAD &&
          beam_vm_get_schedulers(vm, &schedulers) == BEAM_WRONG_THREAD &&
          beam_vm_set_schedulers_online(vm, 1, 1, NULL) == BEAM_WRONG_THREAD &&
          beam_vm_stop(vm, &status) == BEAM_WRONG_THREAD &&
          beam_vm_start(vm, NULL) == BEAM_WRONG_THREAD &&
          beam_vm_terminate(&vm, &status) == BEAM_WRONG_THREAD &&
          beam_terminate(&runtime_system) == BEAM_WRONG_THREAD;
    return NULL;
}

static int sigaction_same(const struct sigaction *a, const struct sigaction *b)
{
    int sig;
    if (a->sa_handler != b->sa_handler || a->sa_flags != b->sa_flags) return 0;
    for (sig = 1; sig < NSIG; ++sig)
        if (sigismember(&a->sa_mask, sig) != sigismember(&b->sa_mask, sig)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "normal";
    beam_result saved = {0}, r = {0};
    beam_vm *other = NULL;
    beam_vm_options options;
    beam_vm_options_init(&options);

    pthread_t worker, wrong;
    struct sigaction action = {0}, before[NSIG], after;
    sigset_t mask_before, mask_after;
    struct termios tty_before, tty_after;
    int have_tty = tcgetattr(STDIN_FILENO, &tty_before) == 0;
    int fd_flags[3], threads, fds, sig, code = -1, thread_ok = 0, round_before;

    action.sa_handler = signal_handler;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR2, &action, NULL) == 0);
    CHECK(sigaction(SIGFPE, &action, NULL) == 0);
    for (sig = 1; sig < NSIG; ++sig) sigaction(sig, NULL, &before[sig]);
    CHECK(pthread_sigmask(SIG_SETMASK, NULL, &mask_before) == 0);
    for (sig = 0; sig < 3; ++sig) fd_flags[sig] = fcntl(sig, F_GETFL);
    CHECK(fesetround(FE_DOWNWARD) == 0);
    round_before = fegetround();
    CHECK(setenv("BINDIR", "host-owned-sentinel", 1) == 0);

    CHECK(pthread_create(&worker, NULL, host_worker, NULL) == 0);
    pthread_mutex_lock(&worker_mutex);
    while (!worker_ready) pthread_cond_wait(&worker_cond, &worker_mutex);
    pthread_mutex_unlock(&worker_mutex);
    threads = entries("/proc/self/task");
    fds = entries("/proc/self/fd");
    CHECK(threads >= 2 && fds >= 0);

    OK(beam_init(&runtime_system, NULL));
    CHECK(beam_vm_init(runtime_system, NULL, NULL, NULL) == BEAM_INVALID_ARGUMENT);
    CHECK(pthread_create(&wrong, NULL, wrong_create_thread, &thread_ok) == 0);
    CHECK(pthread_join(wrong, NULL) == 0 && thread_ok);
    OK(beam_vm_init(runtime_system, &options, &vm, NULL));
    CHECK(fegetround() == round_before);
    CHECK(beam_vm_init(runtime_system, NULL, &other, NULL) == BEAM_ALREADY_STARTED);
    OK(beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}));
    OK(beam_vm_load(vm, (beam_bytes){bare_fixture, sizeof bare_fixture}));
    OK(beam_vm_start(vm, NULL));
    OK(beam_vm_call(vm, "bare_example", "answer", no_args, &saved));
    CHECK(small_result(&saved, 42));

    if (!strcmp(mode, "halt")) {
        const unsigned char args[] = {131, 108, 0, 0, 0, 1, 97, 17, 106};
        OK(beam_vm_call(vm, "bare_fixture", "halt_vm", (beam_bytes){args, sizeof args}, &r));
        CHECK(r.kind == BEAM_HALT && r.halt_status == 17);
        beam_result_release(&r);
    } else if (!strcmp(mode, "background") || !strcmp(mode, "background-invalid-call") ||
               !strcmp(mode, "background-invalid-load")) {
        CHECK(call_atom("background_halt", "ok") == 0);
        usleep(150000);
        if (!strcmp(mode, "background-invalid-call"))
            CHECK(beam_vm_call(vm, NULL, NULL, (beam_bytes){NULL, 0}, NULL) == BEAM_STOPPED);
        else if (!strcmp(mode, "background-invalid-load"))
            CHECK(beam_vm_load(vm, (beam_bytes){NULL, 0}) == BEAM_STOPPED);
        else
            CHECK(beam_vm_call(vm, "bare_example", "answer", no_args, &r) == BEAM_STOPPED);
        CHECK(entries("/proc/self/task") == threads);
        CHECK(entries("/proc/self/fd") == fds);
    } else {
        const unsigned char args[] = {131, 108, 0, 0, 0, 2, 97, 40, 97, 2, 106};
        const unsigned char count[] = {131, 108, 0, 0, 0, 1, 98, 0, 0, 78, 32, 106};
        const unsigned char improper[] = {131, 108, 0, 0, 0, 1, 97, 1, 97, 2};
        const unsigned char trailing[] = {131, 106, 0};
        const unsigned char malformed[] = {131, 108, 255, 255, 255, 255};
        const unsigned char compressed[] = {
            131, 80, 0, 0, 0, 1, 120, 156, 203, 2, 0, 0, 107, 0, 107
        };
        /* A tuple containing a map, binary, list, float and bignum. */
        const unsigned char term[] = {
            131,104,5,116,0,0,0,1,97,1,97,2,
            109,0,0,0,3,0,1,255,108,0,0,0,2,97,1,98,0,0,3,232,106,
            70,63,248,0,0,0,0,0,0,110,9,0,0,0,0,0,0,0,0,0,1
        };
        unsigned char echo_args[sizeof term + 6];
        unsigned i;
        memcpy(echo_args, (unsigned char[]){131,108,0,0,0,1}, 6);
        memcpy(echo_args + 6, term + 1, sizeof term - 1);
        echo_args[sizeof echo_args - 1] = 106;
        OK(beam_vm_call(vm, "bare_fixture", "echo", (beam_bytes){echo_args, sizeof echo_args}, &r));
        CHECK(r.kind == BEAM_RETURN && r.value.size == sizeof term &&
              !memcmp(r.value.data, term, sizeof term));
        beam_result_release(&r);
        OK(beam_vm_call(vm, "bare_example", "add", (beam_bytes){args, sizeof args}, &r));
        CHECK(small_result(&r, 42));
        beam_result_release(&r);
        CHECK(call_atom("caught", "caught") == 0);
        CHECK(call_atom("composition", "ok") == 0);
        CHECK(call_atom("shared_data", "ok") == 0);
        CHECK(call_atom("bad_halts", "ok") == 0);
        CHECK(call_atom("native_services", "ok") == 0);
        CHECK(call_atom("child_error", "ok") == 0);
        CHECK(check_exception("bare_fixture", "raise_error", "error", "test_error") == 0);
        CHECK(check_exception("bare_fixture", "raise_throw", "throw", "test_throw") == 0);
        CHECK(check_exception("bare_fixture", "raise_exit", "exit", "test_exit") == 0);
        CHECK(check_exception("bare_fixture", "external_exit", "exit", "killed") == 0);
        CHECK(check_exception("bare_fixture", "max_heap_exit", "exit", "killed") == 0);
        CHECK(check_exception("missing_module", "absent", "error", "undef") == 0);
        CHECK(check_exception("bare_fixture", "absent", "error", "undef") == 0);
        CHECK(beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}) == BEAM_ALREADY_LOADED);
        CHECK(beam_vm_load(vm, (beam_bytes){bare_module, 20}) == BEAM_INVALID_BEAM);
        CHECK(beam_vm_load(vm, (beam_bytes){bare_on_load, sizeof bare_on_load}) == BEAM_UNSUPPORTED);
        CHECK(beam_vm_call(vm, "bare_fixture", "echo", (beam_bytes){improper, sizeof improper}, &r) == BEAM_INVALID_TERM);
        CHECK(beam_vm_call(vm, "bare_fixture", "echo", (beam_bytes){trailing, sizeof trailing}, &r) == BEAM_INVALID_TERM);
        CHECK(beam_vm_call(vm, "bare_fixture", "echo", (beam_bytes){malformed, sizeof malformed}, &r) == BEAM_INVALID_TERM);
        CHECK(beam_vm_call(vm, "bare_fixture", "echo", (beam_bytes){compressed, sizeof compressed}, &r) == BEAM_INVALID_TERM);
        OK(beam_vm_load(vm, (beam_bytes){bare_second, sizeof bare_second}));
        OK(beam_vm_call(vm, "bare_fixture", "cross_module", no_args, &r));
        CHECK(small_result(&r, 43));
        beam_result_release(&r);
        OK(beam_vm_call(vm, "bare_fixture", "allocate", (beam_bytes){count, sizeof count}, &r));
        CHECK(r.kind == BEAM_RETURN && r.value.size == 6 && r.value.data[1] == 98 &&
              r.value.data[4] == 78 && r.value.data[5] == 32);
        beam_result_release(&r);
        for (i = 0; i < 20; ++i) {
            OK(beam_vm_call(vm, "bare_fixture", "spawn_message", no_args, &r));
            CHECK(small_result(&r, 42));
            beam_result_release(&r);
        }
        CHECK(pthread_create(&wrong, NULL, wrong_thread, &thread_ok) == 0);
        CHECK(pthread_join(wrong, NULL) == 0 && thread_ok);
        OK(beam_vm_call(vm, "bare_fixture", "leave_children", no_args, &r));
        CHECK(r.kind == BEAM_RETURN);
        beam_result_release(&r);
        CHECK(call_atom("leave_cleanup", "ok") == 0);
    }

    OK(beam_vm_stop(vm, &code));
    CHECK(code == (!strcmp(mode, "halt") ? 17 : !strncmp(mode, "background", 10) ? 19 : 0));
    OK(beam_vm_stop(vm, &code));
    CHECK(small_result(&saved, 42));
    beam_result_release(&saved);
    CHECK(beam_vm_call(vm, "bare_example", "answer", no_args, &r) == BEAM_STOPPED);
    CHECK(beam_vm_load(vm, (beam_bytes){bare_module, sizeof bare_module}) == BEAM_STOPPED);
    CHECK(beam_vm_call(vm, NULL, NULL, (beam_bytes){NULL, 0}, NULL) == BEAM_STOPPED);
    CHECK(beam_vm_load(vm, (beam_bytes){NULL, 0}) == BEAM_STOPPED);
    CHECK(beam_vm_init(runtime_system, NULL, &other, NULL) == BEAM_ALREADY_STARTED);
    OK(beam_vm_terminate(&vm, &code));
    CHECK(vm == NULL);
    OK(beam_terminate(&runtime_system));
    CHECK(runtime_system == NULL);

    for (sig = 0; sig < 100 && entries("/proc/self/task") != threads; ++sig) usleep(1000);
    CHECK(entries("/proc/self/task") == threads);
    CHECK(entries("/proc/self/fd") == fds);
    for (sig = 1; sig < NSIG; ++sig)
        if (sigaction(sig, NULL, &after) == 0) CHECK(sigaction_same(&before[sig], &after));
    CHECK(pthread_sigmask(SIG_SETMASK, NULL, &mask_after) == 0);
    for (sig = 1; sig < NSIG; ++sig)
        CHECK(sigismember(&mask_before, sig) == sigismember(&mask_after, sig));
    for (sig = 0; sig < 3; ++sig) CHECK(fcntl(sig, F_GETFL) == fd_flags[sig]);
    if (have_tty) {
        CHECK(tcgetattr(STDIN_FILENO, &tty_after) == 0);
        CHECK(tty_before.c_iflag == tty_after.c_iflag && tty_before.c_oflag == tty_after.c_oflag &&
              tty_before.c_cflag == tty_after.c_cflag && tty_before.c_lflag == tty_after.c_lflag &&
              !memcmp(tty_before.c_cc, tty_after.c_cc, sizeof tty_before.c_cc));
    }
    CHECK(fegetround() == round_before);
    CHECK(!strcmp(getenv("BINDIR"), "host-owned-sentinel"));
    CHECK(raise(SIGUSR2) == 0 && signals_received == 1);
    pthread_mutex_lock(&worker_mutex);
    worker_done = 1;
    pthread_cond_signal(&worker_cond);
    pthread_mutex_unlock(&worker_mutex);
    CHECK(pthread_join(worker, NULL) == 0);
    puts("Bare VM stopped; host continued.");
    return 0;
}
