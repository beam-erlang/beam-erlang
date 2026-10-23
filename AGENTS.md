# beam-erlang (distribution) agent notes

## Static SDK and faithful OTP assembly

The current implementation adds `BEAM::RuntimeStatic` alongside shared
`BEAM::Runtime`. OTP product/private hosts use the static target, embed thirteen
OTP-owned preloads and explicitly compose already-linked NIF/driver implementations.
The VM still embeds exactly nine core modules and owns no OTP native inventory.
Modern commands/components retain shared linkage. Native extensions must resolve
against one host runtime; mixed linkage and second-runtime mappings are rejected.

OTP must reproduce the pinned original installation tree, including paths, entry
types, modes and literal link targets for equivalent build features. It installs
no `libbeam.so`, development VM archive, startup wrapper libraries, `otp-runtime/`
directory or validation manifest. Standard external resources remain upstream
owned. Build/source/SDK provenance stays outside the installation. The custom
OTP C playground is retired; independent product fixtures own its preparation,
copied-input, recovery and lifecycle coverage. See the
[implementation evidence](../dev-documents/static-sdk-otp-runtime.md) for current
validation; the ownership checkpoint below is historical evidence and its former
shared-runtime placement is superseded. No application expansion, wx work or
release-upgrade implementation is authorized by this change.

## Ownership cutover checkpoint

`beam-erlang/src/otp/` is removed in the main checkout. BEAM's command now uses
modern boot exclusively; runtime-family selection/locking and OTP-specific boot,
acquisition and deployment adapters are removed. OTP selects its own terminal,
Zstandard and RAM-file artifacts through its host inventory. The ownership
migration and final interpreter/JIT validation are complete. BEAM passes all
21 tests per flavor; OTP completes all 101 entries per flavor with 79 passes,
seven deferred wx entries and 15 retained nonpassing gates. These results do not
establish full OTP compatibility. See [the cutover record](../dev-documents/otp-ownership-cutover.md) for implemented changes, acceptance
transfers and unresolved compatibility gaps. Older descriptions of selectable families,
static OTP registrations and duplicated installation assets below describe the
pre-cutover baseline and must not be restored as policy.


Ownership migration edits belong directly in this main checkout. Keep fresh
builds, isolated installations and logs under workspace `tmp/`; do not create
separate source worktrees. Existing validation snapshots stay fixed until their
running jobs finish. Integration does not establish passing acceptance or
complete a migration milestone. See the workspace runtime strategy for the
full goal and validation requirements.

This repository owns the shared BEAM VM, `libbeam`, embedding SDK and modern
BEAM distribution. OTP-specific sources, native services, startup policy and
commands belong to the separate self-contained `erlang-otp/` distribution
(formerly `otp-framework/`), using this VM through the installed public SDK.
The former OTP execution path, startup assets and SDK adapters are removed.
The reviewed `runtime/` application copies and terminal/Zstandard/RAM-file/native
hostname copies are also removed; fresh final validation is recorded on both flavors.
`erlang-otp-upstream/` is the unmodified upstream tracker. The OTP source tree
starts from the original import followed by seven deletion-only commits removing
849 selected files. At that removal checkpoint, all 10,969 survivors retained their imported contents and modes.
Tests, bootstrap, product inputs, documentation and licenses remain; removed
legacy infrastructure and build recipes are available in the upstream tracker.
An initial source-built OTP installation uses the installed SDK and an OTP-owned
public-API host. Ownership extraction is complete; full command, application and
compatibility coverage remain pending. Only those reviewed
commit sets define current removals; do not apply the historical OTP-side removal
mask. See the [source checkpoint](../dev-documents/source-map.md).
OTP delivery as BEAM packages is not a prerequisite.

When focusing on this tree, launch from `~/Workspace/beam-erlang/beam-erlang` so this file is loaded. The parent `~/Workspace/beam-erlang/AGENTS.md` is the workspace map (hygiene, references, sync, side ventures).

## This CMake vs. erlang-otp-cmake

There are two CMake efforts. Do not mix them.

| Tree | Purpose |
|------|---------|
| **This repository** | Purpose-built build for the shared VM and modern distribution (runtime composition, embedding, `beam` CLI). Free to diverge from upstream layout. |
| `../erlang-otp-cmake/` | Faithful CMake replacement of **vanilla** Erlang/OTP autoconf/make. Same generators, configure surface, release layout, and test harness as upstream at the OTP-29.0 pin. Presentable to the Erlang/OTP team. |

Vanilla build-fidelity work belongs in `erlang-otp-cmake/`. Distribution runtime, packaging, and embedding work belongs here.

## Runtime evolution

The [runtime strategy](../dev-documents/runtime-evolution-strategy.md) defines the
approved ownership boundary and staged migration. Its target supersedes the
older requirements to retain OTP native implementations, selectable runtime
families, an OTP-default `beam` command, or the old OTP boot adapters here.

- `libbeam` contains the shared emulator mechanisms and exactly nine embedded
  Erlang modules: `erlang`, `erts_internal`, `erts_code_purger`,
  `erts_literal_area_collector`, `erts_dirty_process_signal_handler`,
  `erts_trace_cleaner`, `atomics`, `counters` and `persistent_term`.
- The seven ordinary BEAM base modules live outside `libbeam` as explicitly
  supplied bytecode in `lib/beam/library/ebin`, selected by `modules.txt`. Hosts
  verify producer expectations before supplying those buffers to composition.
  Modern boot, compiler, shell, libraries and services remain owned here.
- OTP owns and builds its OTP-specific modules, native services, drivers, boot
  policy, helpers and commands in `erlang-otp`. Private VM dependencies are SDK
  boundary work to resolve, not a reason to retain those implementations here.
  Audit OTP-specific code outside `src/otp/` as well as that directory, including
  the now-retired copies of `prim_tty`, `zstd` and `ram_file`. Their static VM
  registrations, linkage and source copies are removed; OTP selects its own artifacts.
- `beam` is modern-only. Both `--runtime=otp` and `--runtime=beam`, the C
  runtime-family enum and selection fields, family locking, and OTP-specific
  `beam_boot_*` adapters are removed. Do not restore compatibility wrappers.
- Keep generic host-controlled/process-owned execution, explicit composition,
  VM-owned native activation and cleanup, and host-supplied preloads. OTP may
  supply thirteen additional preloads for its 22-module compatibility semantics;
  this does not place those modules in the VM or this repository.

Documentation of an existing API or command below records current behavior,
not a requirement to preserve its obsolete shape. Update functional acceptance
alongside implementation: retain modern shell, source execution and embedding
here, and migrate OTP behavior to the installed OTP product's tests before
removing its old coverage. Record replacement evidence explicitly.

The distribution milestone targets OTP 29.0 on the existing Linux
interpreter/JIT configurations, including applications, commands/builds, native
extensions, distributed interoperability, installation/resources, restart and
release upgrades. Current acceptance proves only the stated baseline. Follow
upstream release generations through the reviewed
[sync process](../dev-documents/Upstream-Sync-Process.md). Project-owned modern
interfaces remain experimental; their stabilization and subsequent compatibility
and deprecation policy belong to the
[release strategy](../dev-documents/distribution-release-strategy.md).
Inherited OTP reporting such as `erlang:system_info(otp_release)` is not a
permanent modern API requirement. Changes to the shared implementation must
still preserve the separate OTP product's selected behavior through suitable
adaptation and acceptance. No permanent source equality or automatic copying
between the products is required; assess correctness and security fixes for both.

Share implementations only across reviewed contracts. The common VM core can
still affect both paths: preserve the selected OTP target through suitable
mechanisms or adaptation. Report concrete blockers; never silently narrow the
target or weaken acceptance.
Shell, source execution and C embedding remain permanent product requirements.

### Current implementation and validation

`src/otp/` and the OTP-specific `beam_boot_*` adapters are removed. `beam` uses
modern returning boot exclusively. The VM has no runtime-family option or lock;
it accepts explicit compositions and sequential fresh VM lifetimes. This does
not add concurrent VMs or returning OTP teardown. Generic process-owned entry
remains available to independently assembled hosts.

OTP's own host acquires thirteen startup preloads, native paths and boot policy,
then uses generic public composition and lifecycle calls. Preserve immutable
context across restart and explicit process-only admission. `erts_bare_mode`
tracks managed lifetime, not permission to load native code, distribute nodes
or change host OS policy. Final ownership acceptance is recorded; see
[the current record](../dev-documents/otp-ownership-cutover.md).

## Documentation and development notes

`docs/` owns the user-facing manuals. Upstream formats and native API references
are adapted to the modern product and retain their license notices. Advanced
VM reference material does not imply public library or capability availability.
The former candidate and unreviewed upstream documentation trees are retired.

Organize `docs/` by reader task and public surface: getting started,
language/runtime concepts, C embedding, modern standard library, native
components, commands and packaging. Keep C tutorials/guides separate from the
API reference. Each of `libbeam-file.so`, `libbeam-network.so` and
`libbeam-zlib.so` owns an independent manual covering selection, capabilities,
Erlang interfaces, lifecycle and limits. The standard-library catalog links to
those manuals and distinguishes public core/base modules from private host
tooling. Do not present compiler/shell helpers as a complete public OTP STDLIB.
Package schema and command policy remain canonical in
`../beam-erlang-package/docs/`; link from the product's packaging guide instead
of maintaining a second specification. Label incomplete coverage and
experimental contracts explicitly. Documentation work alone does not authorize
interface changes; changes affecting OTP require coordinated adaptation and
the appropriate installed acceptance in both products.

Native loading and activation remain under design review. Keep their current
behavior in `docs/native-extensions/loading.md`, explicitly provisional, and
link there from native examples and references. Do not present automatic
`on_load` handling or its current absence as a settled design decision.

Temporary design drafts, implementation notes, migration records and
evidence belong in the workspace's `../dev-documents/`; start with its
[index](../dev-documents/README.md). Moving these records does not retire the
runtime strategy or the permanent acceptance contracts below. Review older
implementation descriptions against the code before relying on them.

`docs/c-api/` owns the C API documentation. Write it for C
embedders without ERTS knowledge and describe supported behavior. Keep history
and proposed capabilities in the other development notes. The standalone
first-call example belongs to `tests/installed/sdk/consumer/first-call/` and is
exercised by installed-SDK acceptance; keep the guide's excerpts and commands
consistent with those canonical sources. Builds and tests must not depend on
the workspace development documents, which are outside this Git repository.

The workspace [source relocation map](../dev-documents/source-map.md) records retained
material's upstream origins, current destinations and validation checkpoints.
Update it for relocations, splits and consolidations, preserving original
origins across later moves. Record deletions separately in the workspace removal
catalog; a removed path may represent retained content consolidated elsewhere.
Keep these internal records in `../dev-documents/`, outside the product tree.

Records of the 29-series split:

- `../dev-documents/source-map.md` — initialization and Pass 1
- `../dev-documents/Upstream-Sync-Process.md` — synchronization with the upstream tracker
- `../dev-documents/source-map.md` — removed paths and rationale; historical synchronization rules are not current policy

Active embedding work focuses on a single VM. Sequential fresh instances are
the only additional lifecycle goal: terminate and reclaim one VM before starting
another in the same host process. `../dev-documents/runtime-composition.md` records the implemented
system/VM initialization boundary and preserves concurrent instances and
pause/resume as potential future ideas, not project goals or required milestones.
Do not expand current work or require architectural changes solely to support
those possibilities. Operations remain serial and owner-thread only. One system
can own one initialized VM handle at a time, including a stopped handle. The
VM retains no family restriction after termination. Sequential composition changes must retain complete
teardown rather than reuse state from an earlier VM.

Native module inspection/unload and scheduler-policy query/control are part of
the public C API. Unload must preserve processes and leave code unchanged on busy
rejection; runtime modules remain protected. Scheduler online changes operate
within fixed initialized capacity and must not be presented as dynamic thread
creation, VM suspension, or a new concurrency contract. Copied module lists follow
the same cross-lifetime ownership principle as copied results. Keep installed
acceptance independent of playground examples for these behaviors.

The bare API retains nine runtime modules and their system processes
unconditionally. The separate returning boot adds selected services above this core. "Bare" does not
mean only host-selected modules or processes. Follow
[`../dev-documents/runtime-composition.md`](../dev-documents/runtime-composition.md) for the current
dependencies. This extraction preserves the nine-module mandatory core and its
runtime processes; changing that set requires a separate architecture decision.
Selected BEAM boot services do not weaken the bare profile.

`cmake/RuntimeCoreModules.cmake` owns the nine embedded modules. The modern
manifest is an independent installed inventory, not a startup order. Public
composition can select additional VM-lifetime preloads; the OTP product selects
thirteen and reports 22 through `erlang:pre_loaded()`. Bare and modern defaults
report nine. Actual supplied origins remain in immutable context.

Seven ordinary base modules are installed under `lib/beam/library/ebin`, with
an explicit `modules.txt` inventory. Bare hosts supply those modules through
ordinary composition inputs; returning preparation requires all seven. Generic
composition does not require modern base modules. Boot and services are ordinary
bytecode inventories under `lib/beam/boot` and `lib/beam/services`. Returning hosts
supply all three inventories. Selected services or deferred activation require
the four ordinary service-support modules before activation. Supplying support
alone grants no capabilities. `libbeam` embeds none of these inventories. The host-side
SDK verifier checks producer-generated build identity, module names, sizes and
hashes before composition copies the supplied bytes; it performs no acquisition
or VM initialization. Consumer configuration verifies canonical expectations
and never derives new expectations from installed replacements.

BEAM boot implementation lives under `src/beam/boot/`: returning native
lifecycle, CLI host and `beam_return_boot`. Shared private CLI I/O and scanner
support live under `src/beam/cli/`; source entry policy under `src/beam/run/`;
owned shell/evaluator policy under `src/beam/interactive/`. No inherited BEAM
`erl_init`, boot script, loader process or generated startup catalog remains.
The former `beam_cli`, `beam_cli_io`, `init` and `erl_prim_loader` adapters are
removed; do not restore compatibility services or implicit code acquisition.
The seven pure/execution base modules live under `src/beam/library/`. The boot
facade lives in `src/beam/boot/`, output/code in `src/beam/services/`, `beam_file` in `src/beam/file/`, and
`beam_socket` in `src/beam/network/`. File, network and zlib units embed their
complete module inventories with their native implementations in matching-build
shared objects. Base, boot and services are explicitly supplied ordinary bytecode. Omitted inventories and
components leave their modules absent, including facades.

Each component owns its source/output lists and native targets.
`src/beam/CMakeLists.txt` assembles modern components; OTP has its own product
build. Shared build helpers live in `cmake/`.
The private `beam_seed_compiler` uses 112 source-attested seeds in a bare VM to
compile owned compiler and support sources. `beam_bootstrap` loads those outputs
only after strict deterministic reproduction succeeds. Both use the modern
compiler host and public VM API; neither links the OTP CLI recipe. Base and core
artifact checks retain byte-for-byte comparison, including compiler/debug
metadata. Modern CLI, interactive and network helpers come from owned support
sources instead of retained OTP application bytecode. Earlier bootstrap checks
passed on both flavors; final command/native ownership acceptance now records
complete BEAM passes and the retained OTP compatibility gaps. OTP uses independent private seed and source-built hosts against the
installed SDK. System Erlang is not a fallback.
Do not install committed copies over those generated outputs or introduce two
producers for one `.beam` file. Retained ordinary service modules install under
`lib/beam/runtime/beam/ebin`; common CLI support and command policies install
in the separate `cli`, `run` and `interactive` inventories. Shared components live under `lib/` beside `libbeam.so`. Verified ordinary
base, boot and service-support outputs install under their respective
`lib/beam/{library,boot,services}/ebin` directories, with one producer per file.

Use clean build directories and empty installation prefixes for layout changes
and distribution validation. Delete and recreate disposable build/install trees
when necessary; preserve unrelated files and active jobs' inputs. Do not add
migration cleanup scripts, historical deletion inventories, configure/install
hooks that remove former layouts, or tests requiring in-place upgrades from
obsolete layouts. Ordinary test scratch resets and missing-asset/recovery checks
remain part of acceptance.

OTP applications are no longer staged or installed here. BEAM command
compilation uses its own optional tooling under `lib/beam/tools/compiler`.
Source execution has an independent private inventory under
`lib/beam/runtime/beam/run/ebin`, with `modules.txt` beside it, containing only
`beam_source_run`. Shared private I/O and language support install under
`lib/beam/runtime/beam/cli/ebin`; shell/evaluator support under
`lib/beam/runtime/beam/interactive/ebin`, each with a sibling `modules.txt`.
Selected retained helpers are private matching-build support, not a new public
library. `main/1` receives
Unicode charlists and preserves ordinary I/O, natural return and requested
halt status. Use `beam_file`, `beam_socket` and `beam_code` explicitly; the CLI
fetches additional bytecode only from the supplied absolute UTF-8 artifact
path, then publishes with the same origin. Do not add code/file/os/inet
compatibility adapters or implicit autoloading to this composition. Line and
character input are supported; source-run term scanning and BEAM DNS are
deferred. The interactive profile additionally supports term scanning and
formatted input. It preserves the real shell, bindings, history, local
functions, Unicode editing and error recovery. History and saved results
default to 20 each, with VM-local settings. OTP-oriented compile/load,
filesystem, record-file and documentation helpers fail explicitly; no code is
acquired to rescue undefined calls. The CLI
selects output, code, file, socket, zlib and program services, while minimal
returning hosts retain their chosen service set. Broader public library
extraction remains separate work. See
[source execution and interactive commands](../dev-documents/beam-cli.md).
Shared embedding bytecode remains under `lib/beam/ebin`. The generic child helper
installs under `libexec/beam`; the SDK package stays in `lib/cmake/BEAM`. The
modern shell/eval recipe acquires `prim_eval` as ordinary code from
`lib/beam/runtime/beam/interactive/ebin/prim_eval.beam`. Its canonical assembly
artifact lives in `src/beam/eval/ebin`; never compile the Erlang stub as a
replacement. OTP owns its independent preload and artifact verification.
`atomics`, `counters` and `persistent_term` remain one shared Erlang/native
implementation under `src/core/shared_data/`, with no family dispatch.

BEAM file, network and zlib native implementations belong to their component
units; tracing retains a separate library under `lib/beam/runtime/beam/priv`.
The modern profile installs no legacy inet drivers or OTP startup assets.
Returning boot selects supported services through public composition; the VM
owns activation, failure and cleanup. Completion tears down that VM before
returning the copied outcome. `beam_vm_terminate_report` supplies final copied
diagnostics; terminal host replies preserve callback non-reentrancy. Bare
embedding without an explicit composition rejects NIF activation and ports.
Do not equate installed availability with activation or claim hot native reload,
arbitrary provider registration or returning OTP teardown.

Generic initialization, BIFs, packet parsing, tracing, polling, ports and NIF
mechanisms remain VM-owned. OTP-specific native implementations must move to
OTP and compile against the installed public SDK. Their current private ERTS
dependencies require suitable public mechanisms or OTP-owned replacements;
copying private VM headers into the consumer is not an extraction boundary.
Preserve complete-prefix relocation and product-local dependency resolution.
The SDK's shared library and static archive each embed exactly nine core modules.
Product native implementations and ordinary base/boot/services inventories must
not become part of these SDK runtime artifacts. CLI catalogs and prefix discovery
stay in product host support. Optional CMake deployment packages explicit generic
and modern profiles; OTP acquisition and deployment belong to OTP's host.
The VM receives copied code and origins and enforces lifetimes; it must not
acquire distribution catalogs or package-resolution policy.

`erts_internal:vm_context/0` is the generic immutable context accessor; the
obsolete `boot_context/0` alias is removed. OTP's activation phase and installed
native-helper behavior remain OTP acceptance contracts. Preserve functional
shell, source execution and provenance checks in their owning product.

Public extension consumers use `BEAM::NIF` or `BEAM::Driver` without linking
private native targets. Independent distributions deploy the matching public
header closure with `beam_install_native_headers`, including the generated
integer-size configuration. Preserve existing NIF include paths and SDK hash
checks; do not recreate this inventory from private source paths in consumers.
The SDK also deploys exact embedded-core bytecode through
`beam_install_core_modules` for tools that inspect preloads on disk. This does
not change runtime composition; consumers must not compile replacement copies
of VM-owned core modules. See [runtime artifact SDK](docs/c-api/deployment.md).

## Current builds and artifact refresh

Use the preset build directories `build/linux-emu` and `build/linux-jit`, with
separate development installations under `install/<preset>`. Scratch builds,
installations and logs belong in `../tmp/`, never in reference trees.

The private `beam_seed_compiler` and `beam_bootstrap` compile and deterministically
verify owned compiler/support sources, then compile modern components and CLI
helpers. Neither host is installed. The obsolete OTP `runtime/` directory is
removed. Install exports shared `BEAM::Runtime`, static `BEAM::RuntimeStatic`, optional
`BEAM::CLI`, public NIF/driver headers and the generic artifact catalog.
Both runtime forms use the same compiled VM and nine core modules. Static
consumers retain the archive and native export inventory through the installed
target; their native extensions must not link a second runtime. Modern commands
and components retain shared linkage. Already-linked NIF/driver selections use
copied public composition descriptors and remain scoped to one VM lifetime.

At distribution integration gates, run the complete CTest suite for both
supported flavors. It checks generic API
validation, modern commands and literal arguments, compiler metadata, C/C++ SDK
clients, terminal behavior, bare and returning lifetimes, components and relocated
installation. OTP application/startup coverage now runs in its owning product.
The public API remains experimental. Stopped VMs cannot restart; terminating the
handle permits a fresh VM under the same system. System reinitialization and
library unloading remain unsupported.

`beam_vm_options.allocator_tagging` selects default, enabled or disabled tagging
for a fresh VM. Default preserves upstream's mixed allocator settings; explicit
selection uses the same allocator group as the retained `+Mu` parser, excluding
temporary, literal and test allocators. Preserve enum validation, exact options
size checking and shared/static sequential-lifetime acceptance. OTP owns the
`+Muatags` command adaptation; no general emulator-argument API or modern command
flag is implied. See the [C API reference](docs/c-api/reference.md).

Normal builds verify source-attested compiler seeds and deterministic core/base
artifacts without rewriting tracked files. `beam_refresh_native_artifacts`
writes modern native-wrapper candidates; `beam_refresh_core_artifacts` writes
nine core candidates including debug/inspection metadata; and
`beam_refresh_base_artifacts` writes the seven base candidates. The aggregate
`beam_refresh_boot_artifacts` depends on those three targets. Review candidates
before adopting them, rebuild and test both flavors. OTP seed maintenance belongs
to its own build. Never use an external runtime for bootstrap or validation.

## Test maintenance

Give each behavior one primary coverage owner. Extend an existing test before
adding another suite, host or harness. Exercise input variations in small focused
tests; add integration cases only for distinct boundaries such as installed
acquisition, consumer configuration, relocation or native lifecycle. Share
test-owned setup where it removes duplication without coupling independent
product consumers. Keep playground examples self-contained.

Do not duplicate mandatory build verification in CTest or freeze incidental
full-SDK directory shapes, generated filenames or fixed test counts. Assert
public usability and the absence of private implementation leakage. Exact
inventories remain appropriate for explicitly selected modules and minimal
bundles. Remove obsolete test adapters rather than retaining migration history.

Consolidation must preserve meaningful failures and recovery: producer identity
and bytecode integrity before and after consumer compilation, copied-input and
outcome ownership, entry returns and exceptions, service readiness and activation
failure, repeated lifetimes, host-state restoration, and observed descriptor,
thread and child cleanup. Keep independent installed-consumer evidence and
upstream assertions. Document coverage owners in `tests/README.md`.

## Permanent acceptance contracts

Preserve functional installed behavior in its owning product. The approved
modern-only `beam` default, removal of runtime selectors and obsolete C APIs,
and transfer of OTP assets and coverage explicitly supersede old command-default
and placement assertions. They do not waive shell, source execution, embedding,
native lifetime, relocation or OTP compatibility requirements. Establish
equivalent installed product coverage before retiring each old test path.

- After installation, `beam run hello_world.erl` must compile and execute the source in `tests/fixtures/hello_world.erl`, write exactly `Hello, World!` followed by a newline to stdout, leave stderr empty, and exit 0. It must not write a compiled `.beam` file. The fixture uses the normal `io:format/2` and `erlang:halt/1` path; do not alter it to weaken the requirement.
- Installed `beam shell` and bare `beam` must open the same native Erlang shell. Preserve the prompt, expression evaluation, bindings between expressions, terminal editing, and recovery after evaluation errors. `q().` must shut down and exit 0; piped expressions must be evaluated and EOF must exit 0. `erlang:halt(Status).` must preserve the requested status. Unknown shell arguments must be rejected. Banner wording is not fixed.
- BEAM acceptance must use the installed native `beam`, linked to the installed `libbeam.so`, and the distribution's own runtime files. OTP acceptance uses its own installed commands and host linked to that sole VM implementation. Both products must work from an unrelated working directory and after relocating the complete prefix, without runtime or loader overrides.
- Never satisfy a requirement by substituting system Erlang, loading runtime files from reference/source/build trees, special-casing the example, fabricating output, or weakening tests. Supported boot stages, explicit dependencies, and architectural changes are legitimate implementations; an alternate path that only makes a check pass is not.
- If the intended implementation cannot satisfy these constraints through the supported project architecture, stop that implementation path and report the concrete blocker and architectural change needed. Do not hide the blocker behind an undocumented bypass.
- Use focused checks while developing and for test-only refactors. At integration gates for build, installation, runtime, CLI or embedding changes, run the complete CTest suite on `linux-emu` and `linux-jit`, plus affected package and OTP acceptance. Full cross-product matrices belong to those gates, not every local edit. Report untested coverage explicitly; focused checks do not establish a completed distribution milestone.
- Bare embedding must load complete module bytes from host memory, return copied results or exceptions to C, and stop all VM-owned threads and OS activity while C continues. It must work without external runtime files or native child helpers. Initialization and pre-start loading create no workers and execute no Erlang. Termination reclaims VM resources and permits a fresh VM under the same system; retaining runtime state, using child processes, or reloading the library cannot substitute for teardown. Copied results survive subsequent VM lifetimes and system termination while the library remains loaded. Preserve host signals, standard descriptors, terminal state, environment, and floating-point state. The distribution-owned `installed-bare` test exercises this contract independently of the playground.
- Bare host-service callbacks run serially on the owner thread during `beam_vm_call`, with copied request/reply bytes and without VM locks held. They must not reenter the VM. Pending background requests wait for a subsequent call; stop cancels them. Optional host support and I/O modules are loaded explicitly, preserving the nine-module bare composition. Lifecycle queries observe state without running Erlang or implicitly joining workers. Preserve these contracts in installed acceptance independently of playground examples.

`installed-deployment` builds public-SDK clients for bare and returning
boot/file/network/zlib/program compositions. Preserve exact project-owned asset
inventories from clean installations, target-private relative metadata and relocation
to paths containing spaces and Unicode. Bundles must run from unrelated
directories without loader overrides or SDK fallback. Missing selected assets
must fail before application entry.

OTP's transferred host-preparation acceptance retains partial-read, allocation/OS
failure, unchanged-output, retry, custom-script, literal-argument and idempotent
release checks. Its product acceptance owns hostname resolution, native helpers,
restart and applications. Modern BEAM retains external programs and defers DNS.
Fixtures belong in their product repository; playground examples must remain
consistent but cannot substitute for permanent installed acceptance.

`beam compile` uses modern BEAM tooling; OTP owns its compiler commands.
BEAM compilation owns
an explicit tooling inventory and runs in a bare BEAM VM with host-request
callbacks, without OTP application services. BEAM `run` terminates that compiler
VM before executing copied bytecode in a fresh returning VM under the same
system. Compiler processes, ETS and persistent terms do not carry across;
transform filesystem effects do. See `../dev-documents/beam-cli.md`.
Compiler tooling stays outside the mandatory core, base and component units.
Internal builds and artifact refresh use the independent modern `beam_bootstrap`
route with source-attested seeds and mandatory deterministic reproduction.
Fresh builds reproduce all 112 seeds and complete BEAM acceptance passes on both
flavors. Do not introduce an external-runtime
bypass or weaken source/artifact correspondence. `installed-compile` owns independent acceptance for diagnostics, artifact
publication, literal arguments and dependency-driven builds. Its EPP dependency
metadata is opt-in; preserve existing preprocessing semantics and return shapes.
The BEAM preprocessor returns dependencies from the actual compilation pass;
transforms execute once. A requested OTP header is a source input, not runtime
application activation. Never satisfy standalone compiler acceptance by loading
OTP application code or starting OTP file/code/application services. Canonical
compiler-private helper names are scoped by the dedicated VM lifetime; they do
not establish a public BEAM library API. Broader public library extraction
remains a separate milestone.
The owned compiler seed inventory and refresh procedure are in
`../dev-documents/build-bootstrap.md`.

`installed-run` independently checks BEAM source execution in fresh and
relocated prefixes with OTP application/startup assets absent. Keep ordinary
I/O, Unicode charlist arguments, explicit service acquisition, missing-code
failures and compiler/execution isolation observable. Its fixtures belong to
this repository and must not read the playground. Preserve distinct shell/eval
coverage and migrate OTP behavior to the OTP product's tests; source-run coverage
does not replace either. Retire the old OTP-default assertions when `beam`
becomes modern-only.

`installed-interactive` independently checks BEAM shell/eval without OTP
application/startup assets or compiler tooling, in fresh and relocated prefixes.
Preserve the real installed shell's terminal behavior, history, local functions,
bindings, interpreted receives, error recovery and requested exit status.
Keep interactive term I/O distinct from the source-run profile. Missing,
malformed or wrong-identity inventory files must fail before entry execution;
undefined calls must never discover modules implicitly. Unsupported OTP helpers
must produce a clear error and leave the session usable. Repeated native-host
lifetimes must reclaim actual descriptors, children, threads and VM resources
and restore terminal/host state. Use independent distribution fixtures. Move
the OTP shell and hostname-resolution baseline to OTP-owned installed-command
coverage rather than preserving the old default/explicit family selectors.

Returning teardown closes surviving ports before releasing the native program
helper, including failed startup and an unused program service. It terminates
and reaps direct launched programs even without exit-status reporting. Preserve
real descriptor, child-PID and worker observations across repeated lifetimes;
zero VM counters alone do not prove OS cleanup. This ownership does not extend
to arbitrary descendants. Process-owning OTP keeps its existing helper lifetime.

OTP's `otp-compatibility-transfer` now owns ordinary I/O, application environment,
supervised worker replacement and TCP/file/zlib operations with both networking
backends. It checks fresh, relocated and isolated installations. Repeat resource
lifetimes, observe readiness and shutdown through acknowledgements/monitors and
bound operations. Loopback access is required; permission failures must never
silently skip coverage. Fixtures remain independent of the playground, with
process-group cleanup on timeout. This baseline does not establish arbitrary
application, distributed deployment or release-upgrade compatibility. Generic
bare and process-owned VM coverage stays here, including sequential lifetimes
with different explicit compositions.

## References

Read-only workspace material, all pinned to OTP-29.0 (`550d7b7898706c7822362c42e7b93120c1d1f29a`):

- `../references/erlang-otp-29/` — unmodified upstream sources
- `../references/erlang-otp-29-artifacts/` — original-build captures and `generated-goldens/{emu,jit}/`
- `../references/erlang-otp-29-studies/` — reverse-engineering (start with `INDEX.md`)

## Hygiene

Follow the parent workspace rules: timeless commits and docs, no session language, `UPSTREAM_COMMIT` is a bare SHA.

## Package format and local builder

[package format](../beam-erlang-package/docs/package-format.md) owns the first revisable `beam.toml` proposal. Seven draft manifests
cover the Erlangsters libraries, complete `otp/kernel` and `otp/stdlib`
applications, and the planned counter pair. These experimental drafts do not
define the separate OTP distribution's delivery mechanism. Existing OTP package
drafts retain one complete application per package; never create module subsets
to bypass ownership conflicts.
The Erlangsters drafts depend on STDLIB, whose overlap with BEAM-host-owned
modules must be resolved before that composition can execute.
`beam-pkg` is an independently versioned Erlang application. The generic native
launcher invokes `beam run --application` with an explicit descriptor; it has no
package policy and does not link libbeam. The package-owned versioned payload
lives under `lib/beam/apps/beam-pkg/<version>`. Help/version use output only;
build/install request file/program/zlib in a fresh composition. Package-aware
commands belong to `beam-pkg`; `beam` retains generic/source execution, compilation,
evaluation and the plain shell. No package module inventory is compiled into beam.
Format-2 package records and target-aware native outputs are under qualification.
Tree remains a status-2 stub; whole OTP build/project launch is unsupported.
Preserve the installed acceptance suites and the existing playground inventory.
The OTP counter draft records retained installed Kernel/STDLIB requirements;
the future consumer targets the separate OTP distribution. Consumer sources and
whole OTP application recipes remain deferred. Ordinary local-library builds
are implemented. Older candidates under workspace `notes/` are historical.

The tool build consumes `BEAM_PACKAGE_SOURCE_DIR`, selected explicitly as a local
`beam-erlang-package` checkout. Its shared recipe owns source/output inventories; the
adapter in `cmake/PackageTool.cmake` supplies the verified modern build compiler.
`BEAM_BUNDLE_PACKAGE_TOOL=OFF` produces a bootstrap or target SDK without package
sources. Desktop distributions normally bundle the selected package payload.
Standalone builds consume installed BEAM::CLI and install to a separate prefix.
Hosted source inputs must be reviewed commits; never substitute working-tree
results for release attestations. CMake performs no network acquisition.

## Installed project shell and source tasks

`beam-pkg shell --project .` opens an installed BEAM selection, including library
roots without main/1. Preparation checks the complete interactive host inventory;
execution uses public composition and explicit native activation with host I/O.
Plain shell belongs to beam; project shell belongs to beam-pkg. Project shell adds no filesystem, network,
program or implicit acquisition services. Never auto-build or install on launch.
`beam-pkg task [--project dir] [name [-- args...]]` lists or executes root-manifest
`[scripts]` literal argv arrays. Task policy belongs to beam-erlang-package;
native exec hosting belongs to beam-erlang and terminates the tool VM/system
before handing off terminal, streams and status. Scripts are source-only and
excluded from archives/locks/environments. Preserve GLM's unused test/ directory.

## Preview release boundary

Release qualification uses product-owned fixtures and excludes playground.
The adopted release identity uses the integrated upstream ERTS version plus an
Erlangsters distribution revision and an explicit preview designation. The
[release strategy](../dev-documents/distribution-release-strategy.md) owns version,
support and public compatibility policy; the
[sync process](../dev-documents/Upstream-Sync-Process.md) owns product branch
selection and upstream propagation. Maintenance branches use the product's
major version, such as BEAM `maint-17`, and start from reviewed product
checkpoints rather than inherited upstream branch tips. Branch existence alone
establishes no support promise.

The current identity is `17.0-r1-preview.1`. Preserve the former
`0.1.0-preview.1` identity in historical evidence. The modern root `OTP_VERSION`
and `otp_versions.table` are removed; generated OTP reporting remains explicit
compatibility metadata until separately changed. Exact full release and installed
SDK build checks remain required under the revised numbering. Current preview APIs remain
experimental: source compatibility and C binary compatibility require separate
stabilization decisions and acceptance. Native component descriptors remain a
private matching-build contract. OTP runtime/application version reporting
remains owned by the separate OTP product. Missing release inputs and mandatory
OTP failures block promotion, never become passing no-ops.

## Build, CI and release ownership

The complete intended desktop/mobile matrix is registered in `ci/targets.json`
and implemented through CMake presets and the reusable GitHub Actions workers.
See [CI architecture](ci/README.md) and [release operations](release/README.md).
Keep target compiler flags in CMake, runner provisioning in `ci/`, behavioral
fixtures in `tests/`, and image recipes in `containers/`. Do not reduce the
catalog to platforms currently passing. Report actual failures and disabled
execution explicitly; neither satisfies a required release gate.

Cross compilation uses isolated source-attested native host tools. Never execute
target binaries for generation or install native host tools into a target SDK.
`BEAM_HOST_TOOLS_ROOT` selects a verified native build at its recorded path.
Product maintenance lines and channels are registered in `release/policy.json`;
branch existence alone does not establish support. Stable and preview share the
same qualified-byte pipeline. Current source pins, owner configuration and OTP
compatibility blockers remain independent from the architecture's completeness.
