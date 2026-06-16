# Public C API reference

Candidate reference for the current experimental SDK.

This reference describes the experimental Linux interface installed through `<beam/beam.h>`. Its runtime and returning-boot declarations
are also available separately through `<beam/runtime.h>` and `<beam/boot.h>`.
The [source header](../../include/beam/beam.h) supplies complete declarations;
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

`beam_composition_prepare(&options, &composition, &error)` requires exact
`struct_size` and an initially NULL output handle. It copies module inputs and
configuration, validates module containers/identities, maps explicit libraries
and retains them without creating a VM. Full code validation remains with the VM
loader. Returned failures leave the output unchanged. Native mapping, activation
and callback timing follow the current provisional
[loading and lifetime policy](../native-extensions/loading.md).

| Composition field | Contract |
| --- | --- |
| `component_count`, `components` | Up to three unique `beam_component_spec` values: exact size, fixed kind and explicit library path. |
| `module_count`, `modules` | Up to 4096 ordinary `beam_module_spec` inputs. |
| `defer_modules` | Retain ordinary inputs without staging them; host loads them explicitly before activation. |
| `nif_count`, `nifs` | Up to 1024 explicit native bindings to supplied ordinary modules. |
| `builtin_nif_count`, `builtin_nifs` | Up to 1024 already-linked NIF descriptors. |
| `builtin_driver_count`, `builtin_drivers` | Up to 1024 already-linked driver descriptors; requires driver admission. |
| `services` | OUTPUT, CODE, ZLIB, FILE, SOCKET, PROGRAM bits, each prefixed `BEAM_SERVICE_`. |
| `program_helper` | Exact absolute UTF-8 executable path for PROGRAM or admitted ordinary native ports; copied and checked without launching it. |
| `activation_count`, `activation_modules` | Up to 4096 reviewed deferred modules, including `on_load`, activated after native facilities with rollback. |
| `native_admission` | Ordinary process-lifetime NIF/driver/port/distribution permissions. |
| `preload_count`, `preloads` | Up to 4096 additional preloads, always staged, including when ordinary modules are deferred. |

`beam_module_spec` contains `name`, complete `bytes` and optional UTF-8 `origin`.
NULL origin becomes `"memory"`; other names/origins must be nonempty UTF-8.
Names are limited to 255 Unicode characters. Origins are labels, never paths
for the VM to acquire. Duplicate/conflicting names across component, preload,
ordinary and activation inventories, or replacement of core modules, are rejected.
The current treatment of `on_load` and deferred activation is described in
[loading and lifetime](../native-extensions/loading.md#load-information-and-module-callbacks).

The native component kinds are `BEAM_COMPONENT_FILE`, `BEAM_COMPONENT_NETWORK`
and `BEAM_COMPONENT_ZLIB`, retaining numeric values 1, 2 and 4. Retired kinds
are rejected. Native units imply their respective service bits; explicit native
service bits require their units. Component origins use canonical library paths
plus `#<module>`.

Base, boot and services are ordinary inventories, verified in the host before
composition. A nonzero effective service mask or deferred activation inventory
requires `beam_component_services`, `beam_host`, `beam_output` and `beam_code`
in ordinary `modules`, not preloads or activation modules. With `defer_modules`,
the host must load that support before activation. Merely supplying support
selects no capability and adds no activation work. External-NIF-only activation
requires no modern support. Generic composition requires neither base nor boot.
See the [inventory guide](guide.md#choose-a-composed-lifecycle) and
[bytecode verification](deployment.md#verify-ordinary-bytecode).

Additional preloads supplement `erlang:pre_loaded/0` throughout this VM's
lifetime and framework restart. They grant neither native permission nor extra
C unload protection; framework policy supplies stickiness. A later fresh VM
inherits none of these selections. The nine embedded modules remain mandatory.

`beam_nif_spec` contains exact `struct_size`, a module name and absolute UTF-8
library path. `beam_builtin_nif` and `beam_builtin_driver` contain exact
`struct_size`, nonempty UTF-8 `name`, matching `build_id`, a typed `initialize`
function and `taint` (zero or one). Initializers return public `ErlNifEntry` or
`ErlDrvEntry` tables.

The current loading design is provisional. See [loading and lifetime](../native-extensions/loading.md)
for validation and mapping, explicit activation, already-linked selections,
ordinary native admission, `on_load`, resource cleanup and upgrade boundaries.
The native API references cover [NIF operations](../native-extensions/nif.md)
and [driver callbacks](../native-extensions/driver-entry.md).

Missing/unloadable libraries or helpers return `BEAM_RUNTIME_NOT_FOUND`;
wrong-kind/build descriptors and conflicts return `BEAM_INVALID_ARGUMENT`;
malformed module containers/identity return `BEAM_INVALID_BEAM`. Recoverable
copy allocation can return `BEAM_OUT_OF_MEMORY`.

Assign the preparation to VM options. Successful initialization retains an
independent reference; `beam_composition_release(&composition)` clears/releases
the host's reference and accepts NULL/empty inputs. Mappings survive native and
code reclamation. `beam_composition_get_modules(composition, &modules, &count)`
borrows its immutable initial inventory, including component and selected
preload code but excluding the nine core modules. It is not a live VM snapshot;
retain the preparation while using it.

After host-controlled start, `beam_vm_activate(vm, &error)` establishes readiness
before application calls. Failure stops the partial VM; terminate it before
retrying with a fresh VM. `beam_vm_get_component(vm, kind, &info)` fills an
exact-size `beam_component_info` containing state and retained failure diagnostics.
States are ABSENT, PREPARED, ACTIVE, FAILED or STOPPED, prefixed
`BEAM_COMPONENT_`. Only the failed unit is FAILED; other selected units stop.
Required-owner failure closes availability and stops execution.

`beam_vm_get_activation(vm, &info)` observes aggregate activation with an
exact-size `beam_activation_info` (`struct_size`, `state`, copied `failure`).
States use the `BEAM_ACTIVATION_` prefix: ABSENT for no requested services,
activation modules or NIFs; PREPARED before activation; ACTIVE after success;
FAILED after activation/owner failure; STOPPED after ordinary shutdown. FAILED
remains observable after stop and takes precedence over STOPPED. Exact facility
or module identity remains in `beam_vm_get_report`. The query is owner-thread-only,
executes no Erlang, joins no workers, and leaves output unchanged on error.

## System and VM lifecycle

| Operation | Contract |
| --- | --- |
| `beam_init(&system, &error)` | Main-thread system initialization, no workers/Erlang. Output unchanged on returned error. A successfully claimed system cannot be initialized again. |
| `beam_vm_init(system, options, &vm, &error)` | Copy options and construct initialized native/code state without workers/Erlang. Output unchanged on returned error. Existing VM handle returns `BEAM_ALREADY_STARTED`. |
| `beam_vm_start(vm, &error)` | Host-controlled explicit start: publish code, create required helpers and workers. It does not call an application export. Starting twice or after stop is invalid. |
| `beam_vm_enter(vm, &error)` | Process-owned start and root admission; once admitted it never returns. |
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
activation finishes. Returned validation/activation errors retain an inspectable
handle; after admission halt exits the OS process and root failure is fatal.
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
| Start | Start host-controlled VM | `BEAM_INVALID_STATE` | `BEAM_INVALID_STATE`. |
| Stop | Stop without starting | Stop/join | Finish join or idempotent success. |
| Terminate | Release | Stop/release | Finish cleanup/release. |

The table assumes valid arguments on the owner thread outside callbacks.
Background activity can change state between observation and admission.

### Reports and host state

`beam_vm_get_report(vm, &report)` observes an exact-size copied `beam_vm_report`
without execution/joining, including after stop. `cause` is NONE, HALT,
COMPONENT_FAILURE or HOST_REQUEST with prefix `BEAM_TERMINAL_`; `halt_status`,
`component` and `failure` describe the winning terminal cause.
`has_cleanup_error`/`cleanup` remain separate. The component string is an identity,
not text parsed from a diagnostic. Output is unchanged on error.

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
retains mapped components and starts nothing. Callback pointers/context remain
borrowed through the run. Run belongs to the system's owner thread and cannot
be called from a callback. Initialization failure leaves preparation available
for retry/discard; after successful initialization execution consumes it following
VM termination, including application failure. If final cleanup fails, preparation
retains its stopped VM and discard retries cleanup before release. Cleanup
diagnostics remain observable even if startup prevented an application outcome.

| Returning option | Contract |
| --- | --- |
| `struct_size` | Exact `sizeof(beam_return_options)`. |
| `services` | Explicit OUTPUT/CODE/ZLIB/FILE/SOCKET/PROGRAM selection. |
| `entry_module`, `entry_function` | Required export; argument list determines arity. |
| `arguments` | Uncompressed ETF proper list; unlike `beam_vm_call`, empty bytes select `[]`. |
| `module_count`, `modules` | Copied `beam_return_module` entries with name, bytes, service flag and origin. |
| `nif_count`, `nifs` | Copied explicit matching-SDK native bindings to supplied ordinary modules; admitted before entry and activated through public composition. |
| `component_count`, `components` | Only FILE/NETWORK/ZLIB, matching their service bits exactly; zero is valid. |
| `bindir` | Absolute directory containing `erl_child_setup` for PROGRAM; translated to copied helper configuration. |
| `output`, `output_context` | Host byte-output handler and borrowed state. |
| `code`, `code_context` | Later code-acquisition handler and borrowed state. |
| `input`, `input_context` | Input handler and borrowed state. |
| `vm` | Optional copied host-controlled options; no initial root, preexisting composition or process policy. NULL selects conservative defaults. |

Returning requires complete verified ordinary base, boot and services inventories.
A returning module with zero `service` loads before start without `on_load`;
nonzero selects reviewed deferred activation. This is not a general native-provider
registration mechanism. Origin is a copied label (NULL means `memory`), with
component origins supplied by their unit. Conflicting names and malformed
components/modules follow composition errors. Preparations retain independent
mappings; another preparation survives a completed or failed run.

All selected services must acknowledge readiness before application entry.
Entry return, exception, requested finish, supported halt or required-service
failure ends the VM including children. The first outcome wins; later cleanup
errors are attached. The [lifecycle guide](guide.md#choose-a-composed-lifecycle) explains the
difference from a persistent bare VM.

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

Successful run reports kind RETURN, EXCEPTION, FINISH, HALT or SERVICE_FAILURE
with prefix `BEAM_OUTCOME_`. `term` is copied ETF, `halt_status` carries halt
status, `service` identifies a failed facility, and
`has_cleanup_error`/`cleanup_error` preserve secondary cleanup failure. Run can
return a non-OK cleanup status while still assigning an owning outcome, or
supply cleanup diagnostics without an application outcome. Release the
initialized output even after a failed run; API failure does not prove its
copied storage is empty. API status and application outcome remain distinct.

`beam_zlib_stream_count`, `beam_file_descriptor_count` and `beam_socket_count`
report semantic native-resource counts used by acceptance. They survive teardown
and are not reset by the next VM initialization; actual OS-resource observations
remain necessary.

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

`beam_composition_options.linked_nifs` is an explicit array of
`beam_linked_nif_spec { struct_size, module, build_id, initialize }` with
`linked_nif_count` entries. Modules must be ordinary supplied modules, identities
must match the loaded SDK, and names may not conflict with any other selected
NIF registration. Preparation validates and copies selections without invoking
initializers. `beam_vm_activate` performs the selected-NIF activation; repeated
activation is idempotent. Initializers and code remain resident through VM
termination. This does not change `builtin_nifs` or ordinary `load_nif` timing.
