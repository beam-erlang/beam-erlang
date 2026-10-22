/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BEAM_H
#define BEAM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental API. No source or binary compatibility promise yet. */
typedef enum beam_status {
    BEAM_OK = 0,
    BEAM_INVALID_ARGUMENT = 1,
    BEAM_OUT_OF_MEMORY = 2,
    BEAM_RUNTIME_NOT_FOUND = 3,
    BEAM_SYSTEM_ERROR = 4,
    BEAM_ALREADY_STARTED = 5,
    BEAM_STOPPED = 6,
    BEAM_WRONG_THREAD = 7,
    BEAM_INVALID_BEAM = 8,
    BEAM_INVALID_TERM = 9,
    BEAM_UNSUPPORTED = 10,
    BEAM_ALREADY_LOADED = 11,
    BEAM_INVALID_STATE = 12,
    BEAM_NOT_FOUND = 13,
    BEAM_BUSY = 14
} beam_status;

const char *beam_version(void);
/* Opaque identity of the loaded runtime build. Borrowed for the library's
 * lifetime; valid before initialization, with no filesystem or VM activity. */
const char *beam_build_id(void);
const char *beam_status_message(beam_status status);

typedef struct beam_system beam_system;
typedef struct beam_vm beam_vm;
typedef struct beam_composition beam_composition;
typedef struct beam_entry beam_entry;

typedef enum beam_execution_mode {
    BEAM_HOST_CONTROLLED = 0,
    BEAM_PROCESS_OWNED = 1
} beam_execution_mode;

/* Optional caller-owned diagnostics, cleared on entry to functions accepting
 * them. native_code is an OS error where applicable; message is NUL-terminated.
 * Returned errors never print diagnostics. */
typedef struct beam_error {
    int native_code;
    char message[256];
} beam_error;

typedef struct beam_host_info {
    /* Set to sizeof(beam_host_info). Zero means an unavailable CPU count. */
    size_t struct_size;
    unsigned configured_cpus;
    unsigned online_cpus;
    unsigned available_cpus;
    unsigned quota_cpus;
} beam_host_info;

typedef struct beam_bytes {
    const unsigned char *data;
    size_t size;
} beam_bytes;

typedef enum beam_process_policy {
    BEAM_PROCESS_SIGNALS = 1u << 0,
    BEAM_PROCESS_STDIO = 1u << 1,
    BEAM_PROCESS_DIAGNOSTICS = 1u << 2
} beam_process_policy;

typedef enum beam_filename_encoding {
    BEAM_FILENAME_UTF8 = 0,
    BEAM_FILENAME_LATIN1 = 1
} beam_filename_encoding;

/* Process-owned interrupt behavior, applied at entry rather than initialization.
 * Non-default selections require BEAM_PROCESS_SIGNALS. INPUT also requires
 * BEAM_PROCESS_STDIO and keeps Ctrl+C available as terminal input. */
typedef enum beam_break_policy {
    BEAM_BREAK_DEFAULT = 0,
    BEAM_BREAK_DISABLED = 1,
    BEAM_BREAK_IGNORE = 2,
    BEAM_BREAK_INPUT = 3
} beam_break_policy;

typedef enum beam_native_admission {
    BEAM_NATIVE_NIFS = 1u << 0,
    BEAM_NATIVE_DRIVERS = 1u << 1,
    BEAM_NATIVE_PORTS = 1u << 2,
    BEAM_NATIVE_DISTRIBUTION = 1u << 3
} beam_native_admission;

/* Borrow immutable embedded-core names. Pure inspection; no VM initialization. */
beam_status beam_core_modules(const char *const **names, size_t *count);
/* Process start admission is permanent after successful system initialization. */
int beam_system_is_initialized(void);

typedef enum beam_allocator_tagging {
    BEAM_ALLOCATOR_TAGGING_DEFAULT = 0,
    BEAM_ALLOCATOR_TAGGING_ENABLED,
    BEAM_ALLOCATOR_TAGGING_DISABLED
} beam_allocator_tagging;

typedef struct beam_vm_options {
    /* Initialize with beam_vm_options_init; the layout must match exactly. */
    size_t struct_size;
    /* Scheduler/poll counts are positive; the async pool may be zero. Online normal schedulers cannot exceed total;
     * dirty CPU totals/online counts must fit the corresponding normal counts. */
    /* This lifecycle starts schedulers unbound; selecting a CPU binding policy
     * is not currently part of the public options contract. */
    unsigned schedulers;
    unsigned schedulers_online;
    unsigned dirty_cpu_schedulers;
    unsigned dirty_cpu_schedulers_online;
    unsigned dirty_io_schedulers;
    unsigned async_threads;
    unsigned poll_threads;
    /* VM-wide table capacities; native tables may round requested capacities. */
    size_t process_limit;
    size_t atom_limit;
    /* Per-process defaults, in machine words. Minimum sizes are rounded up to
     * a supported heap size. max_heap_size is not a VM-wide memory budget;
     * zero disables the per-process maximum. */
    size_t min_heap_size;
    size_t min_bin_vheap_size;
    size_t max_heap_size;
    /* Optional immutable component preparation, retained by initialization. */
    const beam_composition *composition;
    /* Execution ownership selects neither a framework nor native facilities. */
    beam_execution_mode execution;
    /* Required for process-owned execution and rejected for host-controlled
     * execution. Initialization copies names and uncompressed ETF arguments. */
    const beam_entry *initial_root;
    /* Optional copied uncompressed ETF term, immutable across restart.
     * Empty selects undefined. Available through erts_internal:vm_context/0. */
    beam_bytes startup_context;
    /* Explicit process-wide policy; requires process-owned execution. */
    unsigned process_policy;
    /* Copied diagnostic identity; NULL retains the embedding identity. */
    const char *program_name;
    /* DEFAULT preserves each allocator's native default. Explicit modes select
     * allocation-origin tags for the user-configurable allocators; temporary,
     * literal and test allocators are unaffected. Fixed for this VM lifetime. */
    beam_allocator_tagging allocator_tagging;
    size_t port_limit;
    int port_limit_ignore_files;
    int ets_limit;
    unsigned fullsweep_after;
    beam_filename_encoding filename_encoding;
    beam_break_policy break_policy;
} beam_vm_options;

/* Selected scheduler capacity and availability, not busy/live thread counts.
 * Before start these are startup selections; after stop, the last selections. */
typedef struct beam_scheduler_info {
    size_t struct_size;
    unsigned schedulers;
    unsigned schedulers_online;
    unsigned dirty_cpu_schedulers;
    unsigned dirty_cpu_schedulers_online;
    unsigned dirty_io_schedulers;
} beam_scheduler_info;

/* Observe native selections in any valid VM state without starting, joining,
 * or executing Erlang. Set struct_size exactly; output is unchanged on error. */
beam_status beam_vm_get_schedulers(beam_vm *vm, beam_scheduler_info *out);

/* Change both online selections together in a running VM. Counts must be
 * positive, fit their totals, and dirty CPU online must fit normal online.
 * No scaling or clamping occurs. Success waits for both groups to acknowledge;
 * later Erlang operations may change them again. Existing transitions return
 * BEAM_BUSY without changes. This blocking operation invokes no host callbacks. */
beam_status beam_vm_set_schedulers_online(beam_vm *vm,
                                         unsigned schedulers_online,
                                         unsigned dirty_cpu_schedulers_online,
                                         beam_error *error);

/* Query a host snapshot without initializing the library or a VM. Counts
 * describe availability, not reserved capacity; zero denotes unknown. */
beam_status beam_query_host(beam_host_info *out, beam_error *error);

/* Conservative explicit defaults: one worker of each kind, 1048576 processes
 * and atoms, minimum heaps of 233/32768 words, no maximum heap limit. */
void beam_vm_options_init(beam_vm_options *options);

/* Initialize library infrastructure on the main thread. No VM workers or
 * Erlang processes execute. Only one system may be initialized at a time.
 * Operations on this system and its VM belong to this same owner thread.
 * System reinitialization after beam_terminate and library unloading remain
 * unsupported; sequential VM lifetimes share one initialized system.
 * On returned errors, the output handle is unchanged. */
beam_status beam_init(beam_system **out, beam_error *error);

/* Release library infrastructure; reject any system that still owns a VM,
 * including an initialized or stopped VM. Successful release clears *system.
 * Copied result buffers may outlive the system while libbeam stays loaded.
 * A NULL *system is harmless cleanup; the pointer argument must be non-NULL. */
beam_status beam_terminate(beam_system **system);

struct beam_entry {
    const char *module;
    const char *function;
    /* Exactly one uncompressed ETF proper list, including the version byte. */
    beam_bytes arguments;
};

typedef struct beam_module_spec {
    const char *name;
    beam_bytes bytes;
    /* Copied UTF-8 provenance label, never interpreted as an acquisition path.
     * NULL selects "memory". */
    const char *origin;
} beam_module_spec;

typedef struct beam_buffer {
    unsigned char *data;
    size_t size;
} beam_buffer;

/* Query the absolute location of the runtime image without initializing it.
 * This is libbeam.so for shared hosts and the executable for static hosts.
 * required receives the buffer size including NUL. NULL buffer with capacity
 * zero queries the size. An insufficient buffer returns BEAM_INVALID_ARGUMENT
 * and leaves buffer unchanged while reporting required. */
beam_status beam_library_path(char *buffer, size_t capacity, size_t *required,
                              beam_error *error);

typedef enum beam_result_kind {
    BEAM_RETURN,
    BEAM_EXCEPTION,
    BEAM_HALT,
    /* Explicit host-request completion, with a copied opaque value. */
    BEAM_COMPLETE
} beam_result_kind;

typedef struct beam_result {
    beam_result_kind kind;
    beam_buffer value;
    int halt_status;
} beam_result;

typedef enum beam_vm_state {
    BEAM_VM_RUNNING,
    BEAM_VM_HALT_REQUESTED,
    BEAM_VM_STOPPED,
    BEAM_VM_INITIALIZED
} beam_vm_state;

typedef struct beam_vm_state_info {
    beam_vm_state state;
    int halt_status;
} beam_vm_state_info;

typedef enum beam_terminal_cause {
    BEAM_TERMINAL_NONE,
    BEAM_TERMINAL_HALT,
    BEAM_TERMINAL_COMPONENT_FAILURE,
    BEAM_TERMINAL_HOST_REQUEST
} beam_terminal_cause;

/* Copied, VM-independent observation. Set struct_size exactly. Component is a
 * UTF-8 facility/module identity, not text parsed from failure.message. Cleanup
 * diagnostics never replace the winning terminal cause or a copied call result. */
typedef struct beam_vm_report {
    size_t struct_size;
    beam_terminal_cause cause;
    int halt_status;
    char component[1024];
    beam_error failure;
    int has_cleanup_error;
    beam_error cleanup;
} beam_vm_report;

/* Observe without executing Erlang or joining workers, including after stop.
 * The output is unchanged on error. Owner-thread only. */
beam_status beam_vm_get_report(beam_vm *vm, beam_vm_report *out);
/* Terminate with an optional final copied report, captured after native/code
 * reclamation and before the VM handle is released. On a returned cleanup
 * failure the handle remains owned and can be inspected/retried. */
beam_status beam_vm_terminate_report(beam_vm **vm, int *halt_status,
                                     beam_vm_report *out);

typedef struct beam_host_reply beam_host_reply;
typedef beam_status (*beam_host_handler)(void *context, beam_bytes request,
                                        beam_host_reply *reply);

/* Initialize one bare VM without starting workers or executing Erlang.
 * Options are copied. NULL selects the documented conservative defaults.
 * Modules and a host handler may be supplied before beam_vm_start.
 * The system can own only one VM handle, including a stopped handle.
 * Sequential VMs may use different explicit compositions and options.
 * Returned errors leave the output handle unchanged; validation failures
 * permit a corrected attempt.
 * Fatal failures within native initialization can still terminate the host. */
beam_status beam_vm_init(beam_system *system, const beam_vm_options *options,
                         beam_vm **out, beam_error *error);

/* Start an initialized VM's workers and required Erlang helpers. Subsequent
 * calls execute Erlang; background processes can run between calls. Starting
 * an already running or stopped VM is invalid. */
beam_status beam_vm_start(beam_vm *vm, beam_error *error);

/* Enter a process-owned VM through the same initialization and worker-start
 * mechanisms. The supplied root is the initial system process, held suspended
 * until selected components are ready. Returned validation/activation failures
 * retain the VM handle for inspection and termination. After the root is
 * admitted, process ownership is irrevocable and this operation never returns.
 * Halt exits the process; root failure retains fatal system-process semantics.
 * Host callbacks, if registered, run on the owner thread inside this operation. */
beam_status beam_vm_enter(beam_vm *vm, beam_error *error);

/* Stop if necessary, release VM-owned resources, and clear *vm on success.
 * The same system may then initialize a fresh VM with different options
 * and composition.
 * Other aliases to the terminated handle become invalid. Copied result
 * buffers remain valid. This operation is owner-thread only. A NULL *vm is
 * harmless cleanup and reports halt status zero; vm itself must be non-NULL. */
beam_status beam_vm_terminate(beam_vm **vm, int *halt_status);

/* Observe lifecycle without running Erlang or joining workers. Owner thread
 * only, including after stop. Output is unchanged on error. HALT_REQUESTED
 * includes workers that have quiesced but have not yet been joined. */
beam_status beam_vm_get_state(beam_vm *vm, beam_vm_state_info *out);

/* Select one optional handler before the first call is admitted to execution.
 * NULL clears it. Context is borrowed until stop completes. Calls freeze the
 * registration even if scheduler-side argument decoding subsequently fails.
 * Callbacks run serially on the owner thread only inside beam_vm_call, with
 * no VM locks held. They must return and must not reenter VM operations.
 * Background requests wait between calls; stop cancels pending requests. */
beam_status beam_vm_set_host_handler(beam_vm *vm, beam_host_handler handler,
                                     void *context);

/* Callback request bytes and the reply handle are borrowed only during the
 * callback. Supply exactly one uncompressed ETF term and return BEAM_OK.
 * This helper copies bytes immediately; full validation occurs on a scheduler.
 * A second copy is rejected without changing the first. No VM terms or host
 * allocations are borrowed after return. The handle must not be retained. */
beam_status beam_host_reply_copy(beam_host_reply *reply, beam_bytes value);
/* Select terminal completion instead of delivering a reply to Erlang. The
 * uncompressed ETF value is copied and validated before selection. After the
 * callback returns BEAM_OK, the runtime arbitrates under its mutex, stops the
 * VM, and returns BEAM_COMPLETE to the active call. Earlier recorded outcomes
 * win. Only the current reply may be used; this operation never reenters the VM.
 * halt_status must be nonnegative. Ordinary callbacks and calls are unchanged. */
beam_status beam_host_reply_complete(beam_host_reply *reply, beam_bytes value,
                                      int halt_status);

/* Load a complete BEAM module from copied memory before or after start.
 * Loading never executes Erlang. Replacement and on_load are unsupported.
 * A rejected module leaves existing code callable. */
beam_status beam_vm_load(beam_vm *vm, beam_bytes module);
typedef enum beam_module_flags {
    BEAM_MODULE_CURRENT = 1u << 0,
    BEAM_MODULE_OLD = 1u << 1,
    BEAM_MODULE_ON_LOAD = 1u << 2,
    BEAM_MODULE_PROTECTED = 1u << 3
} beam_module_flags;

typedef struct beam_module_info {
    size_t struct_size;
    unsigned flags;
} beam_module_info;

typedef struct beam_module_list {
    size_t count;
    const char *const *names;
} beam_module_list;

/* Module operations accept initialized or running VMs and never execute an
 * application entry process or host callback. Names are nonempty UTF-8 atom
 * names. Looking up an unknown name does not create an atom. Set struct_size
 * exactly; an absent module succeeds with zero flags. Output is unchanged on
 * error. Flags describe native code, including Erlang-side loader changes. */
beam_status beam_vm_get_module(beam_vm *vm, const char *name,
                               beam_module_info *out);

/* Copy a sorted snapshot of names with current, old, or pending code. Release
 * an earlier list before reusing its record. Copied lists outlive the VM and
 * system while libbeam remains loaded. Output is unchanged on error. */
beam_status beam_vm_list_modules(beam_vm *vm, beam_module_list *out);
void beam_module_list_release(beam_module_list *list);

/* Remove all current/old code without terminating processes. BEAM_BUSY leaves
 * the module unchanged when executable references or concurrent native work
 * prevent safe removal. Protected runtime modules and pending on_load are
 * unsupported; missing modules return BEAM_NOT_FOUND. Saved functions do not
 * pin code and may become uncallable. Ordinary data survives; literal storage
 * may be reclaimed asynchronously. Atom names can remain until VM termination.
 * Diagnostics are optional and never printed. */
beam_status beam_vm_unload(beam_vm *vm, const char *name, beam_error *error);

/* Arguments are one uncompressed ETF proper list, including the version byte.
 * Results contain ETF for a return value or {Class, Reason, Stacktrace}.
 * A requested halt stops the VM and returns BEAM_HALT with its status.
 * Calls require a started VM; they never implicitly start it.
 * Result storage belongs to the caller until beam_result_release(). */
beam_status beam_vm_call(beam_vm *vm, const char *module, const char *function,
                         beam_bytes arguments, beam_result *out);

/* Stop and join VM-owned activity. Idempotent on the owner thread; an
 * initialized VM can also be stopped without starting it. The handle remains
 * observable until termination, and cannot be started again.
 * Caller-owned result buffers remain valid after stop and termination. */
beam_status beam_vm_stop(beam_vm *vm, int *halt_status);
void beam_result_release(beam_result *result);

/* Returning boot is a copied, discardable preparation layered on the bare VM.
 * Preparation initializes nothing. Each run uses a fresh VM with its own
 * explicit composition.
 * A run retires every child before the caller observes the outcome. Outcomes
 * contain copied bytes only. */
typedef enum beam_return_service {
    BEAM_SERVICE_OUTPUT = 1u << 0,
    BEAM_SERVICE_CODE = 1u << 1,
    BEAM_SERVICE_ZLIB = 1u << 2,
    BEAM_SERVICE_FILE = 1u << 3,
    BEAM_SERVICE_SOCKET = 1u << 4,
    BEAM_SERVICE_PROGRAM = 1u << 5
} beam_return_service;

typedef enum beam_outcome_kind {
    BEAM_OUTCOME_RETURN = 0,
    BEAM_OUTCOME_EXCEPTION = 1,
    BEAM_OUTCOME_FINISH = 2,
    BEAM_OUTCOME_HALT = 3,
    BEAM_OUTCOME_SERVICE_FAILURE = 4
} beam_outcome_kind;

typedef struct beam_outcome {
    size_t struct_size;
    beam_outcome_kind kind;
    int halt_status;
    int has_cleanup_error;
    char cleanup_error[256];
    char service[1024];
    beam_buffer term;
} beam_outcome;

typedef enum beam_output_status {
    BEAM_OUTPUT_OK = 0,
    BEAM_OUTPUT_UNKNOWN = 1,
    BEAM_OUTPUT_NOT_READY = 2,
    BEAM_OUTPUT_BACKPRESSURE = 3,
    BEAM_OUTPUT_FAILED = 4
} beam_output_status;

typedef enum beam_code_status {
    BEAM_CODE_OK = 0,
    BEAM_CODE_NOT_FOUND = 1,
    BEAM_CODE_INVALID = 2,
    BEAM_CODE_UNAVAILABLE = 3
} beam_code_status;

typedef enum beam_input_status {
    BEAM_INPUT_LINE = 0,
    BEAM_INPUT_EOF = 1,
    /* No complete input is available yet. The caller may request input again;
     * this result is neither an empty line nor end of input. */
    BEAM_INPUT_PENDING = 2
} beam_input_status;

/* Handlers run on the owner thread inside the VM call, without VM locks.
 * They must return and must not call back into the VM. Pointers they return
 * stay valid until the handler returns; the library copies them. */
typedef beam_output_status (*beam_output_handler)(void *context,
                                                 beam_bytes destination,
                                                 beam_bytes bytes);
typedef beam_code_status (*beam_code_handler)(void *context, beam_bytes source,
                                             beam_bytes module,
                                             const unsigned char **bytes,
                                             size_t *size);
typedef beam_input_status (*beam_input_handler)(void *context,
                                               const unsigned char **bytes,
                                               size_t *size);

typedef struct beam_return_module {
    const char *name;
    beam_bytes bytes;
    /* Nonzero modules are activated by the controller, including reviewed
     * on_load. Zero modules are loaded before the entry and cannot use on_load. */
    int service;
    /* Optional copied source label for code provenance; NULL means "memory".
     * The VM does not interpret this label as a path or acquire its contents. */
    const char *origin;
} beam_return_module;

/* Fixed matching-build runtime units, not a third-party extension ABI. */
typedef enum beam_component_kind {
    BEAM_COMPONENT_FILE = 1,
    BEAM_COMPONENT_NETWORK = 2,
    BEAM_COMPONENT_ZLIB = 4
} beam_component_kind;

typedef struct beam_component_spec {
    size_t struct_size;
    beam_component_kind kind;
    const char *path;
} beam_component_spec;

/* A selected external NIF. Paths are absolute UTF-8; the matching SDK stamp
 * and initializer are validated before activation. Load information is undefined. */
/* Explicit matching-SDK binding to a supplied ordinary module. Preparation
 * copies both strings and retains the mapping through VM reclamation. Native
 * constructors may execute while mapping; load callbacks wait for activation. */
typedef struct beam_nif_spec {
    size_t struct_size;
    const char *module;
    const char *path;
} beam_nif_spec;

/* Already-linked native implementations. Initializers and returned tables must
 * remain resident until VM termination. Preparation copies descriptors and
 * strings but never calls initializers. They run at VM native-registry init;
 * NIF load callbacks retain ordinary load_nif timing. The public native SDK
 * defines enif_entry_t and erl_drv_entry; no VM-private types are exposed. */
struct enif_entry_t;
struct erl_drv_entry;
typedef struct beam_builtin_nif {
    size_t struct_size;
    const char *name;
    const char *build_id;
    struct enif_entry_t *(*initialize)(void);
    int taint;
} beam_builtin_nif;
typedef struct beam_builtin_driver {
    size_t struct_size;
    const char *name;
    const char *build_id;
    struct erl_drv_entry *(*initialize)(void);
    int taint;
} beam_builtin_driver;

/* A framework-neutral native composition. Service requests and activation
 * modules require ordinary beam_component_services, beam_host, beam_output
 * and beam_code inputs. Merely supplying these modules grants no facilities.
 * Modules and native configuration are copied; mappings remain retained. */
typedef struct beam_composition_options {
    size_t struct_size;
    size_t component_count;
    const beam_component_spec *components;
    /* Ordinary application and support modules. Preparation copies names, bytes and
     * origins and rejects conflicts with selected components or core code.
     * Initialization performs full code validation; on_load is not admitted. */
    size_t module_count;
    const beam_module_spec *modules;
    /* Keep ordinary application modules in the copied inventory without loading
     * them during VM initialization. The host explicitly loads their bytes;
     * explicitly selected native component code is still staged automatically.
     * Deferred service support must be loaded before beam_vm_activate. */
    int defer_modules;
    size_t nif_count;
    const beam_nif_spec *nifs;
    /* Additional host-backed facilities. Native bits require their components;
     * selected native components also imply their corresponding service bits. */
    unsigned services;
    /* Exact absolute UTF-8 executable path, required for PROGRAM. Copied and
     * validated at preparation; no helper starts until VM start. */
    const char *program_helper;
    /* Explicit reviewed modules activated after native facilities and before
     * entry admission. on_load is allowed here only, with failure rollback. */
    size_t activation_count;
    const beam_module_spec *activation_modules;
    /* Ordinary native loading and distribution are process-lifetime facilities.
     * VM initialization rejects these permissions in host-controlled execution.
     * PORTS permits a copied program_helper without BEAM service components. */
    unsigned native_admission;
    /* Explicit additional preloaded code, copied and always staged before
     * execution, even when ordinary modules are deferred. Names are reported
     * by erlang:pre_loaded() for this VM's lifetime (including framework
     * restarts). Core/component/ordinary-name conflicts and on_load are
     * rejected. This grants no native permissions. Sticky-code policy belongs
     * to the framework; the nine mandatory core modules remain unchanged. */
    size_t preload_count;
    const beam_module_spec *preloads;
    /* Fixed for this VM lifetime, including restarts. Explicit built-in NIFs
     * do not authorize unrelated dynamic loading. Drivers require process-owned
     * native driver admission. No hot registration is supported. */
    size_t builtin_nif_count;
    const beam_builtin_nif *builtin_nifs;
    size_t builtin_driver_count;
    const beam_builtin_driver *builtin_drivers;
} beam_composition_options;

typedef enum beam_component_state {
    BEAM_COMPONENT_ABSENT,
    BEAM_COMPONENT_PREPARED,
    BEAM_COMPONENT_ACTIVE,
    BEAM_COMPONENT_FAILED,
    BEAM_COMPONENT_STOPPED
} beam_component_state;

typedef struct beam_component_info {
    size_t struct_size;
    beam_component_state state;
    beam_error failure;
} beam_component_info;

beam_status beam_composition_prepare(const beam_composition_options *options,
                                     beam_composition **out, beam_error *error);
void beam_composition_release(beam_composition **composition);
/* Borrow the immutable initial code inventory, including explicitly selected component
 * modules. Valid until this preparation is released. This is not a live
 * VM code snapshot and excludes the nine mandatory core modules. */
beam_status beam_composition_get_modules(const beam_composition *composition,
                                         const beam_module_spec **modules, size_t *count);
/* Activate all selected facilities after start, before application calls.
 * A failed activation stops the VM; terminate it before retrying. */
beam_status beam_vm_activate(beam_vm *vm, beam_error *error);
beam_status beam_vm_get_component(beam_vm *vm, beam_component_kind kind,
                                  beam_component_info *out);

typedef enum beam_activation_state {
    BEAM_ACTIVATION_ABSENT,
    BEAM_ACTIVATION_PREPARED,
    BEAM_ACTIVATION_ACTIVE,
    BEAM_ACTIVATION_FAILED,
    BEAM_ACTIVATION_STOPPED
} beam_activation_state;

typedef struct beam_activation_info {
    size_t struct_size;
    beam_activation_state state;
    beam_error failure;
} beam_activation_info;

/* Observe aggregate activation without executing Erlang or joining workers.
 * No requested services, activation modules or NIFs means ABSENT, even if
 * support bytecode is supplied. FAILED retains its copied diagnostic after
 * stop; other requested work becomes STOPPED on shutdown. Exact facility or
 * module identity is available in beam_vm_get_report. Owner-thread only;
 * struct_size must match and output remains unchanged on error. */
beam_status beam_vm_get_activation(beam_vm *vm, beam_activation_info *out);

typedef struct beam_return_options {
    size_t struct_size;
    unsigned services;
    const char *entry_module;
    const char *entry_function;
    /* Uncompressed ETF proper list, including the version byte. Empty means []. */
    beam_bytes arguments;
    size_t module_count;
    const beam_return_module *modules;
    const char *bindir;
    beam_output_handler output;
    void *output_context;
    beam_code_handler code;
    void *code_context;
    beam_input_handler input;
    void *input_context;
    /* NULL selects conservative VM defaults with one normal scheduler. */
    const beam_vm_options *vm;
    /* Complete ordinary base, boot and services inventories are required.
     * FILE, NETWORK and ZLIB exactly match their service bits; zero native
     * components are valid when those capabilities are unselected.
     * Preparation retains each mapped unit and copies its module bytes without
     * starting a VM or activating native implementations. */
    size_t component_count;
    const beam_component_spec *components;
    /* Explicit dynamic NIF bindings, copied and admitted by composition. */
    size_t nif_count;
    const beam_nif_spec *nifs;
} beam_return_options;

typedef struct beam_return_boot beam_return_boot;

beam_status beam_return_prepare(const beam_return_options *options,
                                beam_return_boot **out, beam_error *error);
beam_status beam_return_discard(beam_return_boot **boot);
/* Initializes a VM when preparation is valid. Initialization failure leaves
 * *boot for retry without reserving a VM lifetime.
 * Successful initialization consumes *boot after the VM is terminated, including
 * when the entry itself fails. */
beam_status beam_return_run(beam_system *system, beam_return_boot **boot,
                            beam_outcome *out, beam_error *error);
void beam_outcome_release(beam_outcome *outcome);

/* Semantic counts of returning-boot native resources. They survive VM teardown
 * and are not cleared by the next initialization. */
int beam_zlib_stream_count(void);
int beam_file_descriptor_count(void);
int beam_socket_count(void);

#ifdef __cplusplus
}
#endif

#endif
