# Distribution tests

These tests cover the shared VM, public SDK and modern BEAM distribution.
OTP owns its command, application, startup and native-service acceptance in its
separate product. Both products use the same VM through explicit composition.
The permanent requirements are in [AGENTS.md](../AGENTS.md#permanent-acceptance-contracts).

## Test layout

`installed/` contains installed SDK and modern distribution acceptance,
`checks/` contains API, artifact and implementation-boundary checks, and
`support/` and `fixtures/` hold their shared harness code and inputs.

`vm/` retains three upstream shared-VM suites with their adjacent data directories:

| Suite | Subject |
| --- | --- |
| `erl_print_SUITE` | Term rendering and native formatting. |
| `ethread_SUITE` | Native threads, locks and atomics. |
| `parallel_messages_SUITE` | Messaging, signals and message-queue stress benchmarks. |

The Erlang Common Test wrappers are not registered with CTest. The native
`ethread` fixture has the separate integration described below; `erl_print_SUITE`
and `parallel_messages_SUITE` remain unexecuted. Their assertions, fixtures and
historical path text remain unchanged.
The separate `src/erts/emulator/test/` tree and tool-local tests remain in place;
none of their retained suites is registered here.

### Native thread-library cases

Thirteen `vm-ethread-<case>` entries run the unchanged C fixture against the
same `ethread` library and generated configuration used by the VM. These are
internal implementation tests with private headers, not installed SDK clients.
They need neither Common Test, EUnit, an OTP installation nor a running VM.
The helper is built only for testing and is not installed.

Each entry runs a fresh process with a 600-second deadline and process-group
cleanup on failure/timeout; CTest allows 620 seconds for cleanup. Entries run
serially and carry `vm`, `native` and `shared` labels. Raw stdout, stderr and
`result.json` are kept under `build/<preset>/tests/vm/ethread/<case>/` (or the
corresponding explicitly selected build directory). A pass requires exit zero
and exactly one success marker. Malformed/conflicting markers, abnormal exits,
failure markers and unexpected skips fail the test.

The current production configuration does not enable pthread thread-name
support. Its explicit `thread_name` unsupported-capability result is reported
as a CTest skip, never as a pass. No other skip is accepted. `detached_thread`
(51,200 creations in batches of 64) and `max_threads` (repeated thread exhaustion)
remain outside the pilot pending a dedicated stress-execution design. Their
source and constants are preserved. Native fixture coverage does not establish
execution of the Common Test wrapper or VM-lifecycle coverage.

## Running tests

From the repository root, configure and build each flavor before testing it:

```sh
cmake --preset linux-emu
cmake --build --preset linux-emu
ctest --preset linux-emu
cmake --preset linux-jit
cmake --build --preset linux-jit
ctest --preset linux-jit
```

Both test presets enable failure output and run the complete suite. CI also
runs complete suites. Python 3.9 or newer, the native C/C++ toolchain, CMake,
Ninja, Make, `nm`, `readelf`, pkg-config, system PCRE2 development files,
POSIX terminals and loopback networking are needed.
Network permission failures are failures, not reasons to skip a scenario.

For development, select by stable CTest name or by label:

```sh
ctest --preset linux-emu -R '^installed-returning$'
ctest --preset linux-jit -L terminal
ctest --preset linux-emu -L SDK
```

Labels describe layer (`checks`, `installed`), interface (`SDK`, `CLI`), scope
(`shared`, `BEAM`), and requirements (`terminal`, `network`). A test can carry
several labels. `shared` includes VM/SDK contracts used by independently composed
hosts; no test selects an OTP runtime family in the BEAM command.
Use focused checks for local edits and test-only refactors. Complete both-flavor
and affected cross-product validation at the integration gates described in
[AGENTS.md](../AGENTS.md#permanent-acceptance-contracts).

## Contract map

Paths below are relative to this directory. A runner lives beside its client
project and fixtures. All generated clients, installations and logs remain under
`build/<preset>/tests/` at their established output locations. Each installed
suite creates its own prefix; none reads the playground or a system Erlang.

| CTest name | Sources and fixtures | Contract |
|---|---|---|
| `api-validation` | `checks/api_validation.c` | Public argument/state validation, initialization, composition and generic lifecycle. |
| `bytecode-verifier` | `checks/bytecode_verifier.c` | SHA-256 known answers, build identity, selected-module validation and altered-byte rejection without a VM lifecycle. |
| `cli-smoke` | `checks/cli_smoke.cmake` | Modern command dispatch, obsolete-selector rejection, diagnostics and literal program arguments. |
| `installed-sdk` | `installed/sdk/consumer/`, `installed/sdk/layout.py` | Required public assets and selected inventories; fresh/relocated C/C++ consumers and first-call example; runtime-only SDK; native PCRE coexistence; bytecode integrity before consumer configuration and after compilation. |
| `installed-runtime-artifacts` | `installed/sdk/runtime_artifacts/` | Generic runtime, child-helper, public-header and core-inspection artifact selection, deployment, integrity and relocation. |
| `installed-compile` | `installed/cli/compile/` | Modern compiler diagnostics, transforms, dependencies, Make/Ninja builds, literal paths, missing tooling and repeated compiler VM cleanup. |
| `installed-run` | `installed/cli/run/`, `fixtures/hello_world.erl` | Fresh/relocated/minimal source execution; ordinary I/O, Unicode arguments, explicit services, missing assets and compiler/execution isolation. |
| `installed-interactive` | `installed/cli/interactive/`, `installed/cli/scenarios/` | OTP-free shell/eval and minimal deployment; terminal editing/history/functions, term I/O, recovery/status, common CLI missing/malformed-asset rejection and repeated lifetimes. |
| `native-artifacts-beam` | `checks/native_artifacts.cmake` | Modern native-wrapper source/artifact agreement through the owned bootstrap. |
| `compiler-artifacts` | `checks/compiler_artifacts.py` | Owned compiler source/seed attestation, exact reproduction and rejection of invalid inputs. |
| `native-component-boundary` | `checks/native_component_boundary.py` | Source/object ownership, public returning-helper boundary, native imports/ELF dependencies and exactly nine VM payloads; ordinary base, boot and services payloads stay outside `libbeam`. |
| `installed-runtime-beam` | `installed/cli/baseline/`, `installed/cli/scenarios/` | Fresh/relocated modern shell/eval, module/native provenance and direct program execution. |
| `installed-bare-beam` | `installed/runtime/bare/`, local `fixtures/` | Generic public SDK lifetimes, explicit base composition, copied results, module controls, callbacks, process/terminal state and real resource cleanup without runtime assets. |
| `installed-returning` | `installed/boot/returning/`, local `fixtures/` | Full/minimal relocated component profiles; preparation and retained mappings, copied outcomes, code/output/services and repeated cleanup. |
| `installed-deployment` | `installed/sdk/deployment/` | Generic bare and modern returning bundles; exact inventories, missing assets, Unicode relocation and execution with the SDK hidden. |
| `installed-components` | `installed/runtime/components/` | Relocated bootless file, network and combined compositions; passive support, activation dependencies/state, fault paths and actual service/resource observations. |
| `installed-process-owned` | `installed/runtime/process_owned/` | Generic process-owned roots, context, admission and status behavior through the public SDK, independent of product boot policy. |
| `test-support` | `support/`, `installed/boot/returning/etf{,_test}.c` | Process-group cleanup, environment/output isolation, asset restoration and bounded ETF decoding; no runtime substitute. |
| `installed-pkg` | `installed/cli/pkg/` | Alias/command policy, literal arguments, builds, installed projects and project shell in fresh and relocated prefixes. |
| `installed-nifs` | `installed/runtime/nifs/` | Explicit public NIF composition, activation rejection, resources and deployment through installed SDK consumers. |

Core and base deterministic reproduction is mandatory in the build through
`beam_verify_core_artifacts` and `beam_verify_base_artifacts`. CTest does not
repeat those successful compilations. Compiler attestation and negative-input
checks remain separate.

The native entries cover `create_join_thread`, `equal_tids`, `mutex`,
`try_lock_mutex`, `cond_wait`, `broadcast`, `tsd`, `spinlock`, `rwspinlock`,
`rwmutex`, `atomic`, `dw_atomic_massage` and `thread_name`. The existing
`test-support` entry also checks the native adapter's result protocol,
diagnostic retention and timeout cleanup using synthetic helper processes.

The retired `installed-runtime`, `installed-otp-compat`, OTP boot/native artifact
and startup-observer entries are not runnable BEAM tests. Their functional owners
are OTP's `otp-command-policy`, `otp-compatibility-transfer`, `otp-boot-observer`,
`otp-host-preparation`, product probes and native acceptance. OTP fixtures retain
fresh, relocated and isolated execution, thirteen additional preloads, sticky
module/restart semantics, applications, native services and hostname lookup.
See the [ownership record](../../dev-documents/otp-ownership-cutover.md) for the
transfer and its validation limits. Duplicate bare-family variants are replaced
by explicit composition and sequential VM coverage; family selection and old
installation placement are not compatibility contracts.

## Ownership and evidence

Public SDK usability and integrity belong to `installed-sdk`; common CLI
acquisition failures belong to `installed-interactive`. Input variations in the
bytecode verifier belong to `bytecode-verifier`. Bare/returning lifetime suites
own copied-input ownership and real resource observations. Smaller deployment
suites retain exact selected inventories, missing-asset rejection and relocation
without SDK fallback. Expected selected inventories remain independent of
production manifests; changing a manifest must not silently redefine a contract.
Use clean build/install trees rather than testing upgrades from obsolete layouts.

`support/bytecode_input.h` owns test acquisition of explicitly selected base,
boot and services inventories. Native
fixtures receive its directory explicitly as `TEST_SUPPORT_DIR`; it is neither
an SDK header nor a runtime asset. Playground examples remain self-contained.

Public consumer projects use `find_package(BEAM CONFIG REQUIRED)`, installed
headers and installed exports. They must remain evidence that an outside client
can use the SDK. Private CLI acquisition recipes in `support/PrivateCLI.cmake`,
the compiler/CLI resource probes and malformed native descriptor fixture
serve different purposes. OTP owns its startup-observer instrumentation. They name their
source/build inputs explicitly and still exercise the production VM. They do
not establish public SDK interfaces.

The terminal and provenance scenarios are reusable domain checks, not acceptance
entry points. Baseline, reduced, relocated and minimal executions remain in
their owning runners. Do not remove a scenario merely because its implementation
is shared. Compiler cases are separated into common helpers, command basics,
dependency/transform behavior, owned compiler lifetimes and build-tool integration.
Returning C acceptance shares one source list across its two host executables;
the dispatcher preserves each mode's system and VM lifetime.

## Harness rules

Python runners execute as modules with `-B` from the test source root. CMake
passes suite source and build roots explicitly. Installed commands use explicit
working directories unrelated to their prefixes and a filtered environment with
isolated HOME/XDG locations. Normal tool use has `/usr/bin:/bin`; minimal/bare
profiles explicitly replace PATH with an empty directory. Runtime, loader and
SDK lookup overrides inherited from the caller are removed.

`support.process` bounds subprocess execution, captures byte or text output,
and preserves each caller's accepted statuses and deadlines. On timeout or
interaction failure it kills the process group and reaps the direct child.
Successful exit does not trigger group cleanup: that could mask a runtime leak.
PTY support retains terminal-state checks and captured diagnostics. Asset hiding
must restore files after exceptions, including partially completed hiding.

The returning acceptance ETF reader is local test code, not an SDK codec. It
checks explicit tuple/list shapes, integer/binary/UTF-8 atom values, proper tails
and complete consumption with bounded cursor operations. Its hand-authored tests
include truncation, oversized lengths, integer boundaries, wrong tags/shapes,
improper tails, trailing bytes and deceptive values inside binary payloads.
Resource acceptance must continue observing actual descriptors, children and
threads as well as VM accounting; printed constants and successful harness
cleanup are not evidence of runtime cleanup.

## Platform orchestration and cross builds

The [CI catalog](../ci/README.md) selects complete build/test presets. Native
builds retain the full suite above. Cross builds register `target-sdk-layout`
for host-side installed inventory verification and produce embedding inputs with
native imported compiler tools. They do not execute target code during generation.
`target/execute.py` verifies transported SDK/support archives and builds public
SDK consumers before Android emulator or iOS simulator execution. The device
route records compile/link acceptance and physical execution as untested.

The mobile adapter reuses first-call, API validation and deployment fixtures,
including file/network/zlib, repeated lifetimes, missing assets, bytecode identity
and recovery in a copied deployment. See the
[architecture evidence](../../dev-documents/workflow-cmake-architecture.md) for
actual platform execution evidence. Never classify missing coverage as a pass.
Disabled infrastructure produces an unexecuted report and blocks qualification.
`ci/test_catalog.py` owns route selection, archive extraction and host manifest
boundary tests; `release/test_release.py` owns release evidence and publication
refusal/retry behavior. Neither suite claims target execution.
