# BEAM/Erlang contributor instructions

This repository owns the shared VM, `libbeam`, public BEAM/Erlang SDK, modern
libraries and commands. Read [architecture](docs/architecture.md) for ownership,
[build maintenance](cmake/README.md) for bootstrap/refresh, [tests](tests/README.md)
for coverage and [release operations](release/README.md) for version/support policy.
These maintained documents describe current contracts; temporary plans, reports,
validation logs and development histories do not belong in tracked product source.
Do not reference temporary development records from product documentation.

## Ownership and runtime boundaries

- Both products share one VM implementation. OTP owns its applications, startup,
  services, drivers, helpers, commands, resources and assembly. It consumes an
  explicitly selected installed SDK through public headers, symbols and CMake
  targets. Never use sibling source/build dependencies, copy private VM headers
  into consumers, or introduce a second runtime.
- Shared changes must preserve the selected OTP compatibility target through
  suitable mechanisms or adaptation and owning acceptance. Assess correctness
  and security fixes for both products; never synchronize automatically. Read
  OTP's [compatibility contract](../erlang-otp/docs/compatibility.md) for its
  objective, gates and stop rules. Ownership separation does not establish full
  OTP compatibility.
- Shared/static SDK forms embed exactly nine core modules. Seven base modules,
  boot and service-support inventories remain ordinary, explicitly supplied code.
  OTP supplies thirteen additional preloads and its own linked native selections.
  Supplying support enables no capabilities; generic composition does not require
  modern base or boot. Preserve single-runtime linkage and native rejection.
- `beam` is modern-only. Do not restore runtime-family flags/APIs/locking,
  OTP `beam_boot_*` acquisition/deployment adapters or `src/otp/`. Keep generic
  host-controlled and process-owned execution, explicit composition and literal
  arguments after `--`. Sequential fresh VMs may differ; concurrent VMs remain
  unsupported.
- VM lifetimes own native admission, binding and cleanup. Returning orchestration
  and passive library acquisition belong to the optional public-SDK `BEAM::Host`
  archive, never either VM image. Passive library descriptors are documented
  experimental exact-SDK inputs, not stable provider ABIs or hot-reload promises.
  Initializer timing and residency are documented in
  [native loading](docs/native-extensions/loading.md).
- OTP product/private hosts use `BEAM::RuntimeStatic`; the modern CLI uses shared
  linkage. Passive libraries support shared VM/shared libraries, shared VM/static
  libraries and static VM/static libraries. OTP must reproduce the pinned original installation shape
  for equivalent features, including types, modes and literal link targets.
  Build/SDK provenance stays external. Its own architecture and installed tests
  own application/startup/native behavior.

## Source and bootstrap maintenance

Retained upstream paths live under `src/erts/` and `src/lib/`; modern sources live
under `src/beam/`. Emulator suites and adjacent data live under
`tests/vm/emulator/`. The separate `tests/vm/common_test/` harness explicitly
selects an installed OTP product and matching SDK; ordinary BEAM builds/tests
require neither. Public SDK headers live under `include/beam/`.
[Upstream synchronization](release/upstream-sync.md) owns current mapping and
propagation. Preserve notices and use exact committed-tree imports, with the
base import separate from adaptations. `UPSTREAM_COMMIT` contains only its SHA
and newline. Review actual target-branch history; never apply old blanket masks.

The private modern `beam_seed_compiler` and `beam_bootstrap` use source-attested
seeds and mandatory deterministic reproduction. Normal builds never rewrite
tracked inputs or discover external Erlang. Compiler/debug metadata stays part
of verification. Core/base/native artifacts retain one producer; review candidates
before adopting them. [Build maintenance](cmake/README.md) owns refresh targets
and native cross-host verification. OTP independently owns its bootstrap.

Preserve C API module inspection/unload, scheduler controls and copied-result
ownership. Busy unload leaves code/processes unchanged; runtime modules are
protected. Scheduler changes stay within initialized capacity. One system owns
one VM handle, including a stopped handle; terminate it before initializing
another. Stopped VMs cannot restart; system reinitialization/library unloading
remain unsupported. Host-controlled operation is serial and owner-thread only.

`beam_vm_options.allocator_tagging` retains enum/exact-size validation, upstream
mixed defaults and the selected allocator group excluding temporary, literal and
test allocators. Preserve shared/static sequential-lifetime acceptance. OTP owns
its command adaptation; no general emulator-option API is implied.

Public native consumers use `BEAM::NIF`/`BEAM::Driver`. The installed
`beam_install_native_headers` closure includes generated integer configuration;
never recreate it from private source paths. `beam_install_core_modules` exposes
exact embedded-core inspection bytes without changing composition or authorizing
replacement compilation. Generic SDK artifacts contain no product-native inventory.

## Test maintenance

Give each behavior one primary coverage owner. Extend an existing test before
adding another suite, host or harness. Exercise input variations in small focused
tests; add integration cases only for distinct boundaries such as installed
acquisition, consumer configuration, relocation or native lifecycle. Share
test-owned setup where it removes duplication without coupling independent
product consumers. Installed fixtures must remain self-contained.

Do not duplicate mandatory build verification in CTest or freeze incidental
full-SDK directory shapes, generated filenames or fixed test counts. Assert
public usability and the absence of private implementation leakage. Exact
inventories remain appropriate for explicitly selected modules and minimal
bundles. Remove obsolete test adapters rather than retaining migration history.

Consolidation must preserve meaningful failures and recovery: producer identity
and bytecode integrity before and after consumer compilation, copied-input and
outcome ownership, entry returns and exceptions, service readiness and activation
failure, repeated lifetimes, host-state restoration, and observed descriptor,
thread and child cleanup. Keep independent installed-consumer checks and
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
- Bare embedding must load complete module bytes from host memory, return copied results or exceptions to C, and stop all VM-owned threads and OS activity while C continues. It must work without external runtime files or native child helpers. Initialization and pre-start loading create no workers and execute no Erlang. Termination reclaims VM resources and permits a fresh VM under the same system; retaining runtime state, using child processes, or reloading the library cannot substitute for teardown. Copied results survive subsequent VM lifetimes and system termination while the library remains loaded. Preserve host signals, standard descriptors, terminal state, environment, and floating-point state. The distribution-owned `installed-bare-beam` test exercises this contract independently of development consumers.
- Bare host-service callbacks run serially on the owner thread during `beam_vm_call`, with copied request/reply bytes and without VM locks held. They must not reenter the VM. Pending background requests wait for a subsequent call; stop cancels them. Optional host support and I/O modules are loaded explicitly, preserving the nine-module bare composition. Lifecycle queries observe state without running Erlang or implicitly joining workers. Preserve these contracts in independent installed acceptance.

`installed-deployment` builds public-SDK clients for bare and returning
boot/file/network/zlib/program compositions. Preserve exact project-owned asset
inventories from clean installations, target-private relative metadata and relocation
to paths containing spaces and Unicode. Bundles must run from unrelated
directories without loader overrides or SDK fallback. Missing selected assets
must fail before application entry.

OTP's host-preparation acceptance retains partial-read, allocation/OS
failure, unchanged-output, retry, custom-script, literal-argument and idempotent
release checks. Its product acceptance owns hostname resolution, native helpers,
restart and applications. Modern BEAM retains external programs and defers DNS.
Fixtures belong in their product repository; external examples cannot substitute
for permanent installed acceptance.

`beam compile` uses modern BEAM tooling; OTP owns its compiler commands.
BEAM compilation owns an explicit tooling inventory and runs in a bare BEAM VM with host-request
callbacks, without OTP application services. BEAM `run` terminates that compiler
VM before executing copied bytecode in a fresh returning VM under the same
system. Compiler processes, ETS and persistent terms do not carry across;
transform filesystem effects do. See the [CLI manual](docs/cli/README.md).
Compiler tooling stays outside the mandatory core, base and component units.
Internal builds and artifact refresh use the independent modern `beam_bootstrap`
route with source-attested seeds and mandatory deterministic reproduction.
Fresh builds must reproduce the complete declared seed inventory. Do not
introduce an external-runtime bypass or weaken source/artifact correspondence.
`installed-compile` owns independent acceptance for diagnostics, artifact
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
[build maintenance](cmake/README.md).

`installed-run` independently checks BEAM source execution in fresh and
relocated prefixes with OTP application/startup assets absent. Keep ordinary
I/O, Unicode charlist arguments, explicit service acquisition, missing-code
failures and compiler/execution isolation observable. Its fixtures belong to
this repository and must be self-contained. Preserve distinct shell/eval coverage
and OTP-owned behavior in its product tests; source-run coverage replaces neither.

`installed-interactive` independently checks BEAM shell/eval without OTP
application/startup assets or compiler tooling, in fresh and relocated prefixes.
Preserve the real installed shell's terminal behavior, history, local functions,
bindings, interpreted receives, error recovery and requested exit status.
Keep interactive term I/O distinct from the source-run profile. Missing,
malformed or wrong-identity inventory files must fail before entry execution;
undefined calls must never discover modules implicitly. Unsupported OTP helpers
must produce a clear error and leave the session usable. Repeated native-host
lifetimes must reclaim actual descriptors, children, threads and VM resources
and restore terminal/host state. Use independent distribution fixtures.
OTP shell and hostname-resolution acceptance remain OTP-owned.

Returning teardown closes surviving ports before releasing the native program
helper, including failed startup and an unused program service. It terminates
and reaps direct launched programs even without exit-status reporting. Preserve
real descriptor, child-PID and worker observations across repeated lifetimes;
zero VM counters alone do not prove OS cleanup. This ownership does not extend
to arbitrary descendants. Process-owning OTP keeps its existing helper lifetime.

OTP's `otp-compatibility-transfer` owns ordinary I/O, application environment,
supervised worker replacement and TCP/file/zlib operations with both networking
backends. It checks fresh, relocated and isolated installations. Repeat resource
lifetimes, observe readiness and shutdown through acknowledgements/monitors and
bound operations. Loopback access is required; permission failures must never
silently skip coverage. Fixtures remain self-contained, with process-group cleanup
on timeout. This baseline does not establish arbitrary application, distributed
deployment or release-upgrade compatibility. Generic
bare and process-owned VM coverage stays here, including sequential lifetimes
with different explicit compositions.

## Packages and installed projects

`beam-erlang-package` owns package policy, command behavior, source/module
inventory and its shared CMake recipe. [Package format](../beam-erlang-package/docs/package-format.md)
and [commands](../beam-erlang-package/docs/commands.md) are canonical.
`BEAM_PACKAGE_SOURCE_DIR` selects reviewed source explicitly; CMake never fetches
missing inputs. `BEAM_BUNDLE_PACKAGE_TOOL=OFF` permits an SDK without package
sources. Bundled builds use verified `beam_bootstrap`; standalone package builds
use installed `BEAM::CLI`. Keep inventories, descriptors and host identities matched.
Native package output policy is supplied by the package-owned CMake helper.
Managed builds inject the verified helper from the selected package payload;
direct package recipes include it explicitly after SDK discovery. Ordinary NIF
admission and registration remain SDK-owned and require no package payload.

The generic launcher executes an explicit application descriptor; no package
inventory/policy is compiled into `beam`. Payload lives under `lib/beam/apps/beam-pkg`.
Help/version use output-only assets. Build/install use file/program/zlib. Tree
remains a status-2 stub; whole OTP application recipes/compiler plugins and hosted
registry support remain unsupported. OTP assembly does not depend on package
archives. Whole-application OTP drafts must not become module subsets or duplicate
providers to hide an ownership conflict. Framework/consumer metadata stays with
its sources; do not copy implementations into product fixtures.

Project run/shell selects an installed generation and never builds/installs on
launch. Project shell includes library roots without `main/1`, validates the
complete interactive inventory and uses generic composition and explicit Host startup
with host I/O. It adds no implicit file/network/program/acquisition facilities.
Source tasks execute root `[scripts]` literal argv arrays; package policy and
native hosting remain distinct. Terminate the tool VM/system before handing off
terminal, streams and status. Tasks remain excluded from archives, locks and
installed environments. Preserve optional unbound native outputs, explicit
`beam_target_nif` bindings and ordinary native admission.

## Documentation and working files

User manuals belong in `docs/`, organized by reader task: getting started,
language/runtime, C API guides/reference, modern library, separate native
libraries, CLI and packaging. Keep file/network/zlib manuals independent and
link canonical package policy. Private compiler/shell helpers do not establish
a public OTP standard library. Keep public examples consistent with canonical
product fixtures; the first-call sources live in
`tests/installed/sdk/consumer/first-call/`. Tests/builds must be self-contained.
Documentation does not freeze experimental interfaces or authorize API changes.

Work in the main owning checkout and preserve unrelated/incomplete changes.
Do not create separate source worktrees for distribution/ownership work. No
commits, pushes, publication or deployment are implied. Build under ignored
`build/<preset>` and install under `install/<preset>`; use clean trees/empty prefixes
for layout changes. Do not add obsolete-layout cleanup hooks or upgrade assertions.
Never place disposable experiments in immutable upstream references.

Select build/test dependencies and their executable paths explicitly. Before
claiming a dependency unavailable, verify suitable cached packages/manifests
when provided. Do not fetch missing inputs automatically or use a running system
service. System package installation requires authorization. Keep original
packages, extracted read-only inputs and URL/version/hash manifests separate from
mutable builds, test data and logs.

Before large validation, estimate peak storage, check space and bound concurrent
installations. Use one active scratch root; remove superseded attempts after
stopping owned processes. Never remove another job's inputs. Runners clean
private installations, copies and bundles on success, failure and catchable
interruption, retaining compact source/SDK identities, results and bounded diagnostics.
Use private copies for mutation/relocation/isolation, never hardlinks to selected
inputs. Reuse read-only matching immutable installations only where permitted.
`BEAM_TEST_KEEP_WORK=1` is debugging-only; record retained path/size and remove it
after investigation. Selected SDKs and normal development builds are not cleanup
targets. Write maintained documentation without session/tool attribution.

## CI and release

[ci/targets.json](ci/targets.json) owns the complete desktop/mobile matrix;
CMake presets own compiler settings, [ci/](ci/README.md) provisioning,
[tests/](tests/README.md) behavior and [release/](release/README.md) qualification
and publication. Preserve both Linux flavors and installed-only OTP integration.
All registered platforms remain visible even where infrastructure or ports fail.
Unexecuted checks never satisfy a gate. Keep private consumers outside release
inputs, CI and qualification; product fixtures remain independently owned.

Both Linux integration targets additionally require the registered emulator-suite
`beam-vm` report. Preserve unchanged upstream assertions and individual cases;
missing inputs, failures and skips cannot become passes. Source/seed reproduction,
source pins, owner configuration and unresolved OTP failures remain independent
gates. Native host tools are never installed into cross-target SDKs.

BEAM release identity follows integrated ERTS plus distribution revision, with
an explicit preview designation while experimental. Exact full identity/build
checks stay required. OTP version reporting remains product-owned; inherited
modern reporting is not automatically a permanent API. Stable source/C binary
compatibility requires explicit scope, acceptance and announced deprecations;
preview APIs remain experimental. [Release policy](release/README.md) and
[upstream synchronization](release/upstream-sync.md) own maintenance/support.
