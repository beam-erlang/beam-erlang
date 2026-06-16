/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <beam.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"
#include "support.h"

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "bridge:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

typedef struct reader {
    const unsigned char *data;
    size_t size, pos;
} reader;

typedef struct host_context {
    beam_vm *vm;
    pthread_t owner;
    unsigned calls;
    int failed, fail_output;
    unsigned char output[1024];
    size_t output_size;
} host_context;

static const unsigned char empty_data[] = {131, 106};
static const beam_bytes empty = {empty_data, sizeof empty_data};
static const unsigned char ok_data[] = {131, 119, 2, 'o', 'k'};
static const beam_bytes ok_term = {ok_data, sizeof ok_data};

static void pause_ms(long milliseconds)
{
    struct timespec duration = {milliseconds / 1000, milliseconds % 1000 * 1000000};
    while (nanosleep(&duration, &duration) != 0) {}
}

static double monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static int byte(reader *r, unsigned *value)
{
    if (r->pos == r->size) return 0;
    *value = r->data[r->pos++];
    return 1;
}

static int u32(reader *r, uint32_t *value)
{
    unsigned b;
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; ++i) {
        if (!byte(r, &b)) return 0;
        v = (v << 8) | b;
    }
    *value = v;
    return 1;
}

static int atom(reader *r, char *value, size_t capacity)
{
    unsigned tag, n, low;
    if (!byte(r, &tag) || !byte(r, &n)) return 0;
    if (tag == 100 || tag == 118) {
        if (!byte(r, &low)) return 0;
        n = (n << 8) | low;
    } else if (tag != 115 && tag != 119) return 0;
    if (n >= capacity || n > r->size - r->pos) return 0;
    memcpy(value, r->data + r->pos, n);
    value[n] = 0;
    r->pos += n;
    return 1;
}

static int is_atom(reader *r, const char *expected)
{
    char value[64];
    return atom(r, value, sizeof value) && !strcmp(value, expected);
}

static int tuple(reader *r, unsigned expected)
{
    unsigned tag, n;
    return byte(r, &tag) && tag == 104 && byte(r, &n) && n == expected;
}

static int number(reader *r, uint64_t *value)
{
    unsigned tag, n, sign, digit;
    uint32_t small;
    uint64_t v = 0;
    if (!byte(r, &tag)) return 0;
    if (tag == 97) {
        if (!byte(r, &digit)) return 0;
        *value = digit;
        return 1;
    }
    if (tag == 98) {
        if (!u32(r, &small) || small > INT32_MAX) return 0;
        *value = small;
        return 1;
    }
    if (tag != 110 || !byte(r, &n) || n > 8 || !byte(r, &sign) || sign) return 0;
    for (unsigned i = 0; i < n; ++i) {
        if (!byte(r, &digit)) return 0;
        v |= (uint64_t)digit << (i * 8);
    }
    *value = v;
    return 1;
}

static int list(reader *r, uint32_t *length)
{
    unsigned tag;
    return byte(r, &tag) && tag == 108 && u32(r, length);
}

static int nil(reader *r)
{
    unsigned tag;
    return byte(r, &tag) && tag == 106;
}

static int result_atom(const beam_result *result, const char *expected)
{
    reader r = {result->value.data, result->value.size, 0};
    unsigned version;
    return result->kind == BEAM_RETURN && byte(&r, &version) && version == 131 &&
        is_atom(&r, expected) && r.pos == r.size;
}

static int result_number(const beam_result *result, uint64_t expected)
{
    reader r = {result->value.data, result->value.size, 0};
    unsigned version;
    uint64_t value;
    return result->kind == BEAM_RETURN && byte(&r, &version) && version == 131 &&
        number(&r, &value) && value == expected && r.pos == r.size;
}

static int snapshot(const beam_result *result, int present)
{
    reader r = {result->value.data, result->value.size, 0};
    unsigned version;
    uint32_t length;
    uint64_t value;
    char key[64], status[64];
    int have_total = 0;
    if (result->kind != BEAM_RETURN || !byte(&r, &version) || version != 131 ||
        !tuple(&r, 7) || !is_atom(&r, "snapshot") ||
        !number(&r, &value) || !value || !number(&r, &value) || !value ||
        !number(&r, &value) || !number(&r, &value) ||
        !list(&r, &length) || !length) return 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (!tuple(&r, 2) || !atom(&r, key, sizeof key) || !number(&r, &value)) return 0;
        if (!strcmp(key, "total")) have_total = value > 0;
    }
    if (!have_total || !nil(&r)) return 0;
    if (!present) return is_atom(&r, "absent") && r.pos == r.size;
    if (!tuple(&r, 2) || !is_atom(&r, "present") || !list(&r, &length) || length != 4)
        return 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (!tuple(&r, 2) || !atom(&r, key, sizeof key)) return 0;
        if (!strcmp(key, "status")) {
            if (!atom(&r, status, sizeof status)) return 0;
        } else if (!number(&r, &value)) return 0;
    }
    return nil(&r) && r.pos == r.size;
}

static beam_status handler(void *opaque, beam_bytes request, beam_host_reply *reply)
{
    host_context *host = opaque;
    reader r = {request.data, request.size, 0};
    unsigned version;
    char operation[64];
    beam_status status;
#define CALLBACK_CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "callback:%d: %s\n", __LINE__, #test); \
    host->failed = 1; return BEAM_SYSTEM_ERROR; \
} } while (0)
    CALLBACK_CHECK(pthread_equal(host->owner, pthread_self()));
    ++host->calls;
    CALLBACK_CHECK(byte(&r, &version) && version == 131);
    if (r.pos < r.size && r.data[r.pos] == 104) {
        uint32_t length;
        unsigned tag;
        CALLBACK_CHECK(tuple(&r, 2) && atom(&r, operation, sizeof operation));
        if (!strcmp(operation, "echo")) {
            CALLBACK_CHECK(request.size > 131072);
            return beam_host_reply_copy(reply, request);
        }
        CALLBACK_CHECK(!strcmp(operation, "write") && byte(&r, &tag) && tag == 109);
        CALLBACK_CHECK(u32(&r, &length) && length == r.size - r.pos);
        if (host->fail_output) return BEAM_SYSTEM_ERROR;
        CALLBACK_CHECK(length <= sizeof host->output - host->output_size);
        memcpy(host->output + host->output_size, r.data + r.pos, length);
        host->output_size += length;
        return beam_host_reply_copy(reply, ok_term);
    }
    CALLBACK_CHECK(atom(&r, operation, sizeof operation) && r.pos == r.size);
    if (!strcmp(operation, "copy")) {
        unsigned char *copied = malloc(12);
        static const unsigned char term[] = {131,109,0,0,0,6,'c','o','p','i','e','d'};
        CALLBACK_CHECK(copied != NULL);
        memcpy(copied, term, sizeof term);
        status = beam_host_reply_copy(reply, (beam_bytes){copied, sizeof term});
        memset(copied, 0xa5, sizeof term);
        free(copied);
        CALLBACK_CHECK(status == BEAM_OK);
        CALLBACK_CHECK(beam_host_reply_copy(reply, ok_term) == BEAM_INVALID_STATE);
        return BEAM_OK;
    }
    if (!strcmp(operation, "no_reply")) {
        CALLBACK_CHECK(beam_host_reply_copy(reply, (beam_bytes){NULL, 0}) == BEAM_INVALID_ARGUMENT);
        return BEAM_OK;
    }
    if (!strcmp(operation, "invalid_reply")) {
        static const unsigned char malformed[] = {131,104,1};
        CALLBACK_CHECK(beam_host_reply_copy(reply, (beam_bytes){malformed, sizeof malformed}) == BEAM_OK);
        return BEAM_OK;
    }
    if (!strcmp(operation, "trailing_reply")) {
        unsigned char trailing[8200] = {131,109,0,0,32,0};
        trailing[8198] = 97;
        trailing[8199] = 2;
        CALLBACK_CHECK(beam_host_reply_copy(reply, (beam_bytes){trailing, sizeof trailing}) == BEAM_OK);
        return BEAM_OK;
    }
    if (!strcmp(operation, "invalid_after_binary")) {
        /* Sizing accepts this tuple; decoding allocates its binary before
         * discovering that its final atom contains invalid UTF-8. */
        unsigned char malformed[8203] = {131,104,2,109,0,0,32,0};
        malformed[8200] = 119;
        malformed[8201] = 1;
        malformed[8202] = 255;
        CALLBACK_CHECK(beam_host_reply_copy(reply, (beam_bytes){malformed, sizeof malformed}) == BEAM_OK);
        return BEAM_OK;
    }
    if (!strcmp(operation, "compressed_reply")) {
        static const unsigned char compressed[] = {131,80,0,0,0,2,120,156,75,212,2,0,0,238,0,140};
        CALLBACK_CHECK(beam_host_reply_copy(reply, (beam_bytes){compressed, sizeof compressed}) == BEAM_OK);
        return BEAM_OK;
    }
    if (!strcmp(operation, "failed")) return BEAM_SYSTEM_ERROR;
    if (!strcmp(operation, "reentrant")) {
        beam_result nested = {0};
        beam_module_info module = {sizeof(module), 0};
        beam_module_list modules = {0};
        beam_scheduler_info schedulers = {sizeof(schedulers), 0};
        beam_vm_state_info state;
        int halt_status;
        CALLBACK_CHECK(beam_vm_call(host->vm, "bare_bridge", "answer", empty, &nested) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_load(host->vm, (beam_bytes){bare_bridge, sizeof bare_bridge}) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_stop(host->vm, &halt_status) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_start(host->vm, NULL) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_terminate(&host->vm, &halt_status) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_get_state(host->vm, &state) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_get_module(host->vm, "bare_bridge", &module) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_list_modules(host->vm, &modules) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_unload(host->vm, "bare_bridge", NULL) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_get_schedulers(host->vm, &schedulers) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_set_schedulers_online(host->vm, 1, 1, NULL) == BEAM_INVALID_STATE);
        CALLBACK_CHECK(beam_vm_set_host_handler(host->vm, handler, host) == BEAM_INVALID_STATE);
    }
    if (!strcmp(operation, "dead_requester") || !strcmp(operation, "halt_in_callback") ||
        !strcmp(operation, "fairness_first")) {
        unsigned char copy[128];
        CALLBACK_CHECK(request.size <= sizeof copy);
        memcpy(copy, request.data, request.size);
        pause_ms(400);
        CALLBACK_CHECK(memcmp(copy, request.data, request.size) == 0);
    }
    return beam_host_reply_copy(reply, ok_term);
#undef CALLBACK_CHECK
}

static int call_atom(beam_vm *vm, const char *module, const char *function, const char *expected)
{
    beam_result result = {0};
    OK(beam_vm_call(vm, module, function, empty, &result));
    CHECK(result_atom(&result, expected));
    beam_result_release(&result);
    return 0;
}

static void *wrong_thread(void *opaque)
{
    host_context *host = opaque;
    beam_vm_state_info info = {BEAM_VM_RUNNING, 123};
    if (beam_vm_get_state(host->vm, &info) != BEAM_WRONG_THREAD ||
        info.state != BEAM_VM_RUNNING || info.halt_status != 123 ||
        beam_vm_set_host_handler(host->vm, handler, host) != BEAM_WRONG_THREAD)
        host->failed = 1;
    return NULL;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "normal";
    host_context host = {0};
    beam_system *system = NULL;
    beam_vm_options options;
    beam_vm_options_init(&options);

    beam_vm_state_info info = {BEAM_VM_HALT_REQUESTED, 123};
    beam_result result = {0}, saved = {0};
    unsigned char *saved_copy = NULL;
    int halt_status = -1, expected_halt = 0;
    pthread_t thread;
    host.owner = pthread_self();
    CHECK(beam_vm_get_state(NULL, &info) == BEAM_INVALID_ARGUMENT);
    CHECK(info.state == BEAM_VM_HALT_REQUESTED && info.halt_status == 123);
    OK(beam_init(&system, NULL));
    OK(beam_vm_init(system, &options, &host.vm, NULL));
    CHECK(beam_vm_get_state(host.vm, NULL) == BEAM_INVALID_ARGUMENT);
    OK(beam_vm_get_state(host.vm, &info));
    CHECK(info.state == BEAM_VM_INITIALIZED && info.halt_status == 0);
    CHECK(beam_vm_load(host.vm, (beam_bytes){empty_data, sizeof empty_data}) == BEAM_INVALID_BEAM);
    OK(beam_vm_get_state(host.vm, &info));
    CHECK(info.state == BEAM_VM_INITIALIZED && info.halt_status == 0);
    CHECK(pthread_create(&thread, NULL, wrong_thread, &host) == 0);
    CHECK(pthread_join(thread, NULL) == 0 && !host.failed);
    OK(beam_vm_load(host.vm, (beam_bytes){host_module, sizeof host_module}));
    OK(beam_vm_load(host.vm, (beam_bytes){bare_bridge, sizeof bare_bridge}));
    OK(beam_vm_load(host.vm, (beam_bytes){bare_observe, sizeof bare_observe}));
    OK(beam_vm_start(host.vm, NULL));
    if (!strcmp(mode, "no-handler")) {
        CHECK(call_atom(host.vm, "bare_bridge", "no_handler", "ok") == 0);
        CHECK(beam_vm_set_host_handler(host.vm, handler, &host) == BEAM_INVALID_STATE);
    } else {
        OK(beam_vm_set_host_handler(host.vm, handler, &host));
        CHECK(beam_vm_call(host.vm, NULL, "answer", empty, &result) == BEAM_INVALID_ARGUMENT);
        OK(beam_vm_set_host_handler(host.vm, NULL, NULL));
        OK(beam_vm_set_host_handler(host.vm, handler, &host));
        if (!strcmp(mode, "normal")) {
            const unsigned char seven[] = {131,108,0,0,0,1,97,7,106};
            CHECK(call_atom(host.vm, "bare_bridge", "requests", "ok") == 0);
            CHECK(host.calls == 9 && !host.failed);
            CHECK(beam_vm_set_host_handler(host.vm, handler, &host) == BEAM_INVALID_STATE);
            OK(beam_vm_call(host.vm, "bare_observe", "snapshot", empty, &result));
            CHECK(snapshot(&result, 0));
            beam_result_release(&result);
            CHECK(call_atom(host.vm, "bare_observe", "start", "ok") == 0);
            for (unsigned i = 1; i <= 2; ++i) {
                OK(beam_vm_call(host.vm, "bare_observe", "add", (beam_bytes){seven, sizeof seven}, &result));
                CHECK(result_number(&result, 7 * i));
                beam_result_release(&result);
            }
            OK(beam_vm_call(host.vm, "bare_observe", "snapshot", empty, &saved));
            CHECK(snapshot(&saved, 1));
            saved_copy = malloc(saved.value.size);
            CHECK(saved_copy != NULL);
            memcpy(saved_copy, saved.value.data, saved.value.size);
            CHECK(call_atom(host.vm, "bare_observe", "stop", "ok") == 0);
            OK(beam_vm_call(host.vm, "bare_observe", "snapshot", empty, &result));
            CHECK(snapshot(&result, 0));
            beam_result_release(&result);
            OK(beam_vm_get_state(host.vm, &info));
            CHECK(info.state == BEAM_VM_RUNNING && info.halt_status == 0);
        } else if (!strcmp(mode, "invalid-call")) {
            static const unsigned char malformed[] = {131,108};
            CHECK(beam_vm_call(host.vm, "bare_bridge", "answer",
                              (beam_bytes){malformed, sizeof malformed}, &result) == BEAM_INVALID_TERM);
            CHECK(beam_vm_set_host_handler(host.vm, handler, &host) == BEAM_INVALID_STATE);
        } else if (!strcmp(mode, "pending") || !strcmp(mode, "pending-stop")) {
            CHECK(call_atom(host.vm, "bare_bridge", "pending", "ok") == 0);
            pause_ms(250);
            CHECK(host.calls == 0);
            {
                beam_module_info module = {sizeof(module), 0};
                beam_module_list modules = {0};
                beam_scheduler_info schedulers = {sizeof(schedulers), 0};
                OK(beam_vm_get_module(host.vm, "bare_bridge", &module));
                OK(beam_vm_list_modules(host.vm, &modules));
                CHECK(beam_vm_unload(host.vm, "absent_bridge_module", NULL) == BEAM_NOT_FOUND);
                OK(beam_vm_get_schedulers(host.vm, &schedulers));
                OK(beam_vm_set_schedulers_online(host.vm, 1, 1, NULL));
                beam_module_list_release(&modules);
                CHECK(host.calls == 0);
            }
            if (!strcmp(mode, "pending")) {
                CHECK(call_atom(host.vm, "bare_bridge", "pending_result", "ok") == 0);
                CHECK(host.calls == 1);
            }
        } else if (!strcmp(mode, "pending-dead")) {
            CHECK(call_atom(host.vm, "bare_bridge", "pending_dead", "ok") == 0);
            pause_ms(250);
            CHECK(host.calls == 0);
            OK(beam_vm_call(host.vm, "bare_bridge", "answer", empty, &result));
            CHECK(result_number(&result, 42) && host.calls == 0);
            beam_result_release(&result);
        } else if (!strcmp(mode, "dead-requester")) {
            CHECK(call_atom(host.vm, "bare_bridge", "dead_requester", "ok") == 0);
            CHECK(host.calls == 1 && !host.failed);
            OK(beam_vm_call(host.vm, "bare_bridge", "answer", empty, &result));
            CHECK(result_number(&result, 42));
            beam_result_release(&result);
        } else if (!strcmp(mode, "fairness")) {
            CHECK(call_atom(host.vm, "bare_bridge", "fairness", "ok") == 0);
            CHECK(host.calls == 1 && !host.failed);
            CHECK(call_atom(host.vm, "bare_bridge", "fairness_result", "ok") == 0);
            CHECK(host.calls == 2 && !host.failed);
        } else if (!strcmp(mode, "halt-callback")) {
            OK(beam_vm_call(host.vm, "bare_bridge", "halt_in_callback", empty, &result));
            CHECK(result.kind == BEAM_HALT && result.halt_status == 23);
            CHECK(host.calls == 1 && !host.failed);
            beam_result_release(&result);
            expected_halt = 23;
            OK(beam_vm_get_state(host.vm, &info));
            CHECK(info.state == BEAM_VM_STOPPED && info.halt_status == 23);
        } else if (!strcmp(mode, "background")) {
            double deadline;
            OK(beam_vm_call(host.vm, "bare_bridge", "background_halt", empty, &result));
            if (result.kind == BEAM_HALT) {
                /* A heavily delayed owner may observe halt before the arm call returns. */
                CHECK(result.halt_status == 19);
                OK(beam_vm_get_state(host.vm, &info));
                CHECK(info.state == BEAM_VM_STOPPED && info.halt_status == 19);
            } else {
                CHECK(result_atom(&result, "ok"));
                deadline = monotonic_seconds() + 10;
                do {
                    OK(beam_vm_get_state(host.vm, &info));
                    if (info.state == BEAM_VM_HALT_REQUESTED) break;
                    CHECK(info.state == BEAM_VM_RUNNING && monotonic_seconds() < deadline);
                    pause_ms(1);
                } while (1);
                CHECK(info.halt_status == 19);
                pause_ms(100);
                OK(beam_vm_get_state(host.vm, &info));
                CHECK(info.state == BEAM_VM_HALT_REQUESTED && info.halt_status == 19);
            }
            beam_result_release(&result);
            expected_halt = 19;
        } else if (!strcmp(mode, "io")) {
            static const unsigned char world[] = {131,108,0,0,0,1,109,0,0,0,5,'W','o','r','l','d',106};
            static const unsigned char name[] = {131,108,0,0,0,1,109,0,0,0,8,'M',0xc3,0xbc,'n','c','h','e','n',106};
            static const unsigned char ascii[] = "Hello, World!\n";
            static const unsigned char expected[] = "Hello, M\303\274nchen!\n";
            OK(beam_vm_load(host.vm, (beam_bytes){bare_io, sizeof bare_io}));
            for (size_t i = 0; i < sizeof io_modules / sizeof *io_modules; ++i)
                OK(beam_vm_load(host.vm, io_modules[i]));
            OK(beam_vm_call(host.vm, "bare_io", "hello", (beam_bytes){world, sizeof world}, &result));
            CHECK(result_atom(&result, "ok"));
            beam_result_release(&result);
            CHECK(host.output_size == sizeof ascii - 1 &&
                  !memcmp(host.output, ascii, sizeof ascii - 1));
            host.output_size = 0;
            OK(beam_vm_call(host.vm, "bare_io", "hello", (beam_bytes){name, sizeof name}, &result));
            CHECK(result_atom(&result, "ok"));
            beam_result_release(&result);
            CHECK(host.output_size == sizeof expected - 1 &&
                  !memcmp(host.output, expected, sizeof expected - 1));
            CHECK(call_atom(host.vm, "bare_io", "bad_format", "ok") == 0);
            host.fail_output = 1;
            CHECK(call_atom(host.vm, "bare_io", "failed_output", "ok") == 0);
        } else CHECK(0);
    }
    OK(beam_vm_stop(host.vm, &halt_status));
    CHECK(halt_status == expected_halt);
    OK(beam_vm_get_state(host.vm, &info));
    CHECK(info.state == BEAM_VM_STOPPED && info.halt_status == expected_halt);
    CHECK(beam_vm_set_host_handler(host.vm, handler, &host) == BEAM_STOPPED);
    OK(beam_vm_stop(host.vm, &halt_status));
    CHECK(halt_status == expected_halt && !host.failed);
    if (!strcmp(mode, "pending-stop")) CHECK(host.calls == 0);
    CHECK(pthread_create(&thread, NULL, wrong_thread, &host) == 0);
    CHECK(pthread_join(thread, NULL) == 0 && !host.failed);
    OK(beam_vm_terminate(&host.vm, &halt_status));
    CHECK(host.vm == NULL && halt_status == expected_halt);
    OK(beam_terminate(&system));
    CHECK(system == NULL);
    if (saved_copy) {
        CHECK(!memcmp(saved_copy, saved.value.data, saved.value.size) && snapshot(&saved, 1));
        free(saved_copy);
        beam_result_release(&saved);
    }
    puts("Host bridge stopped; C continued.");
    return 0;
}
