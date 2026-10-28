# Runtime and distribution architecture

BEAM/Erlang owns the shared VM, `libbeam`, the public embedding/native SDK,
modern components, compiler, shell and commands. The independent Erlang/OTP
product owns its applications, startup, native services, drivers, helpers,
commands, resources and assembly. It consumes an explicitly selected installed
SDK through public headers, symbols and CMake targets, with `libbeam` as its
sole VM. Package policy and its canonical format belong to `beam-erlang-package`.

The [runtime concepts](concepts.md), [C API](c-api/README.md) and
[deployment guide](c-api/deployment.md) define the public behavior. This guide
describes implementation ownership. Build maintenance belongs in
[cmake/](../cmake/README.md), acceptance in [tests/](../tests/README.md), and
release policy and upstream propagation in [release/](../release/README.md).

## Core and product code

Shared `BEAM::Runtime` and static `BEAM::RuntimeStatic` use the same VM objects.
Both physically embed exactly nine modules from `src/erts/preloaded/`:

| Module | Responsibility |
| --- | --- |
| `erlang`, `erts_internal` | Built-ins, native continuations and internal runtime protocols. |
| `erts_code_purger` | Code purge coordination. |
| `erts_literal_area_collector` | Reclamation of code literals and persistent-term storage. |
| `erts_dirty_process_signal_handler` | Erlang signals and system requests for dirty execution. |
| `erts_trace_cleaner` | Cleanup of retired trace-session references. |
| `atomics`, `counters`, `persistent_term` | Shared Erlang/native data mechanisms. |

[RuntimeCoreModules.cmake](../cmake/RuntimeCoreModules.cmake) owns this inventory.
Initialization stages these modules without workers or Erlang execution. Bare
start creates a private root and six required service processes: a purger, a
literal collector, three dirty signal handlers and a trace cleaner. These are
Erlang processes, not an OS-thread count. The root is not registered as `init`
and supplies no I/O server. Changing the mandatory core requires an architecture
decision covering execution and active and quiescent cleanup.

OTP supplies thirteen additional preloads through public composition, preserving
its 22 preloaded/sticky modules and restart behavior. Its executable embeds
those product-owned bytes and selects already-linked native implementations.
Modern commands and components retain shared linkage. Extensions resolve against
one host runtime; mixed linkage and second-runtime mappings are rejected.
Preload classification, actual supplied origins and native permissions remain
distinct. Loading support code grants no capabilities.

OTP's installed tree must match the pinned original build for equivalent
features, including paths, entry types, modes and literal link targets. Its
standard executable statically links the SDK. It installs no `libbeam.so`, VM
development archive, startup wrapper library, extra runtime directory or
validation manifest. Source/build/SDK provenance stays outside that tree.
Its [architecture](../../erlang-otp/docs/architecture.md) and
[compatibility contract](../../erlang-otp/docs/compatibility.md) own its details.

## Modern inventories and components

Ordinary inventories are acquired and verified by the host before their copied
buffers enter composition. They are neither embedded VM modules nor implicit
startup services.

| Unit | Modules | Installed inventory or library |
| --- | --- | --- |
| Base | `beam`, `beam_list`, `beam_map`, `beam_bytes`, `beam_text`, `beam_format`, `beam_time` | `lib/beam/library/{modules.txt,ebin/*.beam}` |
| Boot | `beam_return_boot`, `beam_boot` | `lib/beam/boot/{modules.txt,ebin/*.beam}` |
| Services | `beam_component_services`, `beam_host`, `beam_output`, `beam_code` | `lib/beam/services/{modules.txt,ebin/*.beam}` |
| File | `beam_file`, `prim_buffer`, `prim_file` | `lib/libbeam-file.so` |
| Network | `beam_socket`, `socket_registry`, `socket`, `maps`, `lists`, `prim_socket` | `lib/libbeam-network.so` |
| Zlib | `zlib` | `lib/libbeam-zlib.so` |

These library paths describe the Linux layout; consumers use exported targets
and verified artifact inventories for platform-specific names. Each component
owns its source/output lists and native target under `src/beam/`.
The [component manuals](components/README.md) independently describe file,
network and zlib APIs. Native descriptors are a private matching-build contract,
not a third-party provider ABI or hot-reload interface.

Returning preparation requires the base, boot and service-support inventories.
Generic composition does not require base or boot; selected services or deferred
activation require all four service-support modules. Supplying them alone
activates nothing. File, network and zlib remain independent selections. The
network component supplies numeric-address sockets, not legacy inet drivers or
DNS. Omitted units contribute no modules, including facades.

The SDK verifier checks producer expectations, module names, sizes and hashes
before composition copies bytes. Verification neither acquires code nor creates
a VM. Consumers verify canonical expectations instead of deriving replacement
expectations from installed files. The VM never scans directories or reads a
distribution catalog to resolve an undefined call.

## Composition, activation and execution

Preparation validates module/native selections, copies code and origins, and
retains mappings. It starts no workers or Erlang code and activates no NIFs.
Trusted native library constructors can execute during mapping. The VM loader
performs executable-code validation. A VM retains its own composition reference;
releasing the preparation does not invalidate live code or resources.

Admission, activation, owner monitoring, rollback and cleanup belong to the VM.
Product hosts choose acquisition, boot and entry policy through public headers
and symbols. Private VM dependencies require a reviewed public boundary or a
product-owned implementation, never copied private headers or another runtime.
Generic process-owned entry explicitly transfers process control; process-lifetime
native permission requires that mode. The `erts_bare_mode` implementation flag
tracks managed lifetime, not authorization to load native code or distribute nodes.

Linked-NIF selections bind a module, exact SDK build and resident initializer.
Preparation copies and validates them; explicit activation invokes the selected
initializer. They remain scoped to one VM lifetime. OTP built-in registration
and `load_nif` timing remain separately owned. Current loading behavior is
provisional; [native loading](native-extensions/loading.md) is its single home.

Modern returning boot executes the entry directly in the public call process.
Entry return completes the boot even if children remain. A server entry must
remain alive until its workload and acknowledged shutdown finish. An opaque
terminal host reply records completion after the callback returns without VM
reentry. The first outcome wins; cleanup diagnostics do not replace it. Copied
outcomes contain no live VM resource handles.

Callbacks run serially on the owner thread without VM locks and cannot reenter
the VM. Calls and process-owned entry pump them while waiting. The explicit
owner-thread pump can also serve idle requests with a timeout and callback
budget; thread-safe wake interrupts the wait without transferring VM ownership.
One asynchronous call slot copies admitted input and retains its result until
consumed. Stop cancels pending callbacks but cannot interrupt executing C code.
There is no general asynchronous cancellation or cross-thread stop contract.

## Lifetimes and host protection

One system owns at most one initialized VM handle, including a stopped handle.
The owner performs VM operations and cleanup. Stop closes admission, retires
application processes, drains dependent work and joins workers while retaining
the handle. Termination reclaims remaining resources and permits a fresh VM
with different composition/options. A stopped VM cannot restart. Concurrent VMs,
system reinitialization and unloading `libbeam` remain unsupported.

The implementation is a resettable singleton. System lifetime owns the native
thread substrate; VM lifetime owns scheduling dimensions, code/term roots,
polling, synchronization, TLS bindings, callbacks and allocated/mapped storage.
Shutdown drains async, timer, thread-progress and auxiliary work while workers
can cooperate. Quiescent cleanup cannot enqueue work to departed helpers.
Resource destructors, descriptor closure and synchronization destruction remain
required in addition to memory reclamation.

Returning teardown closes ports before releasing the program helper. It
terminates and reaps direct launched programs, including those without exit
status reporting; arbitrary descendants remain outside that ownership boundary.
Tests observe real descriptors, threads and child PIDs alongside VM counters.
Host allocator capacity retained in RSS alone does not establish a VM leak.

Host-controlled lifetimes preserve host signals, standard descriptors, terminal
state, environment and floating-point state. Workers own their masks and
alternate stacks. Native-stack JIT requires compatible synchronous-fault
handlers using `SA_ONSTACK`; initialization/start validate that policy. Vendored
PCRE has private visibility because its adapted match state is incompatible
with host PCRE; installed coexistence acceptance checks both symbol directions.

Module inspection/unload preserves processes and leaves code unchanged when
busy; runtime modules stay protected. Scheduler online control stays within
initialized capacity and creates no dynamic-thread, pause/resume or concurrency
promise. Copied lists/results survive later VM lifetimes and system termination
while the library remains loaded. Allocator tagging is selected for each fresh
VM; its default preserves upstream allocator settings. Explicit selection uses
the retained `+Mu` group, excluding temporary, literal and test allocators.

## Commands and implementation boundaries

The modern `beam` host owns shell, evaluation, source execution and compilation.
Runtime-family selectors/locking and OTP `beam_boot_*` adapters are absent.
Literal arguments after `--` remain program data. OTP command policy stays with
its own installed commands. Generic process-owned execution remains available
to independent hosts without reintroducing a product selector.

| Owner under `src/beam/` | Installed inventory |
| --- | --- |
| `compiler/` | `lib/beam/tools/compiler/ebin` |
| `cli/` | `lib/beam/runtime/beam/cli/ebin` |
| `run/` | `lib/beam/runtime/beam/run/ebin`, containing `beam_source_run` |
| `interactive/` | `lib/beam/runtime/beam/interactive/ebin` |

Each has an explicit sibling `modules.txt`. Canonical helper names do not export
a complete public OTP standard library. Compiler host requests supply source,
headers, resources, transforms, filesystem operations and diagnostics without
OTP code/file/application services. `beam run` terminates that compiler VM before
executing copied bytecode in a fresh returning VM under the same system.
Compiler processes, ETS and persistent terms do not carry across; transform
filesystem effects do. Dependencies come from the actual preprocessing pass;
transforms execute once. An OTP header remains source input, not application
activation. The [CLI manual](cli/README.md) owns command/profile behavior.

The shell/evaluator acquires canonical assembly-backed `prim_eval` as ordinary
code. Its adjacent Erlang stub is never a replacement. Shell/eval need no
compiler tooling. Shared CLI I/O and source-run/interactive profiles retain
distinct input capabilities. Unsupported helpers fail explicitly without
implicit code acquisition.

Package policy uses the generic application runner with an explicit descriptor;
no package inventory is compiled into `beam`. The package-owned application
lives under `lib/beam/apps/beam-pkg`; its shared recipe selects outputs. Project
launch selects an installed generation and never builds/installs implicitly.
Source tasks use literal argv, with native handoff after tool VM/system teardown.
The [package guide](packaging/README.md) links canonical package policy.
