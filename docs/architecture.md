# Runtime and distribution architecture

BEAM/Erlang owns the shared VM, `libbeam`, the public embedding/native SDK,
modern libraries, compiler, shell and commands. The independent Erlang/OTP
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
The modern CLI retains shared linkage. Libraries support shared VM/shared
libraries, shared VM/static libraries and static VM/static libraries. Extensions
resolve against one host runtime; second-runtime mappings are rejected.
Preload classification, actual supplied origins and native permissions remain
distinct. Loading support code grants no capabilities.

OTP's installed tree must match the pinned original build for equivalent
features, including paths, entry types, modes and literal link targets. Its
standard executable statically links the SDK. It installs no `libbeam.so`, VM
development archive, startup wrapper library, extra runtime directory or
validation manifest. Source/build/SDK provenance stays outside that tree.
Its [architecture](../../erlang-otp/docs/architecture.md) and
[compatibility contract](../../erlang-otp/docs/compatibility.md) own its details.

## Modern inventories and libraries

Ordinary inventories are acquired and verified by the host before their copied
buffers enter composition. They are neither embedded VM modules nor implicit
startup services.

| Unit | Modules | Installed inventory or library |
| --- | --- | --- |
| Base | `beam`, `beam_list`, `beam_map`, `beam_bytes`, `beam_text`, `beam_format`, `beam_time` | `lib/beam/library/{modules.txt,ebin/*.beam}` |
| Boot | `beam_return_boot`, `beam_boot` | `lib/beam/boot/{modules.txt,ebin/*.beam}` |
| Services | `beam_startup`, `beam_host`, `beam_output`, `beam_code` | `lib/beam/services/{modules.txt,ebin/*.beam}` |
| File | `beam_file`, `prim_buffer`, `prim_file` | `lib/libbeam-file.so` |
| Network | `beam_socket`, `socket_registry`, `socket`, `maps`, `lists`, `prim_socket` | `lib/libbeam-network.so` |
| Zlib | `zlib` | `lib/libbeam-zlib.so` |

These paths describe shared Linux delivery; each native library also has an
installed static archive and explicit shared/static CMake targets. Consumers use
verified artifact inventories for platform names. Each library owns its source
and native implementation under `src/beam/`, compiled against public native SDK
headers and symbols. Library-owned portability code supplies platform support.
The VM owns one private compression backend. Its namespaced public C interface
is used by the ordinary zlib NIF bridge and by OTP's independently composed
zlib NIF. The optional library contains no second codec. Core gzip module
preparation uses a private trapping operation without acquiring the optional
Erlang module.

The [library manuals](components/README.md) independently describe file,
network and zlib. Their [descriptor format](native-extensions/libraries.md) is
experimental and tied to an exact SDK build. Descriptors carry ordinary modules,
origins and typed resident registrations, without startup hooks, fixed kinds or
dependency discovery. Native constructors retain trusted-code behavior.

Generic composition requires no base, boot or startup inventory. Optional Host
startup requires only the controller and code used by the requested actions.
File startup loads file/buffer NIFs and passes the real janitor PID; socket startup
passes its real registry/configuration; zlib loads without an owner sentinel.
Library-owned monitors report actual owner failures through generic VM failure
requests. The network library supplies numeric-address sockets, not legacy inet
or DNS. Omitted libraries contribute no modules, including facades.

Composition may also supply immutable supplemental embedded-dependency build
facts. They extend third-party reporting without changing the VM-owned inventory,
loading code or granting capabilities. Products own their declarations; the VM
validates, copies and retains them across restart. Startup context stays opaque.

The SDK verifier checks producer expectations, module names, sizes and hashes
before composition copies bytes. Verification neither acquires code nor creates
a VM. Consumers verify canonical expectations instead of deriving replacement
expectations from installed files. The VM never scans directories or reads a
distribution catalog to resolve an undefined call.

## Composition, binding and execution

Preparation validates generic module/native selections and copies bytes and
origins. It retains opaque native owners before storing their pointers, without
knowing whether an owner represents a library. Acquisition and native image
inspection happen separately through public interfaces. A VM independently
retains its preparation until resources, unload callbacks and native registries
have been reclaimed.

Native execution safety, admission, binding and cleanup belong to the VM.
Library acquisition, startup actions, readiness monitors and deferred-module
rollback belong to the optional Host and library layers. Neither runtime image
contains a library catalog or returning controller. Host code compiles against
public SDK headers and symbols only. OTP uses generic VM interfaces without
acquiring a dependency on Host.

Resident table access happens at VM initialization; Erlang `load_nif/2` later
supplies actual load information. Explicit linked/external selections instead
use one generic binding record, initialized by `beam_vm_bind_nif` after start.
This route preserves deferred initialization and `undefined` load information.
Success is idempotent; initialization failure stops the VM and retains the first
terminal cause. Cleanup errors remain separate. See [native loading](native-extensions/loading.md).

Modern returning boot executes the entry directly in the public call process.
Its native orchestration is installed in the optional static `BEAM::Host`
archive, outside both VM images. Hosts include `<beam/boot.h>` explicitly;
the core umbrella exposes VM declarations only. The same optional archive
provides [passive library acquisition](native-extensions/libraries.md), with
generic native-owner records keeping mapped code resident through reclamation.
Neither library acquisition nor descriptor inspection starts a VM or invokes
native load callbacks.
Entry return completes the boot even if children remain. A server entry must
remain alive until its workload and acknowledged shutdown finish. An opaque
terminal host reply records completion after the callback returns without VM
reentry. The first outcome wins; cleanup diagnostics do not replace it. Copied
outcomes contain no live VM resource handles.

Process-owned hosts may start workers while keeping the initial root suspended,
perform synchronous setup and explicit per-module native binding, then commit
with `beam_vm_enter`. Before commit, stop and termination remain recoverable;
ordinary process-lifetime native permissions and irreversible process policy
remain deferred. The commit and terminal-failure arbitration are synchronized.

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
