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
Both physically embed exactly six modules; the canonical sources are under
`src/erts/preloaded/` and private `beam_internal` is under `src/beam/core/`:

| Module | Responsibility |
| --- | --- |
| `erlang`, `erts_internal` | Built-ins, native continuations and internal runtime protocols. |
| `erts_code_purger` | Code purge coordination. |
| `erts_literal_area_collector` | Reclamation of code literals and persistent-term storage. |
| `erts_dirty_process_signal_handler` | Erlang signals and system requests for dirty execution. |
| `beam_internal` | Mandatory trace-session cleanup worker and its private native calls. |

[RuntimeCoreModules.cmake](../cmake/RuntimeCoreModules.cmake) owns this inventory.
Initialization stages these modules without workers or Erlang execution. Bare
start creates a private root and six required service processes: a purger, a
literal collector, three dirty signal handlers and a trace cleaner. These are
Erlang processes, not an OS-thread count. The root is not registered as `init`
and supplies no I/O server. Changing the mandatory core requires an architecture
decision covering execution and active and quiescent cleanup.
Private embedded code remains protected through both C module management and
Erlang loading, deletion and purge paths. `beam_internal` is not replaceable by
ordinary code or by an OTP restart.

OTP supplies seventeen additional preloads through public composition, preserving
its 22 preloaded/sticky modules and restart behavior. Its executable embeds
those product-owned bytes and selects already-linked native implementations.
The five canonical SDK core names plus seventeen OTP preloads preserve the
reported inventory: private `beam_internal` remains physically embedded but is
excluded from Erlang preload reporting. OTP supplies `erts_trace_cleaner` as a
compatibility facade and selects its `start/0` as the cleaner startup entry.
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
| Base | `beam`, `beam_format`, `beam_time`, `beam_atomics`, `beam_counters`, `beam_persistent_term`, `beam_binary`, `beam_lists`, `beam_maps`, `beam_records`, `beam_ets`, `beam_string`, `beam_unicode`, `beam_math`, `beam_ddll`, `beam_debugger`, private `beam_runtime_unicode_util` | `lib/beam/library/{modules.txt,ebin/*.beam}` |
| Standard library | `array`, `dict`, `graph`, `gb_sets`, `gb_trees`, `orddict`, `ordsets`, `proplists`, `queue`, `sets`, `sofs`, `digraph`, `digraph_utils` | `lib/beam/stdlib/{modules.txt,ebin/*.beam}` |
| Boot | `beam_return_boot`, `beam_boot` | `lib/beam/boot/{modules.txt,ebin/*.beam}` |
| Services | `beam_startup`, `beam_host`, `beam_output`, `beam_code` | `lib/beam/services/{modules.txt,ebin/*.beam}` |
| File | `beam_file`, `prim_buffer`, `prim_file` | `lib/libbeam-file.so` |
| Network | `beam_socket`, `socket_registry`, `socket`, `beam_maps`, `beam_lists`, `beam_runtime_string_legacy`, `prim_socket` | `lib/libbeam-network.so` |
| Zlib | `zlib` | `lib/libbeam-zlib.so` |
| Tracing | `beam_trace`, private `beam_runtime_trace_errors` | `lib/libbeam-tracing.so` |

The ordinary base modules define library interfaces independently of native
execution. The collection and text interfaces select canonical native contracts
under `beam_binary`, `beam_lists`, `beam_maps`, `beam_records`, `beam_string` and
`beam_unicode`. OTP selects the same contracts at its own canonical names.
Each ID has one host-selected destination; unbound contracts publish no exports.

The catalog contains 573 native calling contracts. The modern library recipe
binds 558 operations, including four ETS implementation protocols; canonical
compiler and OTP hosts bind 556. Identical contracts
share one descriptor with one destination MFA per VM, while differing argument,
result or continuation-error policies retain distinct descriptors. Modern
execution does not supply `binary`, `lists`, `maps`, `string`, `unicode`, `atomics`,
`counters`, `persistent_term`, `ets`, `math`, `erl_ddll` or `erl_debugger`.
Modern hosts select the mathematics, driver and debugger contracts under
`beam_math`, `beam_ddll` and `beam_debugger`. Compiler-only canonical modules and OTP's facade
modules keep their names. Their storage and private array execution mechanisms
remain VM-owned; public native helper exports are selected explicitly. All 51 ETS contracts are shared descriptors: modern hosts bind
`beam_ets`, while canonical hosts bind `ets`. Table ownership, representation,
concurrency and continuation protocols retain their existing semantics.

The tracing component supplies twenty public session/sequential functions, ten
private native-access slots, the private delivery callbacks `enabled/3` and
`trace/5`, and explicit `load_nif/0` activation. `erts_internal:system_monitor/3`
retains its native destination. The VM owns trace state and event production;
ordinary delivery acquisition and activation do not create a collector or worker.
The cleaner runs independently as mandatory core code. Composition separately
selects its startup entry and an optional default tracer identity.

The public `beam_binary`, `beam_lists`, `beam_maps`, `beam_records`, `beam_string`,
`beam_unicode`, `beam_math`, `beam_ddll` and `beam_debugger` implementations are maintained directly under
`src/beam/library/`, with modern module declarations, calls and native slots.
They compile without runtime projection and retain deterministic artifact
verification and executable-import auditing. Their canonical compiler sources
remain separate, source-attested inputs to the private compiler/bootstrap VM.
The three mathematics, driver and debugger modules retain all 57 upstream
exports, including ordinary math constants and driver compatibility stubs.
Native binding selection neither grants driver admission nor enables debugger
instrumentation. Debugger support retains its experimental, configuration-dependent
contract. OTP retains its canonical module names and native selections.

`src/beam/library` is the base-library source boundary. Its modules may combine
BIF-backed functions with ordinary Erlang wrappers, algorithms and constants.
`src/beam/stdlib` contains thirteen modules without BIF-implemented functions.
Its separate verified inventory is explicitly acquired by normal CLI execution
and independently selected by embedding hosts. The library has no native slots
or startup hooks. Ordinary functions within base modules remain in the base
library.

A deterministic build-only projection still derives `beam_ets`, the private
Unicode helper and owned command/support modules from canonical sources. It
filters the adopted ETS export set and rewrites owned runtime references
independently of compiler target profiles, preserving source data, encoding atoms
and user-written MFAs.
Private Unicode algorithms remain under `beam_runtime_unicode_util`. Required
legacy Latin-1 `chr/2` and `to_upper/1` behavior remains in the private
`beam_runtime_string_legacy` helper, outside public `beam_string` exports.

Shell collection algorithms use `beam_runtime_queue`, `beam_runtime_sets`,
`beam_runtime_proplists` and `beam_runtime_orddict`. Their private names remain internal to shell tooling.
The ordinary standard library supplies thirteen public modules under their
upstream names. It is their sole modern producer; canonical compiler support
remains isolated in its own VM. Normal CLI execution verifies and acquires the
standard-library inventory explicitly before entry. Bare and component hosts
select it independently, without changing native selections or capabilities.

Base and networking use the same verified collection artifacts. Hosts partition
`beam_lists`/`beam_maps` explicitly when both inventories are selected; generic
composition still rejects duplicate names. Networking remains usable without
acquiring the complete base library. The CLI fallback owns the legacy helper
only when networking is absent. The public `beam_ets` facade has one producer
in the base library; interactive support uses that same supplied artifact.
Its 63 exports preserve table operations and ordinary algorithms while keeping
QLC/DETS adapters outside the interface. File-persistence and inspection exports
are withheld pending their own placement decisions. Match-spec compiler and
shell support are selected in their separate tooling inventories.
Type references, language/I/O protocols and hidden
noncallable diagnostic labels do not imply supplied facade modules.

[The C API reference](c-api/reference.md#optional-native-bif-bindings) owns the
binding contracts and their experimental identifiers. Empty composition publishes
no native callable exports. Ordinary code acquisition and native binding remain separate. Modern compiler
policy lowers map comprehensions through core map construction, so language
syntax also works with no optional bindings. Explicit user MFAs are preserved;
canonical targets retain their own explicit binding profiles.

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
handlers using `SA_ONSTACK`; initialization/start validate that policy. Regex
implementations belong to ordinary native providers. Neither VM image contains
PCRE or regex BIFs; installed coexistence acceptance includes host PCRE.

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

## Clock and timer API selection

Clock/timer implementations are shared nameless contracts. Modern compositions
bind 15 operations under `beam_time`; canonical compiler and OTP hosts bind them
under `erlang` and additionally select legacy `now/0`. The ordinary canonical
converter is an explicit core-export selection. Embedded core bytecode retains
its bodies while the shared loader publishes only selected optional core slots.
The private mandatory conversion helper supports native continuations and
component-internal conversions without acquiring the ordinary base library.
