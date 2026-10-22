/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_version.h"
#include "beam_release_version.h"
#include "erl_process.h"
#include "erl_message.h"
#include "erl_proc_sig_queue.h"
#include "erl_binary.h"
#define ERTS_WANT_EXTERNAL_TAGS
#include "external.h"
#include "beam_load.h"
#include "module.h"
#include "code_ix.h"
#include "erl_thr_progress.h"
#include "bif.h"
#include "beam_internal.h"
#include "beam_memory.h"
#include "beam_core_modules.h"
#include "erl_map.h"
#include <dlfcn.h>
#include "beam_composition_internal.h"
#include "erl_misc_utils.h"
#include "erl_async.h"
#include "atom.h"
#include "erl_check_io.h"

#include <errno.h>
#include <fenv.h>
#include <limits.h>
#include <stdint.h>
#include <stdatomic.h>
#include <signal.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <sys/syscall.h>
#include <sys/auxv.h>
#endif
#include <unistd.h>

static atomic_int start_claimed;

int beam_start_claimed(void)
{
    return atomic_load(&start_claimed);
}

int beam_claim_start(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&start_claimed, &expected, 1);
}

void beam_release_start(void)
{
    atomic_store(&start_claimed, 0);
}

const char *
beam_version(void)
{
    return "BEAM/Erlang " BEAM_RELEASE_VERSION " (erts-" ERLANG_VERSION ")";
}

const char *
beam_status_message(beam_status status)
{
    switch (status) {
    case BEAM_OK: return "success";
    case BEAM_INVALID_ARGUMENT: return "invalid API argument";
    case BEAM_OUT_OF_MEMORY: return "out of memory";
    case BEAM_RUNTIME_NOT_FOUND: return "runtime files not found or inaccessible";
    case BEAM_SYSTEM_ERROR: return "runtime system error";
    case BEAM_ALREADY_STARTED: return "runtime initialization has already been claimed";
    case BEAM_STOPPED: return "the VM has stopped";
    case BEAM_WRONG_THREAD: return "operation must use the runtime owner thread";
    case BEAM_INVALID_BEAM: return "invalid BEAM module";
    case BEAM_INVALID_TERM: return "invalid external term arguments";
    case BEAM_UNSUPPORTED: return "operation is unsupported";
    case BEAM_ALREADY_LOADED: return "the module is already loaded";
    case BEAM_INVALID_STATE: return "operation is invalid in the current VM state";
    case BEAM_NOT_FOUND: return "the module was not found";
    case BEAM_BUSY: return "the runtime cannot complete the operation safely yet";
    default: return "unknown error";
    }
}

typedef struct beam_request {
    struct beam_vm *vm;
    unsigned char *data;
    size_t size;
    char *module;
    char *function;
    Binary *prepared;
    Eterm module_atom;
    beam_status status;
    beam_result result;
    int recorded;
    int done;
    int is_call;
    beam_selected_nif *nif;
    beam_error error;
    enum { MODULE_INFO, MODULE_LIST, MODULE_UNLOAD } module_operation;
    beam_module_info module_info;
    beam_module_list module_list;
    unsigned schedulers_online;
    unsigned dirty_cpu_schedulers_online;
} beam_request;

typedef struct beam_host_request beam_host_request;

struct beam_host_reply {
    beam_buffer value;
    int supplied;
    int terminal;
    int halt_status;
};

/* All persistent request storage is owned by libc, including the encoded
 * reference. An owner callback may outlive scheduler/allocator shutdown. */
struct beam_host_request {
    beam_host_request *next;
    beam_host_request *previous;
    beam_buffer argument;
    beam_buffer reference;
    beam_host_reply reply;
    Eterm pid; /* An immediate local identity, never a Process pointer. */
    beam_status status;
    unsigned references; /* Process plus queue/callback/auxiliary-work owners. */
    int queued;
    int cancelled;
};

struct beam_vm {
    struct beam_system *system;
    pthread_t owner;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    beam_request *active;
    int stopped;
    int started;
    int halt_status;
    int halted;
    beam_host_handler handler;
    void *handler_context;
    int calls_started;
    int in_callback;
    beam_host_reply *current_reply;
    beam_host_request *host_first;
    beam_host_request *host_last;
    int host_admission;
    unsigned host_reply_aux;
    beam_composition *composition;
    beam_error initialization_error;
    atomic_int components_active;
    atomic_int activating;
    unsigned failed_component;
    beam_error component_failure;
    beam_vm_report report;
    beam_execution_mode execution;
    beam_entry root;
    beam_bytes startup_context;
    char *program_name;
    unsigned process_policy;
    beam_filename_encoding filename_encoding;
    beam_break_policy break_policy;
    atomic_int entered;
    struct sigaction saved_pipe;
    int restore_pipe;
};

struct beam_system {
    pthread_t owner;
    uint32_t creation;
};

static beam_system *bare_system;
static beam_vm *bare_vm;
static int system_initialized;
int erts_bare_destroying;

static beam_status diagnostic(beam_error *error, beam_status status,
                              int native_code, const char *message)
{
    if (error) {
        error->native_code = native_code;
        snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return status;
}

static void clear_error(beam_error *error)
{
    if (error)
        memset(error, 0, sizeof(*error));
}

beam_status beam_query_host(beam_host_info *out, beam_error *error)
{
    erts_cpu_info_t *cpu;
    beam_host_info info = {sizeof(info), 0, 0, 0, 0};
    int count;
    clear_error(error);
    if (!out || out->struct_size != sizeof(*out))
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0,
                          "host_info.struct_size must match sizeof(beam_host_info)");
    cpu = erts_cpu_info_create();
    if (!cpu)
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM,
                          "could not allocate host discovery state");
    count = erts_get_cpu_configured(cpu);
    if (count > 0) info.configured_cpus = count;
    count = erts_get_cpu_online(cpu);
    if (count > 0) info.online_cpus = count;
    count = erts_get_cpu_available(cpu);
    if (count > 0) info.available_cpus = count;
    count = erts_get_cpu_quota(cpu);
    if (count > 0) info.quota_cpus = count;
    erts_cpu_info_destroy(cpu);
    *out = info;
    return BEAM_OK;
}

void beam_vm_options_init(beam_vm_options *options)
{
    if (options) {
        *options = (beam_vm_options) {
            .struct_size = sizeof(*options),
            .schedulers = 1,
            .schedulers_online = 1,
            .dirty_cpu_schedulers = 1,
            .dirty_cpu_schedulers_online = 1,
            .dirty_io_schedulers = 1,
            .async_threads = 1,
            .poll_threads = 1,
            .process_limit = ERTS_DEFAULT_MAX_PROCESSES,
            .atom_limit = 1024*1024,
            .min_heap_size = H_DEFAULT_SIZE,
            .min_bin_vheap_size = VH_DEFAULT_SIZE,
            .max_heap_size = H_DEFAULT_MAX_SIZE,
            .allocator_tagging = BEAM_ALLOCATOR_TAGGING_DEFAULT,
            .port_limit = ERTS_DEFAULT_MAX_PORTS,
            .fullsweep_after = 65535
        };
    }
}

static beam_status validate_options(const beam_vm_options *options,
                                    beam_error *error)
{
    Uint min_heap, min_bin_heap;
#define CHECK_OPTION(test, field, description) \
    do { if (!(test)) return diagnostic(error, BEAM_INVALID_ARGUMENT, 0, \
                                        #field " " description); } while (0)
    CHECK_OPTION(options->struct_size == sizeof(*options), struct_size,
                 "must match sizeof(beam_vm_options)");
    CHECK_OPTION(options->allocator_tagging >= BEAM_ALLOCATOR_TAGGING_DEFAULT &&
                 options->allocator_tagging <= BEAM_ALLOCATOR_TAGGING_DISABLED,
                 allocator_tagging, "is invalid");
    CHECK_OPTION(options->execution == BEAM_HOST_CONTROLLED ||
                 options->execution == BEAM_PROCESS_OWNED, execution, "is invalid");
    CHECK_OPTION((options->execution == BEAM_PROCESS_OWNED) == !!options->initial_root,
                 initial_root, "is required only for process-owned execution");
    CHECK_OPTION(!(options->process_policy & ~7u), process_policy, "contains unknown policy bits");
    CHECK_OPTION(options->break_policy >= BEAM_BREAK_DEFAULT && options->break_policy <= BEAM_BREAK_INPUT,
                 break_policy, "is invalid");
    CHECK_OPTION(options->break_policy == BEAM_BREAK_DEFAULT ||
                 (options->process_policy & BEAM_PROCESS_SIGNALS), break_policy,
                 "requires process signal ownership");
    CHECK_OPTION(options->break_policy != BEAM_BREAK_INPUT ||
                 (options->process_policy & BEAM_PROCESS_STDIO), break_policy,
                 "terminal input policy requires standard-stream ownership");
    CHECK_OPTION(options->execution == BEAM_PROCESS_OWNED ||
                 (!options->process_policy && (!options->composition || !options->composition->native_admission)),
                 execution, "process-wide native admission and policy require process ownership");
    CHECK_OPTION(options->port_limit <= INT_MAX,
                 port_limit, "is outside native port bounds");
    CHECK_OPTION(options->filename_encoding == BEAM_FILENAME_UTF8 || options->filename_encoding == BEAM_FILENAME_LATIN1,
                 filename_encoding, "must select UTF-8 or Latin-1");
    CHECK_OPTION(options->fullsweep_after <= 65535, fullsweep_after, "must fit 16 bits");
    CHECK_OPTION(!options->startup_context.size || (options->startup_context.data &&
                 options->startup_context.size <= (Uint)-1), startup_context, "requires ETF bytes");
    if (options->initial_root) {
        const beam_entry *root = options->initial_root;
        CHECK_OPTION(root->module && root->module[0] && strlen(root->module) <= 1020 &&
                     root->function && root->function[0] && strlen(root->function) <= 1020,
                     initial_root, "requires module and function names");
        CHECK_OPTION(root->arguments.data && root->arguments.size >= 2 && root->arguments.size <= (Uint)-1,
                     initial_root, "requires copied ETF arguments");
    }
    CHECK_OPTION(options->schedulers >= 1 &&
                 options->schedulers <= ERTS_MAX_NO_OF_SCHEDULERS,
                 schedulers, "is outside the native scheduler bounds");
    CHECK_OPTION(options->schedulers_online >= 1 &&
                 options->schedulers_online <= options->schedulers,
                 schedulers_online, "must be between 1 and schedulers");
    CHECK_OPTION(options->dirty_cpu_schedulers >= 1 &&
                 options->dirty_cpu_schedulers <= options->schedulers,
                 dirty_cpu_schedulers, "must be between 1 and schedulers");
    CHECK_OPTION(options->dirty_cpu_schedulers_online >= 1 &&
                 options->dirty_cpu_schedulers_online <= options->dirty_cpu_schedulers &&
                 options->dirty_cpu_schedulers_online <= options->schedulers_online,
                 dirty_cpu_schedulers_online,
                 "must fit both dirty_cpu_schedulers and schedulers_online");
    CHECK_OPTION(options->dirty_io_schedulers >= 1 &&
                 options->dirty_io_schedulers <= ERTS_MAX_NO_OF_DIRTY_IO_SCHEDULERS,
                 dirty_io_schedulers, "is outside the native scheduler bounds");
    CHECK_OPTION(options->async_threads >= ERTS_MIN_NO_OF_ASYNC_THREADS &&
                 options->async_threads <= ERTS_MAX_NO_OF_ASYNC_THREADS,
                 async_threads, "is outside the native async thread bounds");
    CHECK_OPTION(options->poll_threads >= 1 &&
                 options->poll_threads <= ERTS_MAX_NO_OF_SCHEDULERS,
                 poll_threads, "is outside the native poll thread bounds");
    CHECK_OPTION(options->process_limit >= ERTS_MIN_PROCESSES &&
                 options->process_limit <= ERTS_MAX_PROCESSES,
                 process_limit, "is outside the native process table bounds");
    CHECK_OPTION(options->atom_limit >= MIN_ATOM_TABLE_SIZE &&
                 options->atom_limit <= MAX_ATOM_TABLE_SIZE,
                 atom_limit, "is outside the native atom table bounds");
    min_heap = erts_initial_heap_size(options->min_heap_size);
    min_bin_heap = erts_initial_heap_size(options->min_bin_vheap_size);
    CHECK_OPTION(options->min_heap_size >= 1 &&
                 min_heap != 0 && min_heap <= INT_MAX,
                 min_heap_size, "is outside the native heap size bounds");
    CHECK_OPTION(options->min_bin_vheap_size >= 1 &&
                 min_bin_heap != 0 && min_bin_heap <= INT_MAX,
                 min_bin_vheap_size, "is outside the native heap size bounds");
    CHECK_OPTION(options->max_heap_size <= (size_t) MAX_SMALL &&
                 (!options->max_heap_size ||
                  options->max_heap_size >= min_heap),
                 max_heap_size, "must be zero or a representable size at least min_heap_size");
#undef CHECK_OPTION
    return BEAM_OK;
}

static beam_status check_system(beam_system *system)
{
    if (!system || system != bare_system)
        return BEAM_INVALID_ARGUMENT;
    if (!pthread_equal(system->owner, pthread_self()))
        return BEAM_WRONG_THREAD;
    if (bare_vm && bare_vm->in_callback)
        return BEAM_INVALID_STATE;
    return BEAM_OK;
}

beam_status beam_init(beam_system **out, beam_error *error)
{
    beam_system *system;
    clear_error(error);
    if (!out)
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0, "system output is NULL");
#ifdef __APPLE__
    if (!pthread_main_np())
#else
    if (syscall(SYS_gettid) != getpid())
#endif
        return diagnostic(error, BEAM_WRONG_THREAD, 0,
                          "system initialization requires the main thread");
    if (system_initialized)
        return diagnostic(error, BEAM_ALREADY_STARTED, 0,
                          "system infrastructure was already initialized");
    system = calloc(1, sizeof(*system));
    if (!system)
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not allocate system handle");
    if (!beam_claim_start()) {
        free(system);
        return diagnostic(error, BEAM_ALREADY_STARTED, 0, "process already owns a BEAM runtime");
    }
    system->owner = pthread_self();
    erts_sys_pre_init();
    bare_system = system;
    system_initialized = 1;
    *out = system;
    return BEAM_OK;
}

beam_status beam_terminate(beam_system **system)
{
    beam_status status;
    if (!system)
        return BEAM_INVALID_ARGUMENT;
    if (!*system)
        return BEAM_OK;
    status = check_system(*system);
    if (status != BEAM_OK)
        return status;
    if (bare_vm)
        return BEAM_INVALID_STATE;
    if (erts_sys_system_cleanup() != 0)
        return BEAM_SYSTEM_ERROR;
    beam_runtime_memory_system_destroy();
    free(*system);
    bare_system = *system = NULL;
    return BEAM_OK;
}

static beam_status check_owner(beam_vm *vm)
{
    if (!vm || vm != bare_vm)
        return BEAM_INVALID_ARGUMENT;
    if (!pthread_equal(vm->owner, pthread_self()))
        return BEAM_WRONG_THREAD;
    if (vm->in_callback)
        return BEAM_INVALID_STATE;
    return BEAM_OK;
}

static beam_status encode_bytes(Eterm value, beam_buffer *out)
{
    Uint size;
    byte *cursor;
    if (erts_encode_ext_size(value, &size) != ERTS_EXT_SZ_OK)
        return BEAM_INVALID_TERM;
    cursor = out->data = malloc(size);
    if (!cursor)
        return BEAM_OUT_OF_MEMORY;
    erts_encode_ext(value, &cursor);
    out->size = cursor - out->data;
    return BEAM_OK;
}

/* Request references, queue links, and cancellation use vm->mutex. Process
 * locks must always be acquired before that mutex, never while holding it. */
static void host_release(beam_host_request *request)
{
    ASSERT(request->references);
    if (--request->references == 0) {
        free(request->argument.data);
        free(request->reference.data);
        free(request->reply.value.data);
        free(request);
    }
}

static void host_unlink(beam_vm *vm, beam_host_request *request)
{
    ASSERT(request->queued);
    if (request->previous)
        request->previous->next = request->next;
    else
        vm->host_first = request->next;
    if (request->next)
        request->next->previous = request->previous;
    else
        vm->host_last = request->previous;
    request->next = request->previous = NULL;
    request->queued = 0;
}

static void host_close_admission(beam_vm *vm)
{
    vm->host_admission = 0;
    while (vm->host_first) {
        beam_host_request *request = vm->host_first;
        host_unlink(vm, request);
        request->cancelled = 1;
        host_release(request); /* Release queue ownership; process will exit. */
    }
}

static void host_process_exiting(Process *p)
{
    beam_host_request *request = p->bare_host_request;
    if (!request)
        return;
    pthread_mutex_lock(&bare_vm->mutex);
    p->bare_host_request = NULL;
    request->cancelled = 1;
    if (request->queued) {
        host_unlink(bare_vm, request);
        host_release(request);
    }
    host_release(request); /* Release process ownership. */
    pthread_mutex_unlock(&bare_vm->mutex);
}

int erts_bare_host_replies_pending(void)
{
    unsigned pending;
    pthread_mutex_lock(&bare_vm->mutex);
    pending = bare_vm->host_reply_aux;
    pthread_mutex_unlock(&bare_vm->mutex);
    return pending != 0;
}

static Eterm host_atom(const char *name)
{
    return erts_atom_put((const byte *)name, strlen(name), ERTS_ATOM_ENC_UTF8, 1);
}

static Eterm host_error(Process *p, const char *reason)
{
    Eterm atom = host_atom(reason);
    Eterm *hp = HAlloc(p, 3);
    return TUPLE2(hp, am_error, atom);
}

BIF_RETTYPE erts_internal_host_request_1(BIF_ALIST_1)
{
    beam_vm *vm = bare_vm;
    beam_host_request *request;
    Eterm ref;
    beam_status status;
    if (!erts_bare_mode)
        BIF_RET(host_error(BIF_P, "unsupported"));
    pthread_mutex_lock(&vm->mutex);
    if (!vm->host_admission || !vm->handler) {
        const char *reason = vm->host_admission ? "no_handler" : "stopped";
        pthread_mutex_unlock(&vm->mutex);
        BIF_RET(host_error(BIF_P, reason));
    }
    pthread_mutex_unlock(&vm->mutex);
    request = calloc(1, sizeof(*request));
    if (!request)
        BIF_RET(host_error(BIF_P, "out_of_memory"));
    request->references = 1;
    status = encode_bytes(BIF_ARG_1, &request->argument);
    ref = erts_make_ref(BIF_P);
    if (status == BEAM_OK)
        status = encode_bytes(ref, &request->reference);
    if (status != BEAM_OK) {
        host_release(request);
        BIF_RET(host_error(BIF_P, status == BEAM_OUT_OF_MEMORY
                                   ? "out_of_memory" : "invalid_term"));
    }
    request->pid = BIF_P->common.id;
    /* This takes the process message-queue lock. Set the receive marker before
     * publication, outside vm->mutex, so no reply can precede the marker. */
    erts_msgq_set_save_end(BIF_P);
    pthread_mutex_lock(&vm->mutex);
    if (!vm->host_admission) {
        host_release(request);
        pthread_mutex_unlock(&vm->mutex);
        erts_msgq_set_save_first(BIF_P);
        BIF_RET(host_error(BIF_P, "stopped"));
    }
    ASSERT(!BIF_P->bare_host_request);
    BIF_P->bare_host_request = request;
    request->references++; /* Queue ownership becomes callback, then aux. */
    request->queued = 1;
    request->previous = vm->host_last;
    if (vm->host_last)
        vm->host_last->next = request;
    else
        vm->host_first = request;
    vm->host_last = request;
    pthread_cond_signal(&vm->condition);
    pthread_mutex_unlock(&vm->mutex);
    BIF_TRAP1(erts_await_result, BIF_P, ref);
}

/* Build an independently owned message. Invalid replies discard the entire
 * decoding factory, including any off-heap resources it created. */
static ErtsMessage *host_reply_message(beam_host_request *request, Eterm *term)
{
    ErtsHeapFactory factory;
    ErtsMessage *message;
    const byte *cursor;
    Eterm ref, value, result;
    Eterm *hp;
    Sint reference_size = erts_decode_ext_size(request->reference.data,
                                               request->reference.size);
    Sint value_size = 0;
    const char *error = NULL;
    if (request->status != BEAM_OK)
        error = "callback_failed";
    else if (!request->reply.supplied)
        error = "no_reply";
    else if (request->reply.value.size < 2 ||
             request->reply.value.data[0] != VERSION_MAGIC ||
             request->reply.value.data[1] == COMPRESSED ||
             (value_size = erts_decode_ext_size_public(request->reply.value.data,
                                                request->reply.value.size)) < 0)
        error = "invalid_reply";
    ASSERT(reference_size >= 0);
retry:
    if (error)
        value_size = 0;
    message = erts_factory_message_create(&factory, NULL, NULL,
                                           reference_size + value_size + 9);
    cursor = request->reference.data;
    ref = erts_decode_ext(&factory, &cursor, 0);
    ASSERT(is_internal_ref(ref));
    if (!error) {
        cursor = request->reply.value.data;
        value = erts_decode_ext(&factory, &cursor, 0);
        if (is_non_value(value) ||
            cursor != request->reply.value.data + request->reply.value.size) {
            erts_factory_undo(&factory);
            error = "invalid_reply";
            goto retry;
        }
    }
    else {
        value = host_atom(error);
        if (request->status != BEAM_OK) {
            hp = erts_produce_heap(&factory, 3, 0);
            value = TUPLE2(hp, value, make_small((Sint)request->status));
        }
    }
    hp = erts_produce_heap(&factory, 6, 0);
    result = TUPLE2(hp, error ? am_error : am_ok, value);
    *term = TUPLE2(hp + 3, ref, result);
    erts_factory_close(&factory);
    return message;
}

static void host_reply_command(void *argument)
{
    beam_host_request *request = argument;
    beam_vm *vm = bare_vm;
    Process *receiver;
    ErtsMessage *message = NULL;
    Eterm term;
    int deliver;
    pthread_mutex_lock(&vm->mutex);
    deliver = vm->host_admission && !request->cancelled;
    pthread_mutex_unlock(&vm->mutex);
    if (deliver) {
        message = host_reply_message(request, &term);
        receiver = erts_pid2proc(NULL, 0, request->pid, ERTS_PROC_LOCK_MAIN);
        if (receiver) {
            pthread_mutex_lock(&vm->mutex);
            deliver = vm->host_admission && !request->cancelled &&
                      receiver->bare_host_request == request;
            if (receiver->bare_host_request == request) {
                receiver->bare_host_request = NULL;
                host_release(request); /* Process ownership. */
            }
            pthread_mutex_unlock(&vm->mutex);
            if (deliver) {
                erts_queue_message(receiver, ERTS_PROC_LOCK_MAIN, message,
                                   term, am_system);
                message = NULL;
            }
            erts_proc_unlock(receiver, ERTS_PROC_LOCK_MAIN);
        }
    }
    if (message) {
        ERL_MESSAGE_TERM(message) = term;
        erts_cleanup_messages(message);
    }
    pthread_mutex_lock(&vm->mutex);
    host_release(request); /* Auxiliary-work ownership. */
    ASSERT(vm->host_reply_aux);
    vm->host_reply_aux--;
    pthread_mutex_unlock(&vm->mutex);
}

static void complete_from_host(beam_vm *vm, beam_host_reply *reply)
{
    if (vm->halted || vm->stopped) return;
    vm->halted = 1;
    vm->halt_status = reply->halt_status;
    beam_request *active = vm->active;
    vm->report.cause = BEAM_TERMINAL_HOST_REQUEST;
    vm->report.halt_status = reply->halt_status;
    if (!active || !active->recorded) {
        if (active) {
            active->recorded = 1;
            active->status = active->is_call ? BEAM_OK : BEAM_STOPPED;
            if (active->is_call) {
                active->result.kind = BEAM_COMPLETE;
                active->result.value = reply->value;
                reply->value = (beam_buffer){0};
                active->result.halt_status = reply->halt_status;
            }
            active->done = 1;
        }
    }
    host_close_admission(vm);
    erts_bare_request_stop();
    pthread_cond_signal(&vm->condition);
}

/* Called with the mutex held; returns with it held. Admission precedes the
 * unlock, so a racing halt may cancel the reply but not this C invocation. */
static void host_invoke(beam_vm *vm)
{
    beam_host_request *request = vm->host_first;
    host_unlink(vm, request);
    vm->in_callback = 1;
    vm->current_reply = &request->reply;
    pthread_mutex_unlock(&vm->mutex);
    request->status = vm->handler(vm->handler_context,
        (beam_bytes){request->argument.data, request->argument.size},
        &request->reply);
    pthread_mutex_lock(&vm->mutex);
    vm->current_reply = NULL;
    vm->in_callback = 0;
    if (request->status == BEAM_OK && request->reply.terminal && !request->cancelled)
        complete_from_host(vm, &request->reply);
    if (vm->host_admission && !request->cancelled) {
        vm->host_reply_aux++;
        erts_schedule_misc_aux_work(1, host_reply_command, request);
    }
    else {
        host_release(request); /* Callback ownership. */
    }
}

/* The caller holds vm->mutex. A halt may finish a queued command before its
 * callback or process cleanup runs, so later completion must not replace it. */
static void complete_locked(beam_request *request, beam_status status)
{
    if (!request->done) {
        request->status = status;
        request->done = 1;
        pthread_cond_signal(&request->vm->condition);
    }
}

static void complete(beam_request *request, beam_status status)
{
    beam_vm *vm = request->vm;
    pthread_mutex_lock(&vm->mutex);
    complete_locked(request, status);
    pthread_mutex_unlock(&vm->mutex);
}

static beam_status encode_result(beam_request *request, Eterm value)
{
    return encode_bytes(value, &request->result.value);
}

void erts_bare_process_return(Process *p, Eterm value)
{
    beam_request *request = p->bare_call;
    if (!request)
        return;
    pthread_mutex_lock(&request->vm->mutex);
    if (!request->recorded) {
        request->recorded = 1;
        request->result.kind = BEAM_RETURN;
        request->status = encode_result(request, value);
    }
    pthread_mutex_unlock(&request->vm->mutex);
}

int erts_bare_process_exception(Process *p, Eterm exception_class,
                               Eterm reason, Eterm trace)
{
    beam_request *request = p->bare_call;
    Eterm tuple[4];
    if (!request)
        return 0;
    pthread_mutex_lock(&request->vm->mutex);
    if (!request->recorded) {
        request->recorded = 1;
        request->result.kind = BEAM_EXCEPTION;
        request->status = encode_result(request,
            TUPLE3(tuple, exception_class, reason, trace));
    }
    pthread_mutex_unlock(&request->vm->mutex);
    return 1;
}

void erts_bare_process_exiting(Process *p, Eterm reason)
{
    host_process_exiting(p);
    erts_bare_process_exception(p, am_exit, reason, NIL);
}

void erts_bare_process_deleted(Process *p)
{
    beam_request *request = p->bare_call;
    if (request) {
        p->bare_call = NULL;
        pthread_mutex_lock(&request->vm->mutex);
        complete_locked(request, request->status);
        pthread_mutex_unlock(&request->vm->mutex);
    }
}

void erts_bare_halt(int status)
{
    beam_vm *vm = bare_vm;
    pthread_mutex_lock(&vm->mutex);
    if (!vm->halted) {
        vm->halted = 1;
        vm->halt_status = status;
    }
    if (vm->report.cause == BEAM_TERMINAL_NONE) {
        vm->report.cause = BEAM_TERMINAL_HALT;
        vm->report.halt_status = vm->halt_status;
    }
    host_close_admission(vm);
    if (vm->active) {
        beam_request *request = vm->active;
        /* The first recorded outcome wins. Later halt, including one caused by
         * retiring children after a normal return, must not replace it. */
        if (!request->recorded) {
            int kind = BEAM_HALT;
            free(request->result.value.data);
            memset(&request->result, 0, sizeof(request->result));
            if (request->is_call) {
                request->result.kind = kind;
                request->result.halt_status = vm->halt_status;
            }
            request->recorded = 1;
            request->status = request->is_call ? BEAM_OK : BEAM_STOPPED;
        }
        request->done = 1;
    }
    erts_bare_request_stop();
    pthread_cond_signal(&vm->condition);
    pthread_mutex_unlock(&vm->mutex);
}

static void load_command(void *argument)
{
    beam_request *request = argument;
    Binary *prepared = request->prepared;
    Eterm module = request->module_atom;
    Module *existing;
    if (erts_bare_stop_requested()) {
        if (prepared) {
            beam_load_prepared_free(prepared);
            request->prepared = NULL;
        }
        complete(request, BEAM_STOPPED);
        return;
    }
    if (!prepared) {
        Eterm result;
        prepared = erts_alloc_loader_state();
        module = NIL;
        result = erts_prepare_loading(prepared, NULL, NIL, &module,
                                      request->data, request->size);
        if (result != NIL) {
            complete(request, BEAM_INVALID_BEAM);
            return; /* prepare_loading releases failed loader state. */
        }
        if (erts_has_code_on_load(prepared) == am_true) {
            beam_load_prepared_free(prepared);
            complete(request, BEAM_UNSUPPORTED);
            return;
        }
        request->prepared = prepared;
        request->module_atom = module;
    }
    if (!erts_try_seize_code_load_permission_aux(load_command, request))
        return;
    existing = erts_get_module(module, erts_active_code_ix());
    if (existing && (existing->curr.code_hdr || existing->old.code_hdr
                    || existing->on_load)) {
        erts_release_code_load_permission();
        beam_load_prepared_free(prepared);
        request->prepared = NULL;
        complete(request, BEAM_ALREADY_LOADED);
        return;
    }
    /* The first profile publishes one new module at a time. Blocking here
     * reuses the loader's existing synchronous code-index commit protocol. */
    erts_thr_progress_block();
    erts_start_staging_code_ix(1);
    erts_finish_loading(prepared, NULL, 0, &module);
    request->prepared = NULL;
    erts_end_staging_code_ix();
    erts_commit_staging_code_ix();
    erts_blocking_code_barrier();
    erts_thr_progress_unblock();
    erts_release_code_load_permission();
    complete(request, BEAM_OK);
}

static beam_status module_operation(beam_request *request, int initial)
{
    switch (request->module_operation) {
    case MODULE_INFO:
        return erts_bare_get_module(request->module, initial,
                                    &request->module_info);
    case MODULE_LIST:
        return erts_bare_list_modules(initial, &request->module_list);
    case MODULE_UNLOAD:
        return erts_bare_unload_module(request->module, initial);
    }
    return BEAM_INVALID_ARGUMENT;
}

static void module_command(void *argument)
{
    beam_request *request = argument;
    beam_status status;
    if (erts_bare_stop_requested()) {
        complete(request, BEAM_STOPPED);
        return;
    }
    if (request->module_operation == MODULE_UNLOAD) {
        /* In particular, a purger can retain staging permission while it
         * waits for dirty work. Refuse rather than queue behind that purge. */
        if (!erts_try_seize_code_load_permission_aux_nowait(request)) {
            complete(request, BEAM_BUSY);
            return;
        }
    } else if (!erts_try_seize_code_load_permission_aux(module_command, request))
        return;
    status = module_operation(request, 0);
    erts_release_code_load_permission();
    complete(request, status);
}

/* Native activation uses an explicit target, never an invented Erlang caller. */
extern Eterm erts_load_selected_nif(Process *, Eterm, void *);
static void nif_command(void *argument)
{
    beam_request *request = argument;
    Process process;
    Eterm module, result;
    if (erts_bare_stop_requested()) { complete(request, BEAM_STOPPED); return; }
    if (!erts_try_seize_code_mod_permission_aux(nif_command, request)) return;
    erts_init_empty_process(&process);
    erts_proc_lock(&process, ERTS_PROC_LOCK_MAIN);
    module = erts_atom_put((byte *)request->nif->module, strlen(request->nif->module), ERTS_ATOM_ENC_UTF8, 0);
    result = erts_load_selected_nif(&process, module, request->nif->initializer);
    if (result != am_ok)
        erts_snprintf(request->error.message, sizeof(request->error.message),
                      "NIF %s activation failed: %T", request->nif->module, result);
    erts_release_code_mod_permission();
    erts_proc_unlock(&process, ERTS_PROC_LOCK_MAIN);
    erts_cleanup_empty_process(&process);
    complete(request, result == am_ok ? BEAM_OK : BEAM_SYSTEM_ERROR);
}

static void scheduler_change_complete(void *argument, beam_status status)
{
    complete(argument, status);
}

static void scheduler_command(void *argument)
{
    beam_request *request = argument;
    beam_status status;
    if (erts_bare_stop_requested()) {
        complete(request, BEAM_STOPPED);
        return;
    }
    status = erts_bare_set_schedulers_online(request->schedulers_online,
                request->dirty_cpu_schedulers_online,
                scheduler_change_complete, request);
    if (status != BEAM_OK)
        complete(request, status);
}

static void call_command(void *argument)
{
    beam_request *request = argument;
    Process temporary;
    Process *parent;
    ErtsHeapFactory factory;
    ErlSpawnOpts options;
    const byte *cursor = request->data;
    Eterm arguments, module, function, pid;
    Sint arity;
    int temporary_locked = 1;
    Sint heap_size;
    beam_status status = BEAM_INVALID_TERM;
    if (request->size > 1 && cursor[0] == VERSION_MAGIC && cursor[1] == COMPRESSED) {
        complete(request, status);
        return;
    }
    heap_size = erts_decode_ext_size_public(cursor, request->size);
    if (heap_size < 0) {
        complete(request, status);
        return;
    }
    erts_init_empty_process(&temporary);
    erts_proc_lock(&temporary, ERTS_PROC_LOCK_MAIN);
    if (heap_size)
        erts_factory_proc_prealloc_init(&factory, &temporary, heap_size);
    else
        erts_factory_dummy_init(&factory);
    arguments = erts_decode_ext(&factory, &cursor, 0);
    if (is_non_value(arguments)) {
        erts_factory_undo(&factory);
        goto done;
    }
    erts_factory_close(&factory);
    arity = erts_list_length(arguments);
    if (cursor != request->data + request->size || arity < 0 || arity > MAX_ARG)
        goto done;
    module = erts_atom_put((byte *)request->module, strlen(request->module),
                           ERTS_ATOM_ENC_UTF8, 0);
    function = erts_atom_put((byte *)request->function, strlen(request->function),
                             ERTS_ATOM_ENC_UTF8, 0);
    if (is_non_value(module) || is_non_value(function)) {
        status = BEAM_INVALID_ARGUMENT;
        goto done;
    }
    ERTS_SET_DEFAULT_SPAWN_OPTS(&options);
    options.bare_call = request;
    erts_proc_unlock(&temporary, ERTS_PROC_LOCK_MAIN);
    temporary_locked = 0;
    parent = erts_pid2proc(NULL, 0, erts_init_process_id, ERTS_PROC_LOCK_MAIN);
    if (!parent) {
        status = BEAM_SYSTEM_ERROR;
        goto done;
    }
    pid = erl_create_process(parent, module, function, arguments, &options);
    erts_proc_unlock(parent, ERTS_PROC_LOCK_MAIN);
    status = is_non_value(pid) ? BEAM_SYSTEM_ERROR : BEAM_OK;
done:
    if (temporary_locked)
        erts_proc_unlock(&temporary, ERTS_PROC_LOCK_MAIN);
    erts_cleanup_empty_process(&temporary);
    if (status != BEAM_OK)
        complete(request, status);
}

extern void erts_components_request_stop(void);

static beam_status dispatch(beam_vm *vm, beam_request *request,
                            void (*command)(void *))
{
    beam_status status;
    int halted;
    request->vm = vm;
    pthread_mutex_lock(&vm->mutex);
    if (vm->halted || vm->stopped) {
        pthread_mutex_unlock(&vm->mutex);
        if (!vm->stopped) {
            erts_components_request_stop();
            erts_bare_stop_runtime();
            vm->stopped = 1;
        }
        return BEAM_STOPPED;
    }
    vm->active = request;
    request->is_call = command == call_command;
    if (command == call_command)
        vm->calls_started = 1;
    erts_schedule_misc_aux_work(1, command, request);
    while (!request->done) {
        if (command == call_command && vm->host_admission && vm->host_first)
            host_invoke(vm);
        else
            pthread_cond_wait(&vm->condition, &vm->mutex);
    }
    status = request->status;
    halted = vm->halted;
    vm->active = NULL;
    pthread_mutex_unlock(&vm->mutex);
    if (halted) {
        erts_components_request_stop();
        erts_bare_stop_runtime();
        vm->stopped = 1;
    }
    return status;
}

static beam_status check_running(beam_vm *vm)
{
    int stopped;
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    pthread_mutex_lock(&vm->mutex);
    stopped = vm->halted || vm->stopped;
    pthread_mutex_unlock(&vm->mutex);
    if (stopped) {
        if (!vm->stopped) {
            erts_components_request_stop();
            erts_bare_stop_runtime();
            vm->stopped = 1;
        }
        return BEAM_STOPPED;
    }
    if (!vm->started)
        return BEAM_INVALID_STATE;
    return BEAM_OK;
}

/* Validate the entire term without retaining process heap state. Only the
 * copied wire representation survives restarts and is decoded on demand. */
static beam_status validate_context(beam_bytes bytes)
{
    Process process;
    ErtsHeapFactory factory;
    const byte *cursor = bytes.data;
    Sint words;
    Eterm term;
    if (!bytes.size) return BEAM_OK;
    if (bytes.size < 2 || cursor[0] != VERSION_MAGIC || cursor[1] == COMPRESSED ||
        (words = erts_decode_ext_size_public(cursor, bytes.size)) < 0)
        return BEAM_INVALID_TERM;
    erts_init_empty_process(&process);
    erts_proc_lock(&process, ERTS_PROC_LOCK_MAIN);
    if (words) erts_factory_proc_prealloc_init(&factory, &process, words);
    else erts_factory_dummy_init(&factory);
    term = erts_decode_ext(&factory, &cursor, 0);
    if (is_non_value(term)) erts_factory_undo(&factory);
    else erts_factory_close(&factory);
    beam_status status = is_value(term) && cursor == bytes.data + bytes.size ? BEAM_OK : BEAM_INVALID_TERM;
    erts_proc_unlock(&process, ERTS_PROC_LOCK_MAIN);
    erts_cleanup_empty_process(&process);
    return status;
}

BIF_RETTYPE erts_internal_vm_context_0(BIF_ALIST_0)
{
    ErtsHeapFactory factory;
    if (!bare_vm || !bare_vm->startup_context.size) BIF_RET(am_undefined);
    const byte *cursor = bare_vm->startup_context.data;
    Sint words = erts_decode_ext_size_public(cursor, bare_vm->startup_context.size);
    if (words) erts_factory_proc_prealloc_init(&factory, BIF_P, words);
    else erts_factory_dummy_init(&factory);
    Eterm result = erts_decode_ext(&factory, &cursor, 0);
    ASSERT(is_value(result));
    erts_factory_close(&factory);
    BIF_RET(result);
}

int beam_system_is_initialized(void) { return beam_start_claimed(); }

size_t erts_vm_preloads(const beam_module_spec **modules)
{
    const beam_composition *composition = bare_vm ? bare_vm->composition : NULL;
    *modules = composition ? composition->preloads : NULL;
    return composition ? composition->preload_count : 0;
}

size_t erts_vm_builtin_nifs(const beam_builtin_nif **entries)
{
    const beam_composition *composition = bare_vm ? bare_vm->composition : NULL;
    *entries = composition ? composition->builtin_nifs : NULL;
    return composition ? composition->builtin_nif_count : 0;
}

size_t erts_vm_builtin_drivers(const beam_builtin_driver **entries)
{
    const beam_composition *composition = bare_vm ? bare_vm->composition : NULL;
    *entries = composition ? composition->builtin_drivers : NULL;
    return composition ? composition->builtin_driver_count : 0;
}

void erts_vm_native_init_failed(const char *kind, const char *name)
{
    if (bare_vm && !bare_vm->initialization_error.message[0])
        snprintf(bare_vm->initialization_error.message, sizeof(bare_vm->initialization_error.message),
                 "built-in %s initialization failed: %s", kind, name);
}

beam_status beam_core_modules(const char *const **names, size_t *count)
{
    if (!names || !count) return BEAM_INVALID_ARGUMENT;
    *names = beam_core_module_names;
    *count = sizeof(beam_core_module_names) / sizeof(beam_core_module_names[0]);
    return BEAM_OK;
}

int erts_vm_native_admitted(unsigned permission)
{
    return bare_vm && bare_vm->composition &&
        (bare_vm->composition->native_admission & permission) == permission;
}

int erts_vm_process_policy(unsigned policy)
{
    return bare_vm && (bare_vm->process_policy & policy) == policy;
}

beam_break_policy erts_vm_break_policy(void)
{
    return bare_vm ? bare_vm->break_policy : BEAM_BREAK_DEFAULT;
}

int erts_vm_filename_utf8(void)
{
    return !bare_vm || bare_vm->filename_encoding == BEAM_FILENAME_UTF8;
}

const char *erts_vm_program_name(void)
{
    return bare_vm ? bare_vm->program_name : "libbeam";
}

beam_status beam_vm_init(beam_system *system, const beam_vm_options *options,
                         beam_vm **out, beam_error *error)
{
    beam_vm *vm;
    beam_vm_options defaults;
    beam_status status;
    int err;
    clear_error(error);
    if (!out)
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0, "VM output is NULL");
    status = check_system(system);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    if (bare_vm)
        return diagnostic(error, BEAM_ALREADY_STARTED, 0,
                          "system already owns a VM handle");
    if (!options) {
        beam_vm_options_init(&defaults);
        options = &defaults;
    }
    status = validate_options(options, error);
    if (status != BEAM_OK)
        return status;
    if (system->creation == UINT32_MAX)
        return diagnostic(error, BEAM_UNSUPPORTED, 0,
                          "system VM creation numbers are exhausted");
    if (!erts_bare_check_signal_handlers())
        return diagnostic(error, BEAM_UNSUPPORTED, 0,
                          "host fault handlers require alternate signal stack support");
    vm = calloc(1, sizeof(*vm));
    if (!vm)
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not allocate VM handle");
    vm->system = system;
    vm->owner = pthread_self();
    vm->host_admission = 1;
    if ((err = pthread_mutex_init(&vm->mutex, NULL)) != 0) {
        free(vm);
        return diagnostic(error, BEAM_SYSTEM_ERROR, err, "could not initialize VM mutex");
    }
    if ((err = pthread_cond_init(&vm->condition, NULL)) != 0) {
        pthread_mutex_destroy(&vm->mutex);
        free(vm);
        return diagnostic(error, BEAM_SYSTEM_ERROR, err, "could not initialize VM condition");
    }
    vm->execution = options->execution;
    if (options->initial_root) {
        const beam_entry *root = options->initial_root;
        vm->root.module = strdup(root->module);
        vm->root.function = strdup(root->function);
        vm->root.arguments.data = malloc(root->arguments.size);
        if (!vm->root.module || !vm->root.function || !vm->root.arguments.data) {
            free((void *)vm->root.module);
            free((void *)vm->root.function);
            free((void *)vm->root.arguments.data);
            pthread_cond_destroy(&vm->condition);
            pthread_mutex_destroy(&vm->mutex);
            free(vm);
            return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not copy initial root");
        }
        memcpy((void *)vm->root.arguments.data, root->arguments.data, root->arguments.size);
        vm->root.arguments.size = root->arguments.size;
    }
    vm->process_policy = options->process_policy;
    vm->filename_encoding = options->filename_encoding;
    vm->break_policy = options->break_policy;
    vm->program_name = strdup(options->program_name ? options->program_name : "libbeam");
    if (options->startup_context.size) {
        vm->startup_context.data = malloc(options->startup_context.size);
        if (vm->startup_context.data) {
            memcpy((void *)vm->startup_context.data, options->startup_context.data, options->startup_context.size);
            vm->startup_context.size = options->startup_context.size;
        }
    }
    if (!vm->program_name || (options->startup_context.size && !vm->startup_context.data)) {
        free(vm->program_name);
        free((void *)vm->startup_context.data);
        free((void *)vm->root.module);
        free((void *)vm->root.function);
        free((void *)vm->root.arguments.data);
        pthread_cond_destroy(&vm->condition);
        pthread_mutex_destroy(&vm->mutex);
        free(vm);
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM, "could not copy VM startup context");
    }
    vm->composition = (beam_composition *)options->composition;
    beam_composition_retain(vm->composition);
    vm->report.struct_size = sizeof(vm->report);
    vm->components_active = !beam_composition_needs_activation(vm->composition);
    if (vm->composition) {
        erts_components_begin(vm->composition->services, vm->composition->program_helper);
        beam_components_activate(vm->composition->components, vm->composition->count);
    }
    bare_vm = vm;
    erts_bare_mode = 1;
    beam_runtime_memory_begin();
    erts_bare_set_creation(++system->creation);
    erts_bare_init(options);
    if (vm->initialization_error.message[0]) {
        if (error) *error = vm->initialization_error;
        beam_vm_terminate(&vm, NULL);
        return BEAM_INVALID_ARGUMENT;
    }
    {
        status = validate_context(vm->startup_context);
        if (status == BEAM_OK) status = beam_composition_stage(vm, vm->composition, error);
        else diagnostic(error, status, 0, "startup context must be one uncompressed ETF term");
        if (status != BEAM_OK) {
            beam_vm_terminate(&vm, NULL);
            return status;
        }
    }
    *out = vm;
    return BEAM_OK;
}

static beam_status start_vm(beam_vm *vm, int process_owned, beam_error *error)
{
    beam_status status = check_owner(vm);
    clear_error(error);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    if (vm->started || vm->stopped)
        return diagnostic(error, BEAM_INVALID_STATE, 0,
                          "VM start requires an initialized, unstarted VM");
    if ((vm->execution == BEAM_PROCESS_OWNED) != process_owned)
        return diagnostic(error, BEAM_INVALID_STATE, 0,
                          "execution ownership requires its explicit start operation");
    if (!erts_bare_check_signal_handlers())
        return diagnostic(error, BEAM_UNSUPPORTED, 0,
                          "host fault handlers require alternate signal stack support");
    if (erts_components_program_enabled()) {
        if (sigaction(SIGPIPE, NULL, &vm->saved_pipe) != 0)
            return diagnostic(error, BEAM_SYSTEM_ERROR, errno, "could not save host SIGPIPE disposition");
        vm->restore_pipe = 1;
    }
    status = erts_bare_start(process_owned ? &vm->root : NULL);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, "could not prepare initial root");
    vm->started = 1;
    return BEAM_OK;
}

beam_status beam_vm_start(beam_vm *vm, beam_error *error)
{
    return start_vm(vm, 0, error);
}

int erts_vm_process_owned(void)
{
    return bare_vm && bare_vm->entered;
}

static void resume_root(void *argument)
{
    beam_request *request = argument;
    Process *root = erts_pid2proc(NULL, 0, erts_init_process_id, ERTS_PROC_LOCK_MAIN);
    ASSERT(root);
    erts_resume(root, ERTS_PROC_LOCK_MAIN);
    erts_proc_unlock(root, ERTS_PROC_LOCK_MAIN);
    complete(request, BEAM_OK);
}

beam_status beam_vm_enter(beam_vm *vm, beam_error *error)
{
    beam_request request = {0};
    beam_status status = start_vm(vm, 1, error);
    if (status != BEAM_OK) return status;
    status = beam_vm_activate(vm, error);
    if (status != BEAM_OK) return status;
    /* Ownership transfers before the initial root can execute. Both the
     * root's fatal system-process semantics and process-owned halt now apply. */
    vm->entered = 1;
    erts_sys_process_ready();
    vm->calls_started = 1;
    status = dispatch(vm, &request, resume_root);
    if (status == BEAM_OK) {
        pthread_mutex_lock(&vm->mutex);
        while (!vm->halted) {
            if (vm->host_admission && vm->host_first) host_invoke(vm);
            else pthread_cond_wait(&vm->condition, &vm->mutex);
        }
        pthread_mutex_unlock(&vm->mutex);
    }
    /* Component failure can request coordinated stop instead of native halt.
     * Process ownership is irrevocable even on this failure path. */
    int halt_status = 1;
    (void)beam_vm_stop(vm, &halt_status);
    exit(halt_status);
}

beam_status beam_vm_get_state(beam_vm *vm, beam_vm_state_info *out)
{
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    if (!out)
        return BEAM_INVALID_ARGUMENT;
    pthread_mutex_lock(&vm->mutex);
    out->state = vm->stopped ? BEAM_VM_STOPPED :
                 vm->halted ? BEAM_VM_HALT_REQUESTED :
                 vm->started ? BEAM_VM_RUNNING : BEAM_VM_INITIALIZED;
    out->halt_status = vm->halt_status;
    pthread_mutex_unlock(&vm->mutex);
    return BEAM_OK;
}

beam_status beam_vm_set_host_handler(beam_vm *vm, beam_host_handler handler,
                                     void *context)
{
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    pthread_mutex_lock(&vm->mutex);
    if (vm->stopped || vm->halted)
        status = BEAM_STOPPED;
    else if (vm->calls_started)
        status = BEAM_INVALID_STATE;
    else {
        vm->handler = handler;
        vm->handler_context = handler ? context : NULL;
    }
    pthread_mutex_unlock(&vm->mutex);
    return status;
}

beam_status beam_host_reply_copy(beam_host_reply *reply, beam_bytes value)
{
    beam_vm *vm = bare_vm;
    unsigned char *data;
    if (!vm || !reply)
        return BEAM_INVALID_ARGUMENT;
    if (!pthread_equal(vm->owner, pthread_self()))
        return BEAM_WRONG_THREAD;
    if (!vm->in_callback || vm->current_reply != reply || reply->supplied)
        return BEAM_INVALID_STATE;
    if (!value.data || !value.size || value.size > (Uint)-1)
        return BEAM_INVALID_ARGUMENT;
    data = malloc(value.size);
    if (!data)
        return BEAM_OUT_OF_MEMORY;
    memcpy(data, value.data, value.size);
    reply->value = (beam_buffer){data, value.size};
    reply->supplied = 1;
    return BEAM_OK;
}

beam_status beam_host_reply_complete(beam_host_reply *reply, beam_bytes value, int halt_status)
{
    beam_vm *vm = bare_vm;
    if (!vm || !reply || halt_status < 0 || !value.data || !value.size || value.size > (Uint)-1)
        return BEAM_INVALID_ARGUMENT;
    if (!pthread_equal(vm->owner, pthread_self())) return BEAM_WRONG_THREAD;
    if (!vm->in_callback || vm->current_reply != reply || reply->supplied) return BEAM_INVALID_STATE;
    if (value.size < 2 || value.data[0] != VERSION_MAGIC || value.data[1] == COMPRESSED ||
        erts_decode_ext_size_public(value.data, value.size) < 0) return BEAM_INVALID_TERM;
    beam_status status = beam_host_reply_copy(reply, value);
    if (status == BEAM_OK) {
        reply->terminal = 1;
        reply->halt_status = halt_status;
    }
    return status;
}

beam_status beam_vm_load(beam_vm *vm, beam_bytes module)
{
    beam_request request = {0};
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    if (vm->stopped)
        return BEAM_STOPPED;
    if (!vm->started) {
        if (!module.data || !module.size || module.size > (Uint)-1)
            return BEAM_INVALID_ARGUMENT;
        return erts_bare_load_initial(module);
    }
    status = check_running(vm);
    if (status != BEAM_OK)
        return status;
    if (!module.data || !module.size || module.size > (Uint)-1)
        return BEAM_INVALID_ARGUMENT;
    request.data = malloc(module.size);
    if (!request.data)
        return BEAM_OUT_OF_MEMORY;
    memcpy(request.data, module.data, module.size);
    request.size = module.size;
    status = dispatch(vm, &request, load_command);
    if (request.result.kind == BEAM_HALT)
        status = BEAM_STOPPED;
    free(request.data);
    return status;
}

/* Unlike execution admission, observation does not join a halted VM. */
static beam_status check_management(beam_vm *vm, int require_running)
{
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    pthread_mutex_lock(&vm->mutex);
    if (vm->halted || vm->stopped)
        status = BEAM_STOPPED;
    else if (require_running && !vm->started)
        status = BEAM_INVALID_STATE;
    pthread_mutex_unlock(&vm->mutex);
    return status;
}

static beam_status dispatch_module(beam_vm *vm, beam_request *request)
{
    if (!vm->started)
        return module_operation(request, 1);
    return dispatch(vm, request, module_command);
}

beam_status beam_vm_get_module(beam_vm *vm, const char *name,
                               beam_module_info *out)
{
    beam_request *request;
    beam_status status = check_management(vm, 0);
    if (status != BEAM_OK)
        return status;
    if (!name || !*name || !out || out->struct_size != sizeof(*out))
        return BEAM_INVALID_ARGUMENT;
    request = calloc(1, sizeof(*request));
    if (!request)
        return BEAM_OUT_OF_MEMORY;
    request->module_operation = MODULE_INFO;
    request->module_info.struct_size = sizeof(request->module_info);
    request->module = strdup(name);
    if (!request->module) {
        free(request);
        return BEAM_OUT_OF_MEMORY;
    }
    status = dispatch_module(vm, request);
    if (status == BEAM_OK)
        *out = request->module_info;
    free(request->module);
    free(request);
    return status;
}

beam_status beam_vm_list_modules(beam_vm *vm, beam_module_list *out)
{
    beam_request *request;
    beam_status status = check_management(vm, 0);
    if (status != BEAM_OK)
        return status;
    if (!out)
        return BEAM_INVALID_ARGUMENT;
    request = calloc(1, sizeof(*request));
    if (!request)
        return BEAM_OUT_OF_MEMORY;
    request->module_operation = MODULE_LIST;
    status = dispatch_module(vm, request);
    if (status == BEAM_OK)
        *out = request->module_list;
    else
        beam_module_list_release(&request->module_list);
    free(request);
    return status;
}

void beam_module_list_release(beam_module_list *list)
{
    if (list) {
        free((void *)list->names);
        memset(list, 0, sizeof(*list));
    }
}

beam_status beam_vm_unload(beam_vm *vm, const char *name, beam_error *error)
{
    beam_request *request;
    beam_status status;
    clear_error(error);
    status = check_management(vm, 0);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    if (!name || !*name)
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0,
                           "module name must be a nonempty UTF-8 atom name");
    request = calloc(1, sizeof(*request));
    if (!request)
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM,
                           "could not allocate module removal request");
    if (vm->composition) for (size_t i = 0; i < vm->composition->nif_count; ++i) {
        if (!strcmp(name, vm->composition->nifs[i].module)) {
            free(request);
            return diagnostic(error, BEAM_BUSY, 0, "selected NIF modules remain bound until VM termination");
        }
    }
    request->module_operation = MODULE_UNLOAD;
    request->module = strdup(name);
    if (!request->module) {
        free(request);
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM,
                           "could not copy module name");
    }
    status = dispatch_module(vm, request);
    free(request->module);
    free(request);
    if (status == BEAM_INVALID_ARGUMENT)
        return diagnostic(error, status, 0,
                           "module name must be a nonempty UTF-8 atom name of at most 255 characters");
    if (status == BEAM_UNSUPPORTED)
        return diagnostic(error, status, 0,
                           "module is protected or has pending on_load code");
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    return BEAM_OK;
}

beam_status beam_vm_get_schedulers(beam_vm *vm, beam_scheduler_info *out)
{
    beam_scheduler_info info = {0};
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    if (!out || out->struct_size != sizeof(*out))
        return BEAM_INVALID_ARGUMENT;
    info.struct_size = sizeof(info);
    erts_bare_get_schedulers(&info);
    *out = info;
    return BEAM_OK;
}

beam_status beam_vm_set_schedulers_online(beam_vm *vm, unsigned normal,
                                         unsigned dirty, beam_error *error)
{
    beam_scheduler_info info = {0};
    beam_request *request;
    beam_status status;
    clear_error(error);
    status = check_management(vm, 1);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    erts_bare_get_schedulers(&info);
    if (!normal || normal > info.schedulers)
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0,
                           "schedulers_online must be between 1 and schedulers");
    if (!dirty || dirty > info.dirty_cpu_schedulers || dirty > normal)
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0,
                           "dirty_cpu_schedulers_online must fit dirty_cpu_schedulers and schedulers_online");
    request = calloc(1, sizeof(*request));
    if (!request)
        return diagnostic(error, BEAM_OUT_OF_MEMORY, ENOMEM,
                           "could not allocate scheduler change request");
    request->schedulers_online = normal;
    request->dirty_cpu_schedulers_online = dirty;
    status = dispatch(vm, request, scheduler_command);
    /* On halt dispatch joins workers, retiring every native reference before
     * releasing this request. Completion on a live VM relinquishes the native
     * slot before notifying the owner. */
    free(request);
    if (status != BEAM_OK)
        return diagnostic(error, status, 0, beam_status_message(status));
    return BEAM_OK;
}

beam_status beam_vm_call(beam_vm *vm, const char *module, const char *function,
                         beam_bytes arguments, beam_result *out)
{
    beam_request request = {0};
    beam_status status = check_running(vm);
    if (status == BEAM_OK && !vm->components_active && !vm->activating)
        status = BEAM_INVALID_STATE;
    if (out)
        memset(out, 0, sizeof(*out));
    if (status != BEAM_OK)
        return status;
    if (!out || !module || !*module || !function || !*function
        || !arguments.data || !arguments.size || arguments.size > (Uint)-1)
        return BEAM_INVALID_ARGUMENT;
    request.module = strdup(module);
    request.function = strdup(function);
    request.data = malloc(arguments.size);
    if (!request.module || !request.function || !request.data) {
        status = BEAM_OUT_OF_MEMORY;
        goto done;
    }
    memcpy(request.data, arguments.data, arguments.size);
    request.size = arguments.size;
    status = dispatch(vm, &request, call_command);
    if (status == BEAM_OK)
        *out = request.result;
    else
        beam_result_release(&request.result);
done:
    free(request.module);
    free(request.function);
    free(request.data);
    return status;
}

beam_status beam_vm_stop(beam_vm *vm, int *halt_status)
{
    beam_status status = check_owner(vm);
    if (status != BEAM_OK)
        return status;
    if (!vm->stopped) {
        pthread_mutex_lock(&vm->mutex);
        host_close_admission(vm);
        erts_components_request_stop();
        if (vm->started)
            erts_bare_request_stop();
        pthread_mutex_unlock(&vm->mutex);
        erts_bare_stop_runtime();
        vm->stopped = 1;
    }
    if (halt_status)
        *halt_status = vm->halt_status;
    if (vm->restore_pipe) {
        if (sigaction(SIGPIPE, &vm->saved_pipe, NULL) != 0) return BEAM_SYSTEM_ERROR;
        vm->restore_pipe = 0;
    }
    return BEAM_OK;
}

void erts_components_set_cleanup(const char *bytes, size_t size)
{
    beam_vm *vm = bare_vm;
    if (!vm || !vm->composition || !bytes) return;
    pthread_mutex_lock(&vm->mutex);
    if (!vm->report.has_cleanup_error) {
        if (size >= sizeof(vm->report.cleanup.message)) size = sizeof(vm->report.cleanup.message) - 1;
        memcpy(vm->report.cleanup.message, bytes, size);
        vm->report.cleanup.message[size] = 0;
        vm->report.has_cleanup_error = 1;
    }
    pthread_mutex_unlock(&vm->mutex);
}

void erts_components_set_native_cleanup(int native_code, const char *message)
{
    beam_vm *vm = bare_vm;
    if (!vm || !vm->composition) return;
    pthread_mutex_lock(&vm->mutex);
    if (!vm->report.has_cleanup_error) {
        vm->report.has_cleanup_error = 1;
        diagnostic(&vm->report.cleanup, BEAM_SYSTEM_ERROR, native_code, message);
    }
    pthread_mutex_unlock(&vm->mutex);
}

beam_status beam_vm_get_report(beam_vm *vm, beam_vm_report *out)
{
    beam_status status = check_owner(vm);
    if (status != BEAM_OK) return status;
    if (!out || out->struct_size != sizeof(*out)) return BEAM_INVALID_ARGUMENT;
    pthread_mutex_lock(&vm->mutex);
    beam_vm_report report = vm->report;
    pthread_mutex_unlock(&vm->mutex);
    *out = report;
    return BEAM_OK;
}

beam_status beam_vm_terminate(beam_vm **vm, int *halt_status)
{
    return beam_vm_terminate_report(vm, halt_status, NULL);
}

beam_status beam_vm_terminate_report(beam_vm **vm, int *halt_status, beam_vm_report *out)
{
    beam_status status;
    if (!vm || (out && out->struct_size != sizeof(*out))) return BEAM_INVALID_ARGUMENT;
    if (!*vm) {
        if (halt_status) *halt_status = 0;
        if (out) *out = (beam_vm_report){.struct_size = sizeof(*out)};
        return BEAM_OK;
    }
    status = check_owner(*vm);
    if (status != BEAM_OK) return status;
    status = beam_vm_stop(*vm, halt_status);
    if (status != BEAM_OK) {
        (*vm)->report.has_cleanup_error = 1;
        diagnostic(&(*vm)->report.cleanup, status, errno, "could not restore host SIGPIPE disposition");
        if (out) (void)beam_vm_get_report(*vm, out);
        return status;
    }
    erts_bare_destroy_runtime((*vm)->started);
    if (out) (void)beam_vm_get_report(*vm, out);
    if ((*vm)->composition) {
        beam_components_deactivate();
        erts_components_end();
        beam_composition_release(&(*vm)->composition);
    }
    pthread_cond_destroy(&(*vm)->condition);
    pthread_mutex_destroy(&(*vm)->mutex);
    free((void *)(*vm)->startup_context.data);
    free((*vm)->program_name);
    free((void *)(*vm)->root.module);
    free((void *)(*vm)->root.function);
    free((void *)(*vm)->root.arguments.data);
    free(*vm);
    bare_vm = *vm = NULL;
    return BEAM_OK;
}

beam_status beam_vm_activate(beam_vm *vm, beam_error *error)
{
    beam_result result = {0};
    beam_status status = check_running(vm);
    beam_buffer arguments = {0};
    const char *failed_support = NULL;
    clear_error(error);
    if (status != BEAM_OK) return diagnostic(error, status, 0, beam_status_message(status));
    if (vm->components_active) return BEAM_OK;
    if (!vm->composition) return BEAM_OK;
    int support = beam_composition_needs_support(vm->composition);
    if (support) {
        for (size_t i = 0; i < sizeof(beam_activation_support_modules) / sizeof(beam_activation_support_modules[0]); ++i) {
            beam_module_info info = {.struct_size = sizeof(info)};
            status = beam_vm_get_module(vm, beam_activation_support_modules[i], &info);
            if (status != BEAM_OK || !(info.flags & BEAM_MODULE_CURRENT)) {
                failed_support = beam_activation_support_modules[i];
                status = diagnostic(error, BEAM_SYSTEM_ERROR, 0, "required ordinary activation support is not loaded");
                break;
            }
        }
    }
    if (support && status == BEAM_OK) status = beam_composition_activation_arguments(vm->composition, &arguments);
    vm->activating = 1;
    if (support && status == BEAM_OK) status = beam_vm_call(vm, "beam_component_services", "activate",
                          (beam_bytes){arguments.data, arguments.size}, &result);
    vm->activating = 0;
    free(arguments.data);
    if (status == BEAM_OK && support && (result.kind != BEAM_RETURN || result.value.size != 3 ||
        memcmp(result.value.data, "\203\141\000", 3))) status = BEAM_SYSTEM_ERROR;
    beam_result_release(&result);
    for (size_t i = 0; status == BEAM_OK && i < vm->composition->nif_count; ++i) {
        beam_request request = {0};
        request.nif = &vm->composition->nifs[i];
        status = dispatch(vm, &request, nif_command);
        if (status != BEAM_OK) {
            pthread_mutex_lock(&vm->mutex);
            vm->component_failure = request.error;
            vm->failed_component = 0; /* Failure is not necessarily a native component. */
            if (vm->report.cause == BEAM_TERMINAL_NONE) {
                vm->report.cause = BEAM_TERMINAL_COMPONENT_FAILURE;
                vm->report.halt_status = 1;
                snprintf(vm->report.component, sizeof(vm->report.component), "%s", request.nif->module);
                vm->report.failure = request.error;
            }
            pthread_mutex_unlock(&vm->mutex);
        }
    }
    pthread_mutex_lock(&vm->mutex);
    if (status == BEAM_OK && !vm->halted && !vm->component_failure.message[0]) {
        vm->components_active = 1;
        pthread_mutex_unlock(&vm->mutex);
        return BEAM_OK;
    }
    if (status == BEAM_OK) status = BEAM_SYSTEM_ERROR;
    if (!vm->component_failure.message[0]) {
        vm->failed_component = 0; /* Failure is not necessarily a native component. */
        if (error && error->message[0]) vm->component_failure = *error;
        else diagnostic(&vm->component_failure, status, 0, "component activation failed");
        if (vm->report.cause == BEAM_TERMINAL_NONE) {
            vm->report.cause = BEAM_TERMINAL_COMPONENT_FAILURE;
            vm->report.halt_status = 1;
            snprintf(vm->report.component, sizeof(vm->report.component), "%s",
                     failed_support ? failed_support : "services");
            vm->report.failure = vm->component_failure;
        }
    }
    if (error) *error = vm->component_failure;
    pthread_mutex_unlock(&vm->mutex);
    beam_vm_stop(vm, NULL);
    return status;
}

beam_status beam_vm_get_activation(beam_vm *vm, beam_activation_info *out)
{
    beam_status status = check_owner(vm);
    beam_activation_info info = {.struct_size = sizeof(info)};
    if (status != BEAM_OK) return status;
    if (!out || out->struct_size != sizeof(*out)) return BEAM_INVALID_ARGUMENT;
    pthread_mutex_lock(&vm->mutex);
    if (!beam_composition_needs_activation(vm->composition)) info.state = BEAM_ACTIVATION_ABSENT;
    else if (vm->component_failure.message[0]) {
        info.state = BEAM_ACTIVATION_FAILED;
        info.failure = vm->component_failure;
    } else if (vm->stopped || vm->halted) info.state = BEAM_ACTIVATION_STOPPED;
    else info.state = vm->components_active ? BEAM_ACTIVATION_ACTIVE : BEAM_ACTIVATION_PREPARED;
    pthread_mutex_unlock(&vm->mutex);
    *out = info;
    return BEAM_OK;
}

beam_status beam_vm_get_component(beam_vm *vm, beam_component_kind kind,
                                  beam_component_info *out)
{
    beam_status status = check_owner(vm);
    beam_component_info info = {0};
    if (status != BEAM_OK) return status;
    if (!out || out->struct_size != sizeof(*out) || !beam_component_kind_valid(kind))
        return BEAM_INVALID_ARGUMENT;
    info.struct_size = sizeof(info);
    pthread_mutex_lock(&vm->mutex);
    if (!vm->composition || !(vm->composition->selected & (1u << kind)))
        info.state = BEAM_COMPONENT_ABSENT;
    else if (vm->component_failure.message[0] && (unsigned)kind == vm->failed_component) {
        info.state = BEAM_COMPONENT_FAILED;
        info.failure = vm->component_failure;
    } else if (vm->stopped || vm->halted) info.state = BEAM_COMPONENT_STOPPED;
    else info.state = vm->components_active ? BEAM_COMPONENT_ACTIVE : BEAM_COMPONENT_PREPARED;
    pthread_mutex_unlock(&vm->mutex);
    *out = info;
    return BEAM_OK;
}

BIF_RETTYPE erts_internal_component_ready_1(BIF_ALIST_1)
{
    unsigned service = 0;
    if (BIF_ARG_1 == host_atom("file")) service = BEAM_SERVICE_FILE;
    else if (BIF_ARG_1 == host_atom("socket")) service = BEAM_SERVICE_SOCKET;
    else if (BIF_ARG_1 == host_atom("zlib")) service = BEAM_SERVICE_ZLIB;
    else if (BIF_ARG_1 == host_atom("program")) service = BEAM_SERVICE_PROGRAM;
    else if (BIF_ARG_1 == host_atom("output")) service = BEAM_SERVICE_OUTPUT;
    else if (BIF_ARG_1 == host_atom("code")) service = BEAM_SERVICE_CODE;
    else BIF_ERROR(BIF_P, BADARG);
    if (!erts_bare_mode || !erts_components_has_service(service)) BIF_RET(am_false);
    if (bare_vm->composition && !bare_vm->components_active) BIF_RET(am_false);
    BIF_RET(am_true);
}

BIF_RETTYPE erts_internal_component_failed_2(BIF_ALIST_2)
{
    beam_vm *vm = bare_vm;
    char service[1024];
    if (!erts_bare_mode || !vm || !vm->composition) BIF_ERROR(BIF_P, EXC_NOTSUP);
    if (!is_atom(BIF_ARG_1)) BIF_ERROR(BIF_P, BADARG);
    Atom *name = atom_tab(atom_val(BIF_ARG_1));
    if ((size_t)name->len >= sizeof(service)) BIF_ERROR(BIF_P, BADARG);
    memcpy(service, erts_atom_get_name(name), name->len);
    service[name->len] = 0;
    pthread_mutex_lock(&vm->mutex);
    if (!erts_bare_stop_requested() && !vm->component_failure.message[0]) {
        vm->components_active = 0;
        vm->failed_component = BIF_ARG_1 == host_atom("file") ? BEAM_COMPONENT_FILE :
            BIF_ARG_1 == host_atom("socket") ? BEAM_COMPONENT_NETWORK :
            BIF_ARG_1 == host_atom("zlib") ? BEAM_COMPONENT_ZLIB : 0;
        erts_snprintf(vm->component_failure.message, sizeof(vm->component_failure.message),
                      "%s component failed: %T", service, BIF_ARG_2);
        if (!vm->halted) {
            vm->report.cause = BEAM_TERMINAL_COMPONENT_FAILURE;
            vm->report.halt_status = 1;
            snprintf(vm->report.component, sizeof(vm->report.component), "%s", service);
            vm->report.failure = vm->component_failure;
        }
    }
    pthread_mutex_unlock(&vm->mutex);
    if (!erts_bare_stop_requested()) erts_bare_halt(1);
    BIF_RET(am_ok);
}

void erts_bare_destroy_runtime(int started)
{
    /* There can be no users of the VM after stop/join. Native references must
     * release synchronously here, without queuing scheduler work. */
    erts_bare_destroying = 1;
    erts_bare_begin_allocator_reclamation();
    erts_bare_destroy_builtin_drivers();
    if (ethr_runtime_cleanup() != 0)
        ERTS_INTERNAL_ERROR("could not clear VM thread state");
    erts_bare_destroy_trace();
    erts_bare_destroy_timers();
    erts_bare_destroy_async(started);
    erts_bare_destroy_term_roots();
    erts_bare_destroy_code();
    erts_bare_destroy_nif_registry();
    erts_bare_destroy_cpu_topology();
    erts_bare_time_sup_cleanup();
    erts_bare_check_io_destroy();
    erts_sys_vm_cleanup();
    erts_bare_destroy_scheduling(started);
    erts_bare_destroy_msacc();
    erts_sys_vm_cleanup_locks();
    if (ethr_runtime_unbind() != 0)
        ERTS_INTERNAL_ERROR("could not unbind VM thread state");
    erts_bare_reset_init();
    erts_bare_reset_node_tables();
    erts_bare_reset_allocators();
    erts_bare_reset_mseg();
    erts_bare_reset_mmap();
    erts_bare_reset_errno();
    beam_runtime_memory_destroy();
    erts_bare_destroying = 0;
}

void beam_result_release(beam_result *result)
{
    if (result) {
        free(result->value.data);
        memset(result, 0, sizeof(*result));
    }
}

beam_status
beam_library_path(char *buffer, size_t capacity, size_t *required, beam_error *error)
{
    Dl_info info;
    char *path;
    size_t length;
    clear_error(error);
    if (!required || (!buffer && capacity))
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0, "invalid library path output");
    /* A private anchor identifies the owning image without symbol interposition. */
    if (!dladdr((void *)&clear_error, &info))
        return diagnostic(error, BEAM_RUNTIME_NOT_FOUND, 0, "could not locate runtime image");
    /* dladdr may report argv[0] for the executable, which can be relative to
     * a working directory the host has since left. Identify the main image by
     * its ELF entry point and resolve the kernel's executable link instead. */
    const char *image = info.dli_fname;
#ifdef __APPLE__
    char main_image[PATH_MAX];
    uint32_t main_capacity=sizeof(main_image);
    if (info.dli_fbase == (void *)_dyld_get_image_header(0)) {
        if (_NSGetExecutablePath(main_image,&main_capacity))
            return diagnostic(error, BEAM_RUNTIME_NOT_FOUND, 0, "executable path exceeds buffer");
        image=main_image;
    }
#else
    Dl_info executable;
    if (dladdr((void *)getauxval(AT_ENTRY), &executable) &&
        executable.dli_fbase == info.dli_fbase)
        image = "/proc/self/exe";
#endif
    path = realpath(image, NULL);
    if (!path) {
        int saved_errno = errno;
        return diagnostic(error, saved_errno == ENOMEM ? BEAM_OUT_OF_MEMORY : BEAM_RUNTIME_NOT_FOUND,
                          saved_errno, "could not resolve runtime image path");
    }
    length = strlen(path) + 1;
    *required = length;
    if (buffer && capacity < length) {
        free(path);
        return diagnostic(error, BEAM_INVALID_ARGUMENT, 0, "library path buffer is too small");
    }
    if (buffer)
        memcpy(buffer, path, length);
    free(path);
    return BEAM_OK;
}
