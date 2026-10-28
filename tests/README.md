# Distribution tests

These tests cover the shared VM, public SDK and modern BEAM distribution.
OTP owns its command, application, startup and native-service acceptance in its
separate product. Both products use the same VM through explicit composition.
The permanent requirements are in [AGENTS.md](../AGENTS.md#permanent-acceptance-contracts).

## Disposable output

Installed runners remove their copied prefixes, consumer builds and bundles on
completion, including failures and catchable interruption. CTest retains command
output; each test directory retains a `cleanup.json` record and bounded text
diagnostics (up to 8 MiB total, 1 MiB per file). Oversized structured records are
omitted rather than truncated into invalid data. Set `BEAM_TEST_KEEP_WORK=1` only
for active debugging, then remove the retained directory after investigation.
Selected input SDKs and normal development builds are not cleanup targets.

On Darwin, `test-support` also qualifies a test-only Seatbelt filesystem-denial
backend against usable files, executables and native libraries, including symlink
aliases and descendants. Failure to establish the sandbox is a failure, never
permission to retry without isolation. This pilot does not establish process
namespace isolation or complete installed-product isolation.

The test-owned native host observer measures descriptor sets, thread counts,
child PIDs and executable mappings. Its regression creates and removes actual
resources and requires restoration; unavailable or incomplete observations fail.
Darwin uses Mach and process-inspection interfaces, confined to test support.
Linux retains procfs observations. The observer is not installed or exported as
an SDK interface. Bare lifecycle clients use these Darwin observations without
changing their existing cleanup assertions.

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

These three Erlang Common Test wrappers are not registered with CTest. The native
`ethread` fixture has the separate integration described below; `erl_print_SUITE`
and `parallel_messages_SUITE` remain unexecuted. Their assertions, fixtures and
historical path text remain unchanged.
`vm/emulator/` retains the emulator corpus previously under
`src/erts/emulator/test/`: suites, helpers, native fixtures, data and legacy
specifications preserve their bytes and modes. Tool-local tests remain with
their tools. Source ownership and relocation alone establish no execution.

### Installed OTP host for emulator suites

The standalone Linux [Common Test project](vm/common_test/CMakeLists.txt) registers
an initial selection from `vm/emulator/`. It consumes an explicitly selected
installed OTP product and matching installed BEAM SDK, with producer provenance.
Common Test supplies the test environment; the assertions cover shared VM
behavior. Ordinary BEAM configure/build/CTest remains independent of OTP.

| Suite | Selected coverage |
| --- | --- |
| `atomics_SUITE` | Signed/unsigned operations, limits and argument diagnostics. |
| `counters_SUITE` | Operations, limits, independence and write concurrency. |
| `bs_bincomp_SUITE` | Byte/bit comprehensions and tracing. |
| `bs_match_tail_SUITE` | Aligned, unaligned, empty and large bitstring tails. |

Each suite runs its complete `all/0` selection unchanged. The runner requires
the expected case identities and individual successful results; failed, skipped,
missing or duplicate cases fail acceptance. Fixtures compile and execute in
private scratch using the selected installation, without system Erlang or source
runtime fallback. Selected SDK/OTP inputs remain read-only. Reports retain suite
and helper hashes, runtime identities and bounded failure diagnostics; disposable
compiled fixtures and processes are cleaned up on completion or interruption.

Configure this separate project from the repository root, supplying the exact
SDK version/build ID and OTP producer-provenance record for the selected pair:

```sh
cmake -S tests/vm/common_test -B build/vm-common-test-emu \
  -DBEAM_SDK_ROOT=/path/to/installed-beam-sdk \
  -DBEAM_VM_TEST_OTP_PREFIX=/path/to/installed-otp \
  -DBEAM_VM_TEST_OTP_PROVENANCE=/path/to/otp-provenance.json \
  -DBEAM_VM_TEST_SDK_VERSION=17.0-r1-preview.1 \
  -DBEAM_VM_TEST_SDK_BUILD_ID=EXPECTED_BUILD_ID \
  -DBEAM_VM_TEST_FLAVOR=emu
ctest --test-dir build/vm-common-test-emu --output-on-failure
```

Repeat with the JIT pair, `BEAM_VM_TEST_FLAVOR=jit` and a separate build directory.
There is no runtime discovery or automatic installation. CTest names are
`vm-ct-<suite>`; each uses a fresh process with two schedulers, a 600-second
deadline and serial execution. `results/<suite>/result.json`, bounded logs and
`cleanup.json` remain under the selected harness build directory.

Both Linux integration targets require the separate `beam-vm` report in
[release qualification](../release/README.md#candidate-production-and-qualification).
This selection does not execute the rest of the emulator corpus, the legacy
benchmark specifications or the ordered `a_SUITE`/`z_SUITE` lifetime checks.
Each invocation records its own exact inputs and case outcomes. Retained suite
source alone establishes no execution or compatibility result.

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

Both test presets enable failure output and run the complete ordinary BEAM suite.
The installed-OTP emulator selection has its separate integration entry point
above. CI runs each required suite. Python 3.9 or newer, the native C/C++ toolchain, CMake,
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
suite creates its own prefix using self-contained fixtures and no system Erlang.

| CTest name | Sources and fixtures | Contract |
|---|---|---|
| `api-validation` | `checks/api_validation.c` | Public argument/state validation, initialization, composition and generic lifecycle. |
| `bytecode-verifier` | `checks/bytecode_verifier.c` | SHA-256 known answers, build identity, selected-module validation and altered-byte rejection without a VM lifecycle. |
| `cli-smoke` | `checks/cli_smoke.cmake` | Modern command dispatch, obsolete-selector rejection, diagnostics and literal program arguments. |
| `installed-sdk` | `installed/sdk/consumer/`, `installed/sdk/layout.py` | Required public assets and selected inventories; fresh/relocated C/C++ consumers through CMake and pkg-config; installed manuals and copied first-call example; runtime-only SDK; native PCRE coexistence; bytecode integrity before consumer configuration and after compilation. |
| `installed-runtime-artifacts` | `installed/sdk/runtime_artifacts/` | Generic runtime, child-helper, public-header and core-inspection artifact selection, deployment, integrity and relocation. |
| `installed-compile` | `installed/cli/compile/` | Modern compiler diagnostics, transforms, dependencies, Make/Ninja builds, literal paths, missing tooling and repeated compiler VM cleanup. |
| `installed-run` | `installed/cli/run/`, `fixtures/hello_world.erl` | Fresh/relocated/minimal source execution; ordinary I/O, Unicode arguments, explicit services, missing assets and compiler/execution isolation. |
| `installed-interactive` | `installed/cli/interactive/`, `installed/cli/scenarios/` | OTP-free shell/eval and minimal deployment; terminal editing/history/functions, term I/O, recovery/status, common CLI missing/malformed-asset rejection and repeated lifetimes. |
| `native-artifacts-beam` | `checks/native_artifacts.cmake` | Modern native-wrapper source/artifact agreement through the owned bootstrap. |
| `compiler-artifacts` | `checks/compiler_artifacts.py` | Owned compiler source/seed attestation, exact reproduction and rejection of invalid inputs. |
| `native-component-boundary` | `checks/native_component_boundary.py` | Source/object ownership, public returning-helper boundary, native imports/ELF dependencies and exactly nine VM payloads; ordinary base, boot and services payloads stay outside `libbeam`. |
| `installed-runtime-beam` | `installed/cli/baseline/`, `installed/cli/scenarios/` | Fresh/relocated modern shell/eval, module/native provenance and direct program execution. |
| `installed-bare-beam` | `installed/runtime/bare/`, local `fixtures/` | Generic public SDK lifetimes, dedicated-thread ownership, asynchronous calls/pump/wake/stop, explicit base composition, copied results, module controls, callbacks, process/terminal state and real resource cleanup without runtime assets. |
| `installed-returning` | `installed/boot/returning/`, local `fixtures/` | Full/minimal relocated component profiles; preparation and retained mappings, copied outcomes, code/output/services and repeated cleanup. |
| `installed-deployment` | `installed/sdk/deployment/` | Generic bare and modern returning bundles; exact inventories, missing assets, Unicode relocation and execution with the SDK hidden. |
| `installed-components` | `installed/runtime/components/` | Relocated bootless file, network and combined compositions; passive support, activation dependencies/state, fault paths and actual service/resource observations. |
| `installed-process-owned` | `installed/runtime/process_owned/` | Generic process-owned roots, context, admission and status behavior through the public SDK, independent of product boot policy. |
| `test-support` | `support/`, `installed/boot/returning/etf{,_test}.c` | Process-group cleanup, environment/output isolation, asset restoration and bounded ETF decoding; no runtime substitute. |
| `installed-pkg` | `installed/cli/pkg/` | Alias/command policy, literal arguments, builds, installed projects and project shell in fresh and relocated prefixes. |
| `installed-nifs` | `installed/runtime/nifs/` | Explicit public NIF composition, linked activation and conflicts, preparation/activation timing, shared/static hosts, fresh-VM isolation, resources and deployment through installed SDK consumers. |

Core and base deterministic reproduction is mandatory in the build through
`beam_verify_core_artifacts` and `beam_verify_base_artifacts`. CTest does not
repeat those successful compilations. Compiler attestation and negative-input
checks remain separate.

The native entries cover `create_join_thread`, `equal_tids`, `mutex`,
`try_lock_mutex`, `cond_wait`, `broadcast`, `tsd`, `spinlock`, `rwspinlock`,
`rwmutex`, `atomic`, `dw_atomic_massage` and `thread_name`. The existing
`test-support` entry also checks the native adapter's result protocol,
diagnostic retention and timeout cleanup using synthetic helper processes.

OTP owns command, application, startup-observer and host-preparation acceptance,
including fresh/relocated/isolated execution, additional preloads, restart,
native services and hostname lookup. See its [compatibility contract](../../erlang-otp/docs/compatibility.md)
and [test preparation](../../erlang-otp/docs/test-preparation.md). Generic
composition and sequential VM lifetimes remain covered here.

## Ownership and coverage

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
an SDK header nor a runtime asset.

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

Shared-runtime pkg-config checks select `BEAM_TEST_PKG_CONFIG` and the producer's
C/C++ compilers explicitly. They clear inherited pkg-config search overrides and
build against a runtime-only copied prefix, including space/Unicode paths. MSVC
uses native pkgconf syntax; consumers own RPATH or application-local DLLs. Static
linkage and SDK support-source integrity retain their existing CMake owners.

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
adapter records compile/link acceptance and physical execution as untested; the separate real-application fixture owns physical-device execution.

To check an acquired Android SDK without an emulator, select its transported
candidate record and matching SDK/support archives, and an explicitly installed
NDK through `ANDROID_NDK_ROOT`:

```sh
python3 tests/target/execute.py --build-only \
  --record "$PAYLOAD/candidate-$TARGET.json" \
  --work "$WORK/consumer-build" \
  --report "$WORK/$TARGET-consumer-build.json"
```

This verifies archive hashes, extracts to a fresh path containing spaces and
Unicode, and configures/builds/installs the existing first-call, API and component
deployment clients through the installed SDK toolchain. Success returns zero
with `kind: consumer-build`, `status: passed`, `eligible: false` and
`target_execution: untested`. It never invokes an Android executor. `--extended`
and `--build-only` are mutually exclusive. Disposable inputs and build products
are cleaned on success, failure and catchable interruption; bounded diagnostics
remain below `--work`. `BEAM_TEST_KEEP_WORK=1` is an explicit debugging override.
Existing acquired-runtime and application reports remain mandatory for releases.

The mobile adapter reuses first-call, API validation and deployment fixtures,
including file/network/zlib, repeated lifetimes, missing assets, bytecode identity
and recovery in a copied deployment. Execution reports distinguish the actual
platform and exercised cases. Never classify missing coverage as a pass.
The independent `ios-app/` fixture owns real signed Release app installation,
main-thread responsiveness, idle callbacks, package static NIF/resources, explicit
components, repeated VMs and foreground/background reconstruction. It prepares
missing/corrupt bytecode variants before signing. The release adapter binds this
evidence to acquired SDKs and requires physical-device execution without a
debugger. The gate uses independent product-owned fixtures. Simulator runtime and
device selection are explicit; deployment minimum and observed OS are separate.
Disabled infrastructure produces an unexecuted report and blocks qualification.
`ci/test_catalog.py` owns route selection, archive extraction, build-only consumer
dispatch/failure/cleanup and host manifest
boundary tests; `release/test_release.py` owns release evidence and publication
refusal/retry behavior. Neither suite claims target execution.

Android application acceptance owns signed APK/AAB installation, lifecycle and native closure checks in [android-app](android-app/README.md). The installed-input rejection check, `target/android_delivery.py`, verifies ABI/API/NDK/STL mismatches, page alignment, writable executable segments, dependency closure and reserved system-library names without executing target binaries.
