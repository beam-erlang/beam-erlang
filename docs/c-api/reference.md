# Public C API reference

Candidate reference for the current experimental SDK.

This reference describes the experimental Linux interface. `<beam/beam.h>`
exposes `<beam/runtime.h>`; optional returning-boot declarations require
`<beam/boot.h>` and linkage with `BEAM::Host`.
The [source header](../../include/beam/beam.h) supplies VM declarations;
[Embedding BEAM from C](guide.md) supplies a working introduction. Interpreter
and JIT expose the same API. Use matching installed headers and libraries:
there is no source/binary compatibility promise, and every `struct_size` must
match exactly rather than provide forward-compatible minimum sizing.

Use this reference by topic:

- [Common contracts and status](#common-contracts)
- [Discovery and configuration](#discovery-and-configuration)
- [Composition](#composition)
- [System and VM lifecycle](#system-and-vm-lifecycle)
- [Loading, inspection and scheduler control](#loading-inspection-and-scheduler-control)
- [Calling and results](#calling-and-results)
- [Host services](#host-services)
- [Returning BEAM boot](#returning-beam-boot)
- [SDK and service boundaries](#sdk-and-service-boundaries)

## Common contracts

`beam_init` selects the host's main thread; `beam_init_with_options` can select a persistent calling OS thread for host-controlled execution. The selected thread owns every VM
under it; system/VM operations are serial and reject callback reentry. There is no
cross-thread stop, concurrent observation, call cancellation or pause/resume.
One system owns one VM handle, including initialized/stopped handles. Terminating
the VM permits a fresh composition; terminating the system does not permit system
reinitialization or shared-library unloading.

`beam_system`, `beam_vm`, `beam_composition`, `beam_return_boot` and
`beam_host_reply` are opaque. Do not allocate, copy or free their contents.
Initialize owning handle variables to NULL. Termination invalidates aliases;
a copied handle does not retain ownership.

| Value | Ownership |
| --- | --- |
| `beam_bytes { const unsigned char *data; size_t size; }` | Input/borrowed view, measured in bytes; no terminating NUL requirement. |
| `beam_buffer { unsigned char *data; size_t size; }` | Library-allocated copied output, released through its enclosing result API. |
| VM options, module bytes/names/origins, call inputs | Copied/consumed by the operation; caller storage may be released after return. Composition mappings are retained separately. |
| Result buffers and module-name snapshots | Caller-owned until their release function; survive VM/system termination while the library stays loaded. |
| Callback context | Borrowed host state, kept valid through stop/run completion. |
| Callback request and reply handle | Borrowed only for that invocation; copy request bytes to retain them. |
| Diagnostic/observation records | Caller-owned values without separate release obligations. |
| Version/status strings | Borrowed immutable library storage. |

Never shallow-copy an owning result and release both copies. No movable Erlang
heap pointer crosses this interface. Copied bytes do not preserve the liveness
of encoded PIDs, references, funs or native resources.

### Status and diagnostics

API status and Erlang outcome are separate. Returned errors print nothing;
internal native allocation, initialization, worker-start or invariant failures
can still terminate the process. The API does not promise universal rollback.

| Status | Value | Meaning |
| --- | ---: | --- |
| `BEAM_OK` | 0 | C operation succeeded; inspect any Erlang outcome separately. |
| `BEAM_INVALID_ARGUMENT` | 1 | Invalid pointer, size, option, string or handle. |
| `BEAM_OUT_OF_MEMORY` | 2 | Recoverable allocation failure. |
| `BEAM_RUNTIME_NOT_FOUND` | 3 | Required runtime asset/helper is absent or inaccessible. |
| `BEAM_SYSTEM_ERROR` | 4 | OS or runtime preparation/execution failure. |
| `BEAM_ALREADY_STARTED` | 5 | System initialization claimed, or VM slot occupied. |
| `BEAM_STOPPED` | 6 | VM halt/stop prevents admission. |
| `BEAM_WRONG_THREAD` | 7 | Selected owner thread required. |
| `BEAM_INVALID_BEAM` | 8 | Invalid/incompatible compiled module. |
| `BEAM_INVALID_TERM` | 9 | ETF cannot be accepted or a result cannot be encoded. |
| `BEAM_UNSUPPORTED` | 10 | Unsupported operation/configuration. |
| `BEAM_ALREADY_LOADED` | 11 | Module replacement is unavailable. |
| `BEAM_INVALID_STATE` | 12 | Lifecycle or callback reentry disallows the operation. |
| `BEAM_NOT_FOUND` | 13 | Module to unload has no code. |
| `BEAM_BUSY` | 14 | Code or native coordination is in use; retry after it settles. |

Functions declaring an optional `beam_error *` clear that record on entry.
It contains `int native_code` and NUL-terminated `char message[256]`.
Native code is zero when inapplicable; invalid options name the offending field.
Branch on status, not diagnostic text, and do not reuse an older operation's
error record as a new failure explanation.

`beam_build_id()` borrows the opaque identity of the loaded runtime build. It
performs no filesystem access or VM initialization and is available before
`beam_init`. The identity is a matching-build check, not a stable ABI version.

`beam_version()` returns `BEAM/Erlang <release> (erts-<version>)`.
`beam_status_message(status)` returns a general description or `"unknown error"`.
Neither starts a VM. `beam_core_modules(&names, &count)` borrows the immutable
nine-module inventory for the loaded library's lifetime. The pure
`beam_system_is_initialized()` query observes process admission, not a reservation.

`beam_library_path(buffer, capacity, &required, &error)` reports the absolute
containing runtime image: the shared library or statically linked executable.
It neither initializes the VM nor chooses an installation layout.
NULL buffer/zero capacity queries the required size including NUL. An insufficient
buffer returns `BEAM_INVALID_ARGUMENT`, leaves it unchanged and reports the size.

## Discovery and configuration

`beam_query_host(&info, &error)` fills an exact-size `beam_host_info` without
initialization, workers or resource reservation. It is repeatable and needs no
system/owner handle. Output is unchanged on error; missing individual data is
zero, not a failed query. Invalid output/size returns `BEAM_INVALID_ARGUMENT`;
recoverable allocation failure returns `BEAM_OUT_OF_MEMORY`.

| CPU field | Meaning |
| --- | --- |
| `configured_cpus` | OS-configured CPUs. |
| `online_cpus` | OS-online CPUs. |
| `available_cpus` | Main-thread Linux affinity availability, falling back to online count. |
| `quota_cpus` | Whole-number quota hint, capped by online count; positive fractions truncate with minimum one. It does not additionally apply affinity. |

Discovery chooses no scheduler policy. Call `beam_vm_options_init(&options)`
to populate all defaults; NULL is harmless. Passing NULL options to initialization
selects those defaults. The current 64-bit implementation accepts:

| VM option | Default | Contract |
| --- | --- | --- |
| `struct_size` | Exact structure size | Required exact match. |
| `schedulers` | 1 | Normal capacity, 1–1024. |
| `schedulers_online` | 1 | 1 through normal capacity. |
| `dirty_cpu_schedulers` | 1 | 1 through normal capacity. |
| `dirty_cpu_schedulers_online` | 1 | At least 1, no greater than dirty CPU capacity or normal online count. |
| `dirty_io_schedulers` | 1 | 1–1024. |
| `async_threads` | 1 | 0–1024; unlike scheduler/poll counts, zero is allowed. |
| `poll_threads` | 1 | 1–1024. |
| `process_limit` | 1048576 | 1024–134217727; native capacity rounds up to a power of two, capped at the maximum usable size. |
| `atom_limit` | 1048576 | 8192–2147483647. |
| `min_heap_size` | 233 words | Positive per-process minimum, rounded to a supported heap size fitting `INT_MAX`. |
| `min_bin_vheap_size` | 32768 words | Positive binary virtual-heap threshold, independently rounded. |
| `max_heap_size` | 0 | Per-process maximum; zero disables it, otherwise at least rounded minimum heap and at most the native small-integer maximum. |
| `composition` | NULL | Immutable preparation retained by successful initialization. |
| `execution` | `BEAM_HOST_CONTROLLED` | Alternatively `BEAM_PROCESS_OWNED`. |
| `initial_root` | NULL | Required only for process-owned mode; copied `beam_entry` with module/function and ETF proper-list arguments. |
| `startup_context` | Empty | Optional copied uncompressed ETF term; empty means `undefined`. |
| `process_policy` | 0 | Explicit process-owned signal/stdio/diagnostic bits. |
| `program_name` | NULL | Copied diagnostic identity; NULL uses `libbeam`. |
| `allocator_tagging` | `BEAM_ALLOCATOR_TAGGING_DEFAULT` | Preserve mixed allocator defaults, or explicitly select `BEAM_ALLOCATOR_TAGGING_ENABLED` / `BEAM_ALLOCATOR_TAGGING_DISABLED`. Invalid values fail before VM initialization. |
| `port_limit` | Native default | At most `INT_MAX`; native tables round/clamp requested capacity. |
| `port_limit_ignore_files` | 0 | Port capacity policy flag. |
| `ets_limit` | 0 | ETS sizing hint. |
| `fullsweep_after` | 65535 | Default generational-GC count, 0–65535. |
| `filename_encoding` | `BEAM_FILENAME_UTF8` | Alternatively `BEAM_FILENAME_LATIN1`. |
| `break_policy` | `BEAM_BREAK_DEFAULT` | Process-owned interrupt policy below. |

Counts describe configured roles, not every OS helper thread. Schedulers start
unbound; CPU binding is not public configuration. No automatic scaling to CPU
availability occurs. Process/atom limits include runtime objects and reserve
no guaranteed memory. Heap words are eight bytes on supported 64-bit builds;
32768 rounds to 46422, while 233 is already supported. The largest accepted
minimum is 2115685508 words; the maximum heap upper bound is 576460752303423487.
The binary threshold is accounting, not an eagerly allocated buffer. Maximum
heap need not exceed that threshold and is not a VM-wide memory budget.
These bounds are implementation limits, not future ABI promises.

Allocator tagging applies to binary, standard, ETS, process-heap, long-lived,
driver, fixed-size and short-lived allocators. Temporary, literal and test
allocators keep their defaults. Selection belongs to the VM lifetime; a fresh
VM may choose differently, while OTP `init:restart()` retains the selection.
There is no environment parsing or per-allocator tuning in the public API.
Consumers must rebuild for the matching SDK; options use an exact structure
size check.

Embedding ignores emulator-tuning environment variables. Allocator strategies,
reader/counter groups and internal thread coordination remain derived settings.
OTP derives filename policy from its host locale without changing locale during
VM initialization. There is no runtime-family field or system family lock.

### Process policy and context

`startup_context` must encode exactly one uncompressed ETF term with its version
byte, without trailing data. Initialization validates/copies it before execution.
`erts_internal:vm_context/0` decodes a fresh value per call; immutable bytes survive
framework restart. No context yields `undefined`.

`BEAM_PROCESS_SIGNALS`, `BEAM_PROCESS_STDIO` and `BEAM_PROCESS_DIAGNOSTICS`
require process-owned execution. Effects begin at explicit entry, not initialization.
The copied break policy selects:

| Policy | Effect with process signal ownership |
| --- | --- |
| `BEAM_BREAK_DEFAULT` | Install the default break handler. |
| `BEAM_BREAK_DISABLED` | Leave the break handler uninstalled. |
| `BEAM_BREAK_IGNORE` | Ignore terminal interrupt, quit and suspend signals. |
| `BEAM_BREAK_INPUT` | Keep the handler but make the interrupt character available as terminal input; also requires STDIO ownership. |

Every non-default break policy requires SIGNALS. Normal process exit restores
managed terminal settings. Process-wide policy and native admission do not add
returning teardown of OTP.

## Composition

`beam_composition_prepare(&options, &composition, &error)` copies supplied code,
names, origins and native registrations. It creates no VM, maps no image and
runs no native initializer. The output must initially be NULL and `struct_size`
must match exactly. Failure leaves the output unchanged. Structural validation
happens during preparation; the VM loader performs complete bytecode validation.

| Composition field | Contract |
| --- | --- |
| `module_count`, `modules` | Up to 4096 ordinary `beam_module_spec` inputs. |
| `defer_modules` | Retain ordinary bytes for the host to load explicitly before use. |
| `preload_count`, `preloads` | Up to 4096 additional preloads, staged even when ordinary modules are deferred. |
| `builtin_nif_count`, `builtin_nifs` | Up to 1024 resident NIF registrations. Table access occurs at VM initialization; load callbacks occur at ordinary `erlang:load_nif/2`. |
| `builtin_driver_count`, `builtin_drivers` | Up to 1024 resident driver registrations; requires driver admission. |
| `binding_count`, `bindings` | Up to 1024 explicit `beam_nif_binding` declarations, bound individually after start. |
| `native_owner_count`, `native_owners` | Opaque retain/release records keeping native pointers valid through reclamation. |
| `program_helper` | Copied absolute executable path for admitted ports/programs; preparation does not launch it. |
| `native_admission` | Explicit NIF, driver, port, distribution or managed-port permissions. |

`beam_module_spec` contains `name`, complete `bytes` and optional UTF-8 `origin`.
NULL origin becomes `memory`. Names contain at most 255 Unicode characters;
origins are diagnostic labels, never acquisition paths. Duplicate names across
ordinary modules and preloads, conflicts with the nine core modules, and
conflicting resident/explicit native registrations are rejected. Generic
composition requires no product support modules or named facilities.

`beam_builtin_nif` and `beam_builtin_driver` carry exact sizes, names, SDK build
identities, typed table accessors and taint flags. Their code must remain resident
through native-resource destruction, unload callbacks and registry cleanup.
Executable-resident registrations may use a borrowed lifetime; acquired code
supplies a `beam_native_owner` with context and retain/release functions.
Preparation retains owners before storing native pointers. A VM retains its
preparation, allowing the caller to release its own preparation reference early.

`beam_nif_binding` selects a module, exact SDK identity and either a normalized
resident accessor (`initialize`) or a platform initializer (`native_initialize`).
Exactly one initializer is supplied. The platform form preserves Windows callback
table injection. After start, `beam_vm_bind_nif(vm, module, &error)` invokes that
initializer and binds the declared module with `undefined` load information.
Successful repeated binding is idempotent. Invalid arguments, an undeclared
binding or missing code leave state unchanged. Failure after native initialization
begins stops the VM, retains diagnostics and requires termination before retry.
Ordinary reload/rebinding is unsupported for this route.

`beam_composition_get_modules` borrows the copied initial inventory;
`beam_composition_release` drops the caller's reference and clears its handle.
Preloads remain distinct from ordinary modules and retain their preload semantics.
See [native loading](../native-extensions/loading.md) for initializer timing,
image admission and the reviewed deferred-module policy.

### Optional libraries and startup

Include `<beam/library.h>` and link `BEAM::Host` for passive shared/static library
acquisition. Descriptors carry modules and resident registrations, with no startup
hooks, fixed kinds or dependency discovery. See [the library format](../native-extensions/libraries.md).

`<beam/host.h>` supplies optional orchestration:

| Operation | Contract |
| --- | --- |
| `beam_host_prepare` | Copy a `beam_host_options` plan and merge its generic composition, library inputs, explicit startup actions and deferred modules. Starts nothing. |
| `beam_host_composition` | Borrow the prepared generic composition until Host release. VM initialization independently retains it. |
| `beam_host_start` | On an already started VM, run the requested startup calls and complete explicit bindings. Failure stops the VM. Call once per VM lifetime. |
| `beam_host_release` | Release copied policy and the caller's composition reference. |
| `beam_host_open_nif` | Admit/map an explicit NIF image and obtain its typed binding and owner, without calling its initializer. |

A `beam_startup_action` supplies module/function names and an ETF argument list;
an empty span means `[]`. Startup actions must return `ok`. Actions and reviewed
deferred loading require explicitly supplied `beam_startup` bytecode. No unrelated
output or acquisition module is required. Library descriptors never choose these
actions. Raw embedders may implement the same ordering with public VM calls.

## System and VM lifecycle

| Operation | Contract |
| --- | --- |
| `beam_init(&system, &error)` | Main-thread system initialization, no workers/Erlang. Output unchanged on returned error. A successfully claimed system cannot be initialized again. |
| `beam_vm_init(system, options, &vm, &error)` | Copy options and construct initialized native/code state without workers/Erlang. Output unchanged on returned error. Existing VM handle returns `BEAM_ALREADY_STARTED`. |
| `beam_vm_start(vm, &error)` | Publish code and start workers. A process-owned root remains suspended for synchronous setup. Starting twice or after stop is invalid. |
| `beam_vm_enter(vm, &error)` | Commit a prepared process-owned VM, or start and enter an initialized one. Once committed it never returns. |
| `beam_vm_stop(vm, &halt_status)` | Close admission, cancel pending requests, terminate processes, drain cleanup and join workers. Cooperative, idempotent; also stops an unstarted VM. |
| `beam_vm_terminate(&vm, &halt_status)` | Stop if needed, reclaim VM resources and clear handle, permitting fresh initialization. |
| `beam_terminate(&system)` | Release system infrastructure only after its VM handle is gone, then clear handle. |

Initialization loads the [nine core modules](guide.md#choose-a-composed-lifecycle),
plus explicit composition code. It acquires no boot script, application tree,
compiler, automatic loader or I/O server. Unconfigured bare VMs reject ordinary
NIF/driver loading, ports, distribution and direct debug output (`display`,
`display_string`, `process_display`); explicit facilities follow composition rules.
No default logger or host-stderr fallback is installed.

VM initialization validation permits a corrected attempt. Native construction
can still fail fatally. Recoverable allocation/synchronization errors return
OUT_OF_MEMORY/SYSTEM_ERROR. Incompatible host fault handlers or exhausted
system-local creation numbers return UNSUPPORTED. Each successful VM gets a
distinct creation number so old copied PID/reference identities cannot alias
new local identities; exhaustion never wraps.

Stop retains storage and an observable handle, which still occupies the system's
VM slot. Successful termination invalidates aliases. NULL pointer-to-handle
arguments are invalid; NULL handle values permit repeated cleanup, with VM halt
status zero. Returned cleanup failure retains the handle for inspection/retry.
System termination with any VM handle returns `BEAM_INVALID_STATE`; infrastructure
cleanup failure returns `BEAM_SYSTEM_ERROR` without clearing the handle.

Fresh VMs inherit no prior modules, dynamic atoms, processes, timers, callbacks
or halt state. A process-owned root uses the same initialization mechanisms as
host-controlled execution. It is the first system process, suspended until
startup finishes. Before entry commit, synchronous calls and explicit native
binding are permitted; ordinary process-lifetime native permissions remain
denied. Asynchronous call/pump extensions are host-controlled only. Entry rejects
pending work and unfinished bindings. Process policy is applied at commit on
the owner thread, with failure arbitration synchronized against entry. Returned
validation/startup errors retain an inspectable handle; after admission halt
exits the OS process and root failure is fatal.
Host callbacks run on the owner thread inside `beam_vm_enter`.

### States and permitted operations

`beam_vm_get_state(vm, &info)` copies `state` and `halt_status`, without Erlang
execution, callbacks or joining. Output remains unchanged on error. Zero status
means no accepted halt or a host-requested stop; the first accepted halt status
is retained. Termination leaves no queryable handle.

| State | Meaning |
| --- | --- |
| `BEAM_VM_INITIALIZED` | Native state/code exists; workers have not started. |
| `BEAM_VM_RUNNING` | Workers started; no terminal halt accepted. |
| `BEAM_VM_HALT_REQUESTED` | Halt accepted; owner shutdown/join incomplete, even if workers quiesced. |
| `BEAM_VM_STOPPED` | Workers joined; handle/storage retained. |

| Operation | Initialized | Running | Halt requested / stopped |
| --- | --- | --- | --- |
| State/scheduler/report query | Observe | Observe | Observe only. |
| Module query/list/unload | Native operation | Native operation | `BEAM_STOPPED`. |
| Load | Load | Load | `BEAM_STOPPED`; halted load completes joining. |
| Call | `BEAM_INVALID_STATE` | Execute after readiness | `BEAM_STOPPED`; halted call completes joining. |
| Scheduler online change | `BEAM_INVALID_STATE` | Change pair | `BEAM_STOPPED`. |
| Handler registration | Register/clear | Until first admitted call | `BEAM_STOPPED`. |
| Start | Start workers, suspending a process-owned root | `BEAM_INVALID_STATE` | `BEAM_INVALID_STATE`. |
| Stop | Stop without starting | Stop/join | Finish join or idempotent success. |
| Terminate | Release | Stop/release | Finish cleanup/release. |

The table assumes valid arguments on the owner thread outside callbacks.
Background activity can change state between observation and admission.

### Reports and host state

`beam_vm_get_report(vm, &report)` observes an exact-size copied `beam_vm_report`
without execution/joining, including after stop. `cause` is NONE, HALT,
FAILURE or HOST_REQUEST with prefix `BEAM_TERMINAL_`; `halt_status`,
`origin` and `failure` describe the winning terminal cause.
`has_cleanup_error`/`cleanup` remain separate. The origin string is an identity,
not text parsed from a diagnostic. Output is unchanged on error.

Native callbacks and library monitors can call `beam_native_request_failure`
with copied origin/reason text. It requests coordinated shutdown without waiting
for host callbacks. `beam_native_report_cleanup` records the first cleanup
diagnostic separately; `beam_native_stop_requested` remains usable during
quiescent resource destruction. These operations are declared in
`<beam/native.h>` and do not authorize calls from threads outliving their VM.

`beam_vm_terminate_report(&vm, &status, &report)` has termination semantics and
optionally captures the final report after native/code reclamation. Reports
survive VM/system termination. Cleanup failure retains the VM for retry. A
previously copied call result remains valid even if a later terminal event stops
the VM; the report records that terminal event separately.

Host-controlled lifetimes preserve signal dispositions, standard descriptors,
terminal, environment and floating-point state. VM workers block asynchronous
host signals and own alternate stacks. Native-stack JIT requires custom handlers
for synchronous faults (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGTRAP`,
`SIGSYS` where present) to use `SA_ONSTACK`. Initialization and start reject
incompatible handlers rather than rewriting them; maintain the policy while live.
Explicit process-owned policy is the separate route for process-wide effects.

## Loading, inspection and scheduler control

`beam_vm_load(vm, module)` accepts the complete unpacked BEAM container from
copied memory. Data must be non-NULL, nonempty and representable by the native
byte-count type. Loading before or after start executes no Erlang and acquires
no dependencies; existing background execution can continue independently.
Malformed/incompatible code returns `BEAM_INVALID_BEAM`, `on_load` returns
`BEAM_UNSUPPORTED`, and existing code returns `BEAM_ALREADY_LOADED`. Rejection
leaves existing code callable. Unload successfully before loading a replacement.

`beam_vm_get_module(vm, name, &info)` fills exact-size `beam_module_info.flags`.
CURRENT, OLD, ON_LOAD and PROTECTED flags, prefixed `BEAM_MODULE_`, can combine.
An unknown valid name succeeds with zero flags and creates no atom. Names are
nonempty NUL-terminated UTF-8 of at most 255 Unicode characters. Output is
unchanged on errors; flags are observations, not code-retaining references.

`beam_vm_list_modules(vm, &list)` copies unique names having current, old or
pending code, sorted by UTF-8 byte order. Atoms without code are not listed.
Native module names that are empty or contain embedded NUL return
`BEAM_UNSUPPORTED` rather than being truncated. Initialize the list to `{0}`
and release an earlier snapshot before reuse. Output is unchanged on failure;
allocation failure returns `BEAM_OUT_OF_MEMORY`. `beam_module_list_release`
accepts NULL/empty values, frees the snapshot and clears it. Do not free or
modify individual names. The snapshot survives code changes and VM/system
termination while the library remains loaded.

`beam_vm_unload(vm, name, &error)` removes all current/old code only when safe
without terminating processes. It starts no workers before start, dispatches no
callbacks and does not freeze registration. Missing code returns NOT_FOUND;
core/protected, pending `on_load` and bound native code are unsupported. Active
code, uninspectable dirty work or conflicting code management returns BUSY and
leaves code callable. Coordinate application exit from that code before retry.
Unloading does not unregister processes, erase atoms/tables or reset application
state. Retained literal values survive while storage reclamation may be deferred.
A fun alone does not pin code and may become uncallable after removal.

`beam_vm_get_schedulers(vm, &info)` fills exact-size `beam_scheduler_info` with
normal/dirty CPU totals and online counts plus the dirty-I/O total. No Erlang,
callbacks, joining or handler-registration freeze occurs. Output is unchanged
on errors. Counts describe selected policy, not utilization or live threads;
before start/after stop they remain meaningful configuration. A transition can
precede worker acknowledgment; Erlang may subsequently change online counts.
Async/polling workers are not included as schedulers.

`beam_vm_set_schedulers_online(vm, normal, dirty_cpu, &error)` changes both
online counts together in a running VM. Positive counts must fit initialized
capacity and dirty CPU online must not exceed normal online. There is no scaling
or clamping; invalid input changes neither count. BEAM_OK waits for both groups
to acknowledge; an existing transition returns BEAM_BUSY. Halt retires pending
native work and returns BEAM_STOPPED. The operation pumps no callbacks and has
no timeout. Offline workers retain threads/resources; this does not resize
capacity, change dirty-I/O workers or pause the VM.

## Calling and results

`beam_vm_call(vm, module, function, arguments, &result)` invokes an export in a
new ordinary Erlang process after start/readiness. Module/function names follow
the UTF-8 atom-name rules above. Arguments must encode exactly one uncompressed
ETF proper list with its version byte, arity 0–255. `{131, 106}` encodes `[]`.
Improper lists, trailing data, compressed/internal-local/unsupported encodings
or a non-list term return `BEAM_INVALID_TERM`. Inputs are copied.

The owner thread blocks until entry return, exception, exit or terminal VM
action. Children can outlive a normal call. Missing modules/exports produce
Erlang `undef`, not a loading status. There is no timeout or asynchronous cancel.
The non-NULL output is cleared even on failure; release any earlier result
before reuse. Clearing does not free its old allocation. Inspect fields only
after BEAM_OK, which accompanies all four result kinds:

| `beam_result.kind` | `value` | `halt_status` |
| --- | --- | --- |
| `BEAM_RETURN` | One copied ETF value. | Zero. |
| `BEAM_EXCEPTION` | ETF `{Class, Reason, Stacktrace}`; external exits may have an empty stacktrace. | Zero. |
| `BEAM_HALT` | Empty buffer. | First accepted Erlang halt status. |
| `BEAM_COMPLETE` | Copied opaque ETF terminal host value. | Selected completion status. |

Ordinary integer `erlang:halt` returns to the host-controlled caller; its stored
status is masked with `INT_MAX`. A halt during an admitted call completes
shutdown and returns BEAM_HALT. A background halt before admission makes a
later call return BEAM_STOPPED; observe status or stop to complete cleanup.
Abort/crash-dump forms, disabled flushing and finite flush timeouts are rejected
with an Erlang exception. Process-owned halt instead exits the host process.

`beam_result_release(&result)` frees library-owned bytes and clears the record.
It accepts NULL/already cleared results and requires no live system/VM. Release
successful results even when ignoring the payload. Do not supply arbitrary
host buffers or release shallow copies twice.

## Host services

`beam_vm_set_host_handler(vm, handler, context)` registers one optional
`beam_host_handler(void *context, beam_bytes request, beam_host_reply *reply)`;
NULL clears it. Registration is allowed before the first call is admitted,
including while running. Admission freezes it even if scheduler-side ETF
decoding later fails; earlier host validation/allocation failure does not.
A frozen registration returns INVALID_STATE; halted/stopped VMs return STOPPED.

Load optional `beam_host` explicitly and call `beam_host:request(Request)`.
Registration alone loads no module or I/O server. `BEAM_HOST_MODULE` identifies
the installed bridge as a build input. Requests need not be lists. The handler
receives one copied ETF term and a reply handle borrowed only for its invocation.

Callbacks run serially on the owner thread, without VM locks: inside
`beam_vm_call` or `beam_vm_pump` for host-controlled execution, and inside `beam_vm_enter` for
process-owned execution. They must return and cannot reenter VM operations,
including state queries, or wait for another owner-thread callback. Reentry
returns INVALID_STATE; calling `beam_init` instead reports ALREADY_STARTED.
Another thread does not bypass these rules.

For host-controlled VMs, background requests are serviced by the next
`beam_vm_pump` or synchronous call, including when no asynchronous call is active. Load, observation and stop do not invoke handlers. Entry completion need
not service every child's request. Stop cancels pending work; requester exit or
halt can discard an admitted callback's reply without undoing its host effects.
Context storage remains valid until stop completes.

### Replies and terminal completion

`beam_host_reply_copy(reply, bytes)` copies one complete uncompressed ETF term
including its version byte; nonempty data must fit the native byte-count type.
Return BEAM_OK from the callback to select that reply. The handle must be the
current callback's reply; a second copy or use outside the callback returns
INVALID_STATE without replacing the first. Wrong-thread calls return WRONG_THREAD.
Invalid arguments or allocation can return INVALID_ARGUMENT/OUT_OF_MEMORY;
a failed copy does not mark the reply supplied.

BEAM_OK here means copied, not fully decoded. Scheduler-side validation later
rejects malformed/compressed/trailing/internal-local ETF. Source bytes can be
reused immediately after a successful copy. The bridge reports:

| Erlang result | Meaning |
| --- | --- |
| `{ok, Reply}` | Valid copied reply and successful callback; Reply may itself describe an application error. |
| `{error, no_handler}` | No registered callback. |
| `{error, no_reply}` | Callback succeeded without a reply. |
| `{error, invalid_reply}` | Copied bytes were not an acceptable term. |
| `{error, {callback_failed, Status}}` | Nonzero callback status as an integer. |
| `{error, out_of_memory}` / `{error, invalid_term}` | Recoverable request allocation/encoding failure. |
| `{error, stopped}` | Request admission closed. |

Shutdown can terminate the requester instead of delivering a tuple; delivery
after cancellation is not guaranteed.

`beam_host_reply_complete(reply, value, halt_status)` selects terminal completion
instead of an Erlang reply. It requires the current reply, a valid uncompressed
ETF term and nonnegative status. Unlike ordinary reply copy it validates before
selection; failure leaves the reply available for correction. The callback must
return BEAM_OK. After it returns, the VM arbitrates the request against earlier
outcomes, stops, and returns BEAM_COMPLETE to the active call if it wins. An
earlier outcome wins without callback reentry or cross-thread cancellation.

## Returning BEAM boot

The modern returning host uses public composition/lifecycle to manage one
complete application VM under a host-supplied system:

```c
beam_status beam_return_prepare(const beam_return_options *options,
                                beam_return_boot **out, beam_error *error);
beam_status beam_return_run(beam_system *system, beam_return_boot **boot,
                            beam_outcome *out, beam_error *error);
beam_status beam_return_discard(beam_return_boot **boot);
void beam_outcome_release(beam_outcome *outcome);
```

Preparation requires exact size and an initially NULL output. It copies inputs,
retains supplied library owners and starts nothing. Callback pointers/context remain
borrowed through the run. Run belongs to the system's owner thread and cannot
be called from a callback. Initialization failure leaves preparation available
for retry/discard; after successful initialization execution consumes it following
VM termination, including application failure. If final cleanup fails, preparation
retains its stopped VM and discard retries cleanup before release. Cleanup
diagnostics remain observable even if startup prevented an application outcome.

| Returning option | Contract |
| --- | --- |
| `struct_size` | Exact `sizeof(beam_return_options)`. |
| `host` | Optional `beam_host_options`: generic inputs, libraries, explicit startup actions and deferred modules. |
| `entry_module`, `entry_function` | Required export; argument list determines arity. |
| `arguments` | Uncompressed ETF proper list; empty bytes select `[]`. |
| `module_count`, `modules` | Copied `beam_return_module` entries: name, bytes, deferred flag and origin. |
| `output`, `output_context` | Host byte-output handler and borrowed state. |
| `code`, `code_context` | Later code-acquisition handler and borrowed state. |
| `input`, `input_context` | Input handler and borrowed state. |
| `vm` | Optional copied host-controlled options; no initial root, preexisting composition or process policy. NULL selects conservative defaults. |

Supply the ordinary returning boot modules and all code needed by the selected
startup actions and entry. A returning module with zero `deferred` loads before
start without `on_load`; nonzero selects reviewed deferred initialization in the
Host layer. Origins are copied labels. Conflicts follow generic composition
validation. Each preparation retains its own references.

Explicit startup must finish before application entry. Entry return, exception,
requested finish, halt or a reported terminal failure ends the VM, including
children. The first terminal cause wins; cleanup diagnostics remain separate.

Output/code/input callbacks follow owner-thread, copy and no-reentry rules.
`beam_output_handler(context, destination, bytes)` returns `beam_output_status`:
`BEAM_OUTPUT_OK`, UNKNOWN, NOT_READY, BACKPRESSURE or FAILED.
`beam_code_handler(context, source, module, &bytes, &size)` returns
`beam_code_status`: `BEAM_CODE_OK`, NOT_FOUND, INVALID or UNAVAILABLE;
acquisition is distinct from subsequent publication. Input returns
`beam_input_status` LINE, EOF or PENDING (each prefixed `BEAM_INPUT_`) with
bytes/size: PENDING is temporary lack of
input, distinct from an empty line and EOF. Returned byte pointers are copied
rather than retained as caller storage.

Zero-initialize `beam_outcome` and set `struct_size` to its exact size before
running. Release any previous outcome before reuse: run can overwrite the record
without freeing old storage, while early errors can leave it unchanged.
`beam_outcome_release` frees copied storage and clears the entire record,
including `struct_size`; restore that field before another run.

Successful run reports kind RETURN, EXCEPTION, FINISH, HALT or FAILURE
with prefix `BEAM_OUTCOME_`. `term` is copied ETF, `halt_status` carries halt
status, `origin` identifies the failure source, and
`has_cleanup_error`/`cleanup_error` preserve secondary cleanup failure. Run can
return a non-OK cleanup status while still assigning an owning outcome, or
supply cleanup diagnostics without an application outcome. Release the
initialized output even after a failed run; API failure does not prove its
copied storage is empty. API status and application outcome remain distinct.

Native resource counters belong to their respective libraries, not the VM API.
Acceptance also observes actual OS resources; counters alone do not prove cleanup.

## SDK and service boundaries

[SDK deployment](deployment.md) documents CMake targets, verified
artifacts, native headers and relative bundles. Deployment does not activate
services or infer code inventories from entry names. OTP owns its acquisition,
thirteen external preloads, commands and application policy through the generic
process-owned API; the SDK supplies no OTP boot adapter or catalog.

Output callbacks cannot report partial counts: a failed callback has zero
confirmed bytes and unknown progress. Admitted callbacks must finish even when
a requesting service has a deadline. File cleanup failures
populate copied VM cleanup diagnostics and returning outcomes without replacing
the primary cause.

## Asynchronous host control

| Entry | Contract |
| --- | --- |
| `beam_system_options_init` | Set exact structure size and default main-thread ownership. |
| `beam_init_with_options` | `BEAM_SYSTEM_CALLING_THREAD` opts into dedicated ownership; initialization and all later VM operations/cleanup use that OS thread. Process-owned VMs are rejected on this route. |
| `beam_vm_call_begin` | Copy arguments and names into the single call slot; return `BEAM_BUSY` until the prior slot is consumed. Host-controlled execution only. |
| `beam_vm_pump` | Owner-thread, nonreentrant pump. Initialize `beam_pump_result.struct_size`; callback budget must be positive. Zero timeout polls. Returns callback count, `call_ready`, and `stop_requested`. |
| `beam_vm_call_take` | Transfer a completed result and consume its slot. Busy/no-slot return `BEAM_BUSY`/`BEAM_INVALID_STATE` without changing output. A stopped unrecorded call returns `BEAM_STOPPED` with an empty result. |
| `beam_vm_wake` | Thread-safe wake of a valid, externally lifetime-synchronized VM handle. It performs no Erlang operation and grants no cross-thread stop permission. |

Callbacks remain serial and cooperative. The timeout bounds waiting, not arbitrary
native callback execution. Terminal outcomes use the first-recorded-outcome rule.
An owner may stop with an outstanding call; result retrieval remains available
after workers join. Destroying a VM releases an unconsumed slot. Existing copied
results remain host-owned.

## Supplemental embedded dependencies

`beam_composition_options.embedded_dependency_count` and
`embedded_dependencies` supply immutable product-declared build facts for
`erlang:system_info(embedded_3pps)`. Each `beam_embedded_dependency` has an exact
`struct_size`, a nonempty UTF-8 `name` of at most 255 Unicode characters and a
`state`: `BEAM_EMBEDDED_INCLUDED` or `BEAM_EMBEDDED_EXCLUDED`. At most 1024 entries
are accepted. Names are case-sensitive, unique across states and cannot collide
with the VM-owned dependency inventory, including excluded VM dependencies.

Preparation validates and copies entries without creating atoms, loading code,
invoking callbacks or granting capabilities. Invalid input returns
`BEAM_INVALID_ARGUMENT` without publishing a preparation; allocation failure
returns `BEAM_OUT_OF_MEMORY`. A successfully initialized VM retains the copied
facts across product restart until termination. A later VM receives only its own
composition. Zero entries select only VM-owned reporting.

The query preserves VM-owned facts and appends supplements in declaration order
to the corresponding included/excluded lists. Public list order is unspecified.
These facts describe embedded source/build choices, not current native mappings
or every dependency. Native packages are not automatically added. Startup
context remains an opaque independent ETF input. This exact-SDK interface is
experimental; rebuild consumers when its layout changes.
