/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_RUNTIME_H
#define BEAM_RUNTIME_H

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
    BEAM_NATIVE_DISTRIBUTION = 1u << 3,
    /* Built-in ports with managed shutdown, including direct child reaping.
     * Does not authorize dynamic drivers, NIFs or distribution. */
    BEAM_NATIVE_MANAGED_PORTS = 1u << 4
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
    /* Optional immutable composition preparation, retained by initialization. */
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

typedef enum beam_system_owner {
    BEAM_SYSTEM_MAIN_THREAD = 0,
    BEAM_SYSTEM_CALLING_THREAD = 1
} beam_system_owner;
typedef struct beam_system_options {
    size_t struct_size;
    beam_system_owner owner;
} beam_system_options;
void beam_system_options_init(beam_system_options *options);
/* CALLING_THREAD is opt-in host-controlled execution only. The selected OS
 * thread owns the complete system lifetime; ownership cannot be transferred. */
beam_status beam_init_with_options(const beam_system_options *options,
                                   beam_system **out, beam_error *error);

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
 * Android may return an absolute APK archive-member location (archive!/member).
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
    BEAM_TERMINAL_FAILURE,
    BEAM_TERMINAL_HOST_REQUEST
} beam_terminal_cause;

/* Copied, VM-independent observation. Set struct_size exactly. Origin is a
 * UTF-8 facility/module identity, not text parsed from failure.message. Cleanup
 * diagnostics never replace the winning terminal cause or a copied call result. */
typedef struct beam_vm_report {
    size_t struct_size;
    beam_terminal_cause cause;
    int halt_status;
    char origin[1024];
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
 * an already running or stopped VM is invalid. A process-owned VM keeps its
 * root suspended: synchronous setup calls and explicit bindings may follow;
 * process-lifetime permissions remain denied until beam_vm_enter commits. */
beam_status beam_vm_start(beam_vm *vm, beam_error *error);

/* Enter a process-owned VM through the same initialization and worker-start
 * mechanisms, or commit an already started process-owned VM after synchronous
 * setup and all explicit bindings. Asynchronous call/pump APIs are excluded.
 * The supplied root is the initial system process, held suspended until commit.
 * Returned validation/binding failures
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

/* Select one optional handler before the first call or callback is admitted.
 * NULL clears it. Context is borrowed until stop completes. Calls freeze the
 * registration even if scheduler-side argument decoding subsequently fails.
 * Callbacks run serially on the owner thread in beam_vm_call, beam_vm_pump or
 * process-owned beam_vm_enter, with no VM locks held. They must return and must
 * not reenter VM operations. Idle requests wait for owner servicing;
 * stop cancels pending requests. */
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

/* One asynchronous call slot per VM. Begin copies all inputs; a second begin
 * is BEAM_BUSY until take consumes the previous result. Take returns BEAM_BUSY
 * while pending (including terminal requests until stop joins workers), and
 * BEAM_INVALID_STATE without a slot. A consumed result outlives the VM.
 * Stop preserves a recorded outcome, otherwise completes with BEAM_STOPPED. */
beam_status beam_vm_call_begin(beam_vm *vm, const char *module,
                               const char *function, beam_bytes arguments);
beam_status beam_vm_call_take(beam_vm *vm, beam_result *out);
typedef struct beam_pump_result {
    size_t struct_size;
    unsigned callbacks;
    int call_ready;
    int stop_requested;
} beam_pump_result;
/* Owner-thread only. Wait at most timeout_ms, invoking at most max_callbacks
 * (which must be positive). Zero timeout never waits. Callback execution itself
 * is cooperative and cannot be preempted. Reports terminal state without joining
 * workers. Callback requests are serviced even between calls. */
beam_status beam_vm_pump(beam_vm *vm, unsigned timeout_ms,
                         unsigned max_callbacks, beam_pump_result *out);
/* Thread-safe wake only. The caller must synchronize VM lifetime with wake:
 * no wake may overlap termination. All other VM operations remain owner-only. */
beam_status beam_vm_wake(beam_vm *vm);

/* Stop and join VM-owned activity. Idempotent on the owner thread; an
 * initialized VM can also be stopped without starting it. The handle remains
 * observable until termination, and cannot be started again.
 * Caller-owned result buffers remain valid after stop and termination. */
beam_status beam_vm_stop(beam_vm *vm, int *halt_status);
void beam_result_release(beam_result *result);

/* Already-linked native implementations. Initializers and returned tables must
 * remain resident until VM termination. Preparation copies descriptors and
 * strings but never calls initializers. They run at VM native-registry init;
 * NIF load callbacks retain ordinary load_nif timing. The public native SDK
 * defines enif_entry_t and erl_drv_entry; no VM-private types are exposed. */
struct enif_entry_t;
struct erl_drv_entry;
/* Preparation retains owners before using native pointers. Final release
 * follows all VM native reclamation, including failed initialization. Callbacks
 * must not reenter the VM; their code/context must survive final release. */
typedef struct beam_native_owner {
    size_t struct_size;
    void *context;
    void (*retain)(void *context);
    void (*release)(void *context);
} beam_native_owner;

/* The platform entry ABI differs from a normalized resident accessor on
 * Windows. The VM supplies the platform native callback table. */
#if defined(_WIN32)
struct beam_nif_callbacks;
typedef struct enif_entry_t *(*beam_native_nif_initializer)(struct beam_nif_callbacks *);
#else
typedef struct enif_entry_t *(*beam_native_nif_initializer)(void);
#endif
typedef struct beam_builtin_nif {
    size_t struct_size;
    const char *name;
    const char *build_id;
    struct enif_entry_t *(*initialize)(void);
    int taint;
} beam_builtin_nif;
/* Explicit bindings supplied after acquisition. Exactly one initializer is
 * required. Registration grants no ordinary dynamic loading permission. */
typedef struct beam_nif_binding {
    size_t struct_size;
    const char *module;
    const char *build_id;
    struct enif_entry_t *(*initialize)(void);
    beam_native_nif_initializer native_initialize;
} beam_nif_binding;

typedef struct beam_builtin_driver {
    size_t struct_size;
    const char *name;
    const char *build_id;
    struct erl_drv_entry *(*initialize)(void);
    int taint;
} beam_builtin_driver;

/* Supplemental product build facts for system_info(embedded_3pps). These do not
 * load code or grant native permissions. VM-owned names cannot be overridden. */
typedef enum beam_embedded_dependency_state {
    BEAM_EMBEDDED_INCLUDED = 0,
    BEAM_EMBEDDED_EXCLUDED = 1
} beam_embedded_dependency_state;

typedef struct beam_embedded_dependency {
    size_t struct_size;
    const char *name;
    beam_embedded_dependency_state state;
} beam_embedded_dependency;

/* Immutable VM inputs. Preparation copies module bytes, names and origins,
 * validates conflicts, and retains native owners before storing pointers.
 * No library acquisition or Erlang/native load initialization takes place. */
typedef struct beam_composition_options {
    size_t struct_size;
    /* Initialization loads ordinary code and rejects on_load. */
    size_t module_count;
    const beam_module_spec *modules;
    /* Copy ordinary code without loading it at initialization. The host may
     * load these bytes explicitly before or after start. */
    int defer_modules;
    /* Absolute executable helper path, independent of supplied modules.
     * Requires PORTS or MANAGED_PORTS admission. Copied and validated. */
    const char *program_helper;
    /* Ordinary native permissions require process ownership and take effect
     * at entry commit. MANAGED_PORTS permits returning program execution. */
    unsigned native_admission;
    /* Additional preloads always load at initialization and survive framework
     * restarts. Core and ordinary module conflicts and on_load are rejected. */
    size_t preload_count;
    const beam_module_spec *preloads;
    /* Resident tables are obtained during VM initialization. NIF load callbacks
     * run only when the owning module calls erlang:load_nif/2. Executable code
     * without an owner must remain resident until VM termination. */
    size_t builtin_nif_count;
    const beam_builtin_nif *builtin_nifs;
    size_t builtin_driver_count;
    const beam_builtin_driver *builtin_drivers;
    size_t binding_count;
    const beam_nif_binding *bindings;
    size_t native_owner_count;
    const beam_native_owner *native_owners;
    /* At most 1024 unique nonempty UTF-8 names, each at most 255 characters.
     * Preparation copies entries without creating atoms. Facts survive restart
     * and remain fixed until the VM releases its retained preparation. */
    size_t embedded_dependency_count;
    const beam_embedded_dependency *embedded_dependencies;
} beam_composition_options;

beam_status beam_composition_prepare(const beam_composition_options *options,
                                     beam_composition **out, beam_error *error);
void beam_composition_release(beam_composition **composition);
/* Borrow the immutable initial code inventory. Valid until this preparation is released. This is not a live
 * VM code snapshot and excludes the nine mandatory core modules. */
beam_status beam_composition_get_modules(const beam_composition *composition,
                                         const beam_module_spec **modules, size_t *count);
/* Bind one predeclared explicit native implementation. Success is idempotent
 * within this VM, and supplies undefined load information. Validation errors
 * change nothing; a native initialization failure stops the VM. */
beam_status beam_vm_bind_nif(beam_vm *vm, const char *module, beam_error *error);

#ifdef __cplusplus
}
#endif
#endif
