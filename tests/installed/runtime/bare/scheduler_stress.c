/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <beam.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"

/* Native acceptance instrumentation, not SDK interfaces. Blocking scheduler 2
 * lets the test establish pending admission without relying on timing. */
extern void erts_schedule_misc_aux_work(int, void (*)(void *), void *);
extern void erts_bare_get_schedulers(beam_scheduler_info *);
extern beam_status erts_bare_set_schedulers_online(unsigned, unsigned,
                                      void (*)(void *, beam_status), void *);
extern void erts_bare_halt(int);

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "scheduler_stress:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

static const unsigned char empty_data[] = {131, 106};
static const beam_bytes empty = {empty_data, sizeof empty_data};

static double seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

typedef struct scheduler_probe {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned entered;
    unsigned released;
    unsigned exited;
    unsigned admitted;
    unsigned completed;
    unsigned halted;
    beam_status admission_status;
    beam_status completion_status;
} scheduler_probe;

static void require_sync(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "scheduler synchronization: %s\n", message);
        exit(1);
    }
}

/* The test mutex is held by the caller. A deadline bounds failures; every
 * successful wait is decided by an explicit native callback handshake. */
static void wait_locked(scheduler_probe *probe, unsigned *value, unsigned target)
{
    struct timespec deadline;
    require_sync(clock_gettime(CLOCK_REALTIME, &deadline) == 0, "clock");
    deadline.tv_sec += 5;
    while (*value < target) {
        int result = pthread_cond_timedwait(&probe->condition, &probe->mutex,
                                            &deadline);
        require_sync(result == 0, result == ETIMEDOUT ? "handshake timed out" :
                                                      "condition wait");
    }
}

static void block_scheduler(void *argument)
{
    scheduler_probe *probe = argument;
    unsigned ticket;
    pthread_mutex_lock(&probe->mutex);
    ticket = ++probe->entered;
    pthread_cond_broadcast(&probe->condition);
    wait_locked(probe, &probe->released, ticket);
    ++probe->exited;
    pthread_cond_broadcast(&probe->condition);
    pthread_mutex_unlock(&probe->mutex);
}

static void native_completed(void *argument, beam_status status)
{
    scheduler_probe *probe = argument;
    pthread_mutex_lock(&probe->mutex);
    require_sync(++probe->completed == 1, "duplicate native completion");
    probe->completion_status = status;
    pthread_cond_broadcast(&probe->condition);
    pthread_mutex_unlock(&probe->mutex);
}

static void admit_native_change(void *argument)
{
    scheduler_probe *probe = argument;
    beam_status status = erts_bare_set_schedulers_online(1, 1,
                                                        native_completed, probe);
    pthread_mutex_lock(&probe->mutex);
    probe->admission_status = status;
    ++probe->admitted;
    pthread_cond_broadcast(&probe->condition);
    pthread_mutex_unlock(&probe->mutex);
}

static void halt_pending_change(void *argument)
{
    scheduler_probe *probe = argument;
    /* Halt is delivered on scheduler 1 while the owner-thread request still
     * awaits scheduler 2. Releasing scheduler 2 then permits shutdown/join. */
    erts_bare_halt(37);
    pthread_mutex_lock(&probe->mutex);
    require_sync(++probe->halted == 1, "duplicate native halt");
    probe->released = 2;
    pthread_cond_broadcast(&probe->condition);
    pthread_mutex_unlock(&probe->mutex);
}

static void *halt_after_admission(void *argument)
{
    scheduler_probe *probe = argument;
    double deadline = seconds() + 5;
    beam_scheduler_info info = {.struct_size = sizeof info};
    for (;;) {
        struct timespec delay = {0, 1000000};
        /* This private locked snapshot is the only VM observation on this
         * helper. Public C operations remain on the owner thread. */
        erts_bare_get_schedulers(&info);
        if (info.schedulers_online == 1 && info.dirty_cpu_schedulers_online == 1)
            break;
        require_sync(seconds() < deadline, "public change was not admitted");
        nanosleep(&delay, NULL);
    }
    pthread_mutex_lock(&probe->mutex);
    require_sync(probe->entered == 2 && probe->exited == 1,
                 "admitted change did not retain blocked scheduler");
    pthread_mutex_unlock(&probe->mutex);
    erts_schedule_misc_aux_work(1, halt_pending_change, probe);
    return NULL;
}

static int deterministic_controls(beam_system *system, scheduler_probe *probe)
{
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_scheduler_info info = {.struct_size = sizeof info};
    beam_vm_state_info state;
    beam_error error;
    pthread_t helper;
    int halt = -1;

    beam_vm_options_init(&options);


    options.schedulers = options.schedulers_online = 2;
    options.dirty_cpu_schedulers = options.dirty_cpu_schedulers_online = 2;
    OK(beam_vm_init(system, &options, &vm, &error));
    OK(beam_vm_start(vm, &error));

    erts_schedule_misc_aux_work(2, block_scheduler, probe);
    pthread_mutex_lock(&probe->mutex);
    wait_locked(probe, &probe->entered, 1);
    pthread_mutex_unlock(&probe->mutex);
    erts_schedule_misc_aux_work(1, admit_native_change, probe);
    pthread_mutex_lock(&probe->mutex);
    wait_locked(probe, &probe->admitted, 1);
    CHECK(probe->admission_status == BEAM_OK);
    CHECK(probe->completed == 0 && probe->exited == 0);
    pthread_mutex_unlock(&probe->mutex);
    CHECK(beam_vm_set_schedulers_online(vm, 2, 2, &error) == BEAM_BUSY);
    OK(beam_vm_get_schedulers(vm, &info));
    CHECK(info.schedulers_online == 1 && info.dirty_cpu_schedulers_online == 1);
    pthread_mutex_lock(&probe->mutex);
    CHECK(probe->completed == 0);
    probe->released = 1;
    pthread_cond_broadcast(&probe->condition);
    wait_locked(probe, &probe->completed, 1);
    CHECK(probe->completion_status == BEAM_OK && probe->exited == 1);
    pthread_mutex_unlock(&probe->mutex);
    OK(beam_vm_set_schedulers_online(vm, 2, 2, &error));

    erts_schedule_misc_aux_work(2, block_scheduler, probe);
    pthread_mutex_lock(&probe->mutex);
    wait_locked(probe, &probe->entered, 2);
    pthread_mutex_unlock(&probe->mutex);
    CHECK(pthread_create(&helper, NULL, halt_after_admission, probe) == 0);
    CHECK(beam_vm_set_schedulers_online(vm, 1, 1, &error) == BEAM_STOPPED);
    CHECK(pthread_join(helper, NULL) == 0);
    OK(beam_vm_get_state(vm, &state));
    CHECK(state.state == BEAM_VM_STOPPED && state.halt_status == 37);
    pthread_mutex_lock(&probe->mutex);
    CHECK(probe->halted == 1 && probe->exited == 2 && probe->completed == 1);
    pthread_mutex_unlock(&probe->mutex);
    OK(beam_vm_terminate(&vm, &halt));
    CHECK(halt == 37);
    return 0;
}

static int call_ok(beam_vm *vm, const char *function)
{
    beam_result result = {0};
    int is_ok;
    OK(beam_vm_call(vm, "bare_scheduler_stress", function, empty, &result));
    is_ok = result.kind == BEAM_RETURN &&
        ((result.value.size == 5 && result.value.data[0] == 131 &&
          (result.value.data[1] == 119 || result.value.data[1] == 115) &&
          result.value.data[2] == 2 &&
          !memcmp(result.value.data + 3, "ok", 2)) ||
         (result.value.size == 6 && result.value.data[0] == 131 &&
          (result.value.data[1] == 118 || result.value.data[1] == 100) &&
          result.value.data[2] == 0 && result.value.data[3] == 2 &&
          !memcmp(result.value.data + 4, "ok", 2)));
    if (!is_ok)
        fprintf(stderr, "bare_scheduler_stress:%s: outcome %d\n",
                function, result.kind);
    beam_result_release(&result);
    CHECK(is_ok);
    return 0;
}

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error;
    scheduler_probe probe = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .condition = PTHREAD_COND_INITIALIZER
    };

    OK(beam_init(&system, &error));
    CHECK(!deterministic_controls(system, &probe));
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
        beam_scheduler_info info = {.struct_size = sizeof info};
        beam_status status;
        double deadline;

        beam_vm_options_init(&options);


        options.schedulers = 8;
        options.schedulers_online = 4;
        options.dirty_cpu_schedulers = 4;
        options.dirty_cpu_schedulers_online = 1;
        OK(beam_vm_init(system, &options, &vm, &error));
        OK(beam_vm_load(vm, (beam_bytes){bare_scheduler_stress,
                                        sizeof bare_scheduler_stress}));
        OK(beam_vm_start(vm, &error));
        CHECK(!call_ok(vm, "start"));

        /* Independent normal and dirty setters race the host's exact pair.
         * Busy is expected, but the selected pair must always be valid. */
        for (unsigned attempt = 0; attempt < 300; ++attempt) {
            status = beam_vm_set_schedulers_online(vm,
                        attempt % 2 ? 4 : 8, attempt % 2 ? 3 : 1, &error);
            CHECK(status == BEAM_OK || status == BEAM_BUSY);
            OK(beam_vm_get_schedulers(vm, &info));
            CHECK(info.schedulers == 8 && info.dirty_cpu_schedulers == 4);
            CHECK(info.schedulers_online >= 4 && info.schedulers_online <= 8);
            CHECK(info.dirty_cpu_schedulers_online >= 1 &&
                  info.dirty_cpu_schedulers_online <= 4 &&
                  info.dirty_cpu_schedulers_online <= info.schedulers_online);
        }

        CHECK(!call_ok(vm, "stop"));
        deadline = seconds() + 5;
        do {
            struct timespec delay = {0, 1000000};
            status = beam_vm_set_schedulers_online(vm, 4, 3, &error);
            if (status != BEAM_BUSY)
                break;
            CHECK(seconds() < deadline);
            nanosleep(&delay, NULL);
        } while (1);
        CHECK(status == BEAM_OK);
        CHECK(!call_ok(vm, "check"));
        OK(beam_vm_terminate(&vm, NULL));
    }
    OK(beam_terminate(&system));
    /* Retain native callback storage across the later lifetimes so a stale
     * request is observable, rather than handing it already-freed storage. */
    CHECK(probe.entered == 2 && probe.exited == 2 && probe.admitted == 1 &&
          probe.completed == 1 && probe.halted == 1);
    CHECK(pthread_cond_destroy(&probe.condition) == 0);
    CHECK(pthread_mutex_destroy(&probe.mutex) == 0);
    puts("Concurrent scheduler controls completed; C continued.");
    return 0;
}
