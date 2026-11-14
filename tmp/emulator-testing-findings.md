# Emulator testing: findings and decisions

Updated 2026-11-14. This working document owns the current scope, decisions,
harness requirements and acceptance rules. The [progress index](emulator-testing-progress.md)
links the platform trackers and their execution evidence. Keep these documents
tracked in the repository's `tmp/` until the job is done.

The retained corpus and imported support use upstream pin
[`550d7b7898706c7822362c42e7b93120c1d1f29a`](../UPSTREAM_COMMIT).
Actual input fingerprints, rather than the pin alone, identify each execution.
No platform/configuration has an accepted complete ordered campaign.

## Agreed definition of full emulator testing

Run every reasonably applicable upstream functional test of the supported VM,
using the candidate VM, standard build dependencies and self-contained support.
Ordinary helpers, drivers, NIFs, test data, compiler configuration and local
networking are preparation responsibilities; their absence is a harness failure.

Exclude pure performance measurements and benchmarks. Other exclusions require
an agreed scope reason or an exact audited upstream platform, configuration or
resource guard. Timing output or a benchmark-like name does not justify dropping
functional assertions. Passing tests does not measure a percentage of VM correctness.

The campaign covers Linux glibc amd64, macOS arm64 and Windows amd64, each with
`emu`/`jit` in Release/Debug. Linux AArch64 is outside this job. Windows native
product and full-harness support are implemented; native full qualification is
in progress. The Linux full-harness port remains pending.

## Decisions to preserve

| ID | Current decision |
| --- | --- |
| D01 | Preserve the three CMake options, existing root CTest names/labels and exactly one root `emulator.full` entry. Keep individual suite entries in a separate diagnostic tree. |
| D02 | Use the functional scope above; no numerical coverage threshold substitutes for applicable execution. |
| D03 | Bind results to actual candidate/input fingerprints. Results from other inputs cannot qualify the current configuration. |
| D04 | Supply ordinary native fixtures, compiler configuration, local networking and private application/helper support. |
| D05 | Property frameworks, earlier OTP installations, LTTng, lock-counting builds and specialized profilers are outside this baseline. |
| D06 | N/A requires complete, exact audited skip-only accounting. Failures, automatic skips, missing targets and broken preparation remain failures. |
| D07 | Preserve retained upstream sources and the specification lifecycle; express agreed exclusions in project-owned configuration. |
| D08 | Keep the five-suite, 24-case, zero-skip gate independent of full testing. |
| D09 | Execute the built candidate with its matching helper/bootstrap. No discovered external `erl`/`erlc`, sibling distribution or fallback VM. |
| D10 | Keep support private, record identities/diagnostics, isolate execution and clean up only run-owned processes. |
| D11 | Keep separate platform/configuration records; another platform's passes or guards do not establish native coverage. |
| D12 | Preserve unrelated changes. Do not widen CI, install system dependencies, commit, push or publish without authorization. |
| D13 | Investigate likely harness causes first. Defer failures and continue only under an authorized discovery plan; otherwise stop and report an unresolved blocker. Ask before any upstream change, with a focused reproduction, identities, diagnosis and exact proposal. |
| D14 | Run complete suites through their CTest entries and groups/cases through the shared focused launcher. A case pass does not pass its suite. |
| D15 | Process the prepared inventory deterministically. After repairs settle, every applicable suite must pass and every N/A be audited on final inputs before one complete ordered campaign. |
| D16 | Whole `a_SUITE` or `z_SUITE` selection runs both complete suites, in that order, in one VM. Schedule the pair once and record both rows; reject partial endpoint selections. |

The upstream boundary includes retained VM/library sources, suites, drivers,
NIFs and imported files under `tests/emulator/support/`. A `tests/` path does not
make a source project-owned. Project-owned harness fixes are authorized for
qualification; changing upstream assertions, callback code or imported support
requires the user's agreement. Do not conceal failures through configuration
changes, invented exclusions or relaxed accounting.

Repository `tmp/` holds these documents. Disposable builds, installations and
logs belong in workspace `../tmp/`. Reserve `build/<preset-name>` and
`install/<preset-name>` for standard preset outputs.

## Current testing interface

| Option | Default | Effect when `BUILD_TESTING=ON` |
| --- | --- | --- |
| `BUILD_TESTING` | `ON` | Enables runtime checks and the optional integrations. |
| `BEAM_BUILD_EMULATOR_TESTS` | `OFF` | Adds private support and five standalone selected-suite entries. |
| `BEAM_BUILD_FULL_EMULATOR_TESTS` | `OFF` | Adds full support, one ordered root campaign and the separate diagnostic suite catalog. |

Enabled support targets participate in the default build. CTest executes tests;
it does not build missing support. Direct execution rejects stale preparation
and prints the rebuild command.

The root has two runtime entries (`runtime.build`, `runtime.install`), five
selected entries when enabled (`emulator.prim_eval`, `emulator.atomics`,
`emulator.counters`, `emulator.num_bif`, `emulator.process_max_heap_size`), and
`emulator.full` when enabled: eight entries with both integrations on.
`BUILD_TESTING=OFF` disables all of them. Full support does not implicitly enable
the five standalone selected entries.

Successful full preparation generates
`<build>/tests/emulator/full/suites/CTestTestfile.cmake`, currently containing
121 `emulator.suite.<SUITE>` entries. The root does not traverse that tree.
Both suite entries and the generated `run.py` use the same runner. An unfiltered
diagnostic-tree run supplies individual results, not ordered-campaign acceptance.

`check-emulator` and `check-emulator-full` build support before running their
groups. Use exact label expressions: `-L '^emulator$'` selects the independent
24-case gate; `-L '^emulator-full$'` selects the complete campaign.

### Focused diagnosis and complete campaigns

Use a dedicated full-enabled workspace build. Prepare explicitly, then select
one suite in the diagnostic tree:

```sh
cmake --build <build> --target beam_emulator_full_test_support
ctest --test-dir <build>/tests/emulator/full/suites -N
ctest --test-dir <build>/tests/emulator/full/suites \
  -R '^emulator[.]suite[.]atomics_SUITE$' \
  --output-on-failure --no-tests=error -j1
```

Use the launcher for a complete suite or a smaller faithful reproduction:

```sh
python3 <build>/tests/emulator/full/run.py --suite atomics_SUITE --timeout 300
python3 <build>/tests/emulator/full/run.py \
  --suite trace_call_count_SUITE --group legacy_pre_post --cases basic --timeout 300
```

`--suite` is required. One optional unambiguous `--group` preserves enclosing
callbacks, properties and descendants. `--cases` accepts comma-separated names;
cases inside groups require group context. Unknown, empty or ambiguous
selections fail before execution. Narrow requests intersecting excluded
benchmark/profiler scope are rejected rather than broadened.

The generated specification keeps base global settings and exactly the requested
selection. Receipts record the request, Common Test's resolved inventory and
specification hashes; auditing checks observed suites/cases, totals, identities,
skips and cleanup. Focused results are diagnostic and have a separate
`latest-focused-run.txt` pointer. `--timeout` defaults to 21600 seconds of
supervised candidate execution; setup and cleanup add wall time outside that cap.

For each native configuration:

1. Verify inputs and preparation, runtime build/install/staging/relocation and
   the independent five-suite, 24-case, zero-skip gate.
2. Freeze a deterministic plan from the prepared inventory. Run one unit at a
   time, with the whole a/z pair once. Record Passed, N/A, Failed/deferred or Not
   run with counts, fingerprint, command, logs and cleanup.
3. Record failures as red. Under an authorized defer-and-continue plan, finish
   the inventory before returning to them; otherwise stop and report an
   unresolved blocker. Diagnose through the smallest faithful group/case,
   checking fixtures, compiler flags, helpers, paths, environment, group context
   and pinned framework behavior first.
4. After an authorized repair, refresh affected support and rerun the complete
   affected suite. Reconfirm stale units on final inputs without restarting the
   whole inventory or corpus for each experiment.
5. Once all applicable suites pass and every N/A is justified on final inputs,
   run one complete ordered campaign:

```sh
ctest --test-dir <build> -R '^emulator[.]full$' \
  --output-on-failure --no-tests=error -j1
```

The complete campaign keeps `a_SUITE` first and `z_SUITE` last, with the corpus
between them in one VM. Investigate ordering/shared-state failures with focused
reproductions. Start each platform with `emu` Release; after acceptance, qualify
JIT Release and both Debug configurations separately.

### Audited N/A and endpoint-pair behavior

N/A diagnostics return 77 only when every requested target is accounted for by
an agreed exclusion or exact audited upstream user guard and the requested
target has no passed functional assertions. Validate source/guard, reason,
measured condition, planned and terminal coverage, identities and cleanup.
CTest uses `SKIP_RETURN_CODE 77` only
on diagnostic suite entries. N/A is distinct from Passed.

Missing execution, ordinary fixtures, resource discovery or targets; failures;
automatic skips; and unexplained skips remain failures. A companion suite's
passes cannot make an unexecuted target green. Both a/z outcomes must be
accounted for from their joint whole-suite invocation. The selected gate remains
zero-skip, and complete-campaign acceptance retains its strict rules.

## Corpus inventory and acceptance machinery

The corpus has 104 original suites plus ten no-optimization and seven
stripped-type variants: 121 suite identities. Variants follow the pinned upstream
build recipe using temporary module copies; retained sources remain unchanged.
This is the emulator corpus, not all Erlang/OTP applications or ERTS test trees.

CMake owns native fixtures; Erlang preparation owns corpus/data compilation,
variants and inspection metadata. Supply all 29 native/data recipes and the
matching floating-point probe. Complete execution uses the retained
`emulator.spec`; alternative benchmark specifications are outside this scope.

The [Windows foundation accounting review](../../tmp/windows-full-qualification/foundation-accounting-review.md)
records all 29 recipe dispositions, source-hashed native omissions, pinned
OSMon/eventlog closure, F02 observations and the legacy Windows specification
review. Its preparation snapshot predates subsequent launcher repairs; final
preparation and final-input execution remain pending.

The runner checks runtime identity, journal sequence/boundaries, Common Test
totals against events, exact suite inventory, planned counts, first/last suite
boundaries, exact skips and cleanup. Preparation and execution receipts bind
candidate/helper, bootstrap, sources/data/specifications, support, generated
outputs and native artifacts/flags. Preparation alone is not execution.

Implementation: [test entry](../tests/CMakeLists.txt),
[full integration](../tests/cmake/FullEmulatorTests.cmake),
[corpus preparation](../tests/emulator/beam_emulator_full_prepare.erl),
[focused selection](../tests/emulator/beam_emulator_focus.erl),
[supervision and acceptance](../tests/emulator/full_emulator.py),
[events and guard facts](../tests/emulator/beam_emulator_events.erl).
The [scope manifest](../tests/emulator/emulator_scope.json) and
[skip policy](../tests/emulator/emulator_skip_policy.json) are the current
machine-readable selections and guards; do not maintain a duplicate rule ledger.

## Implementation requirements and open work

F01-F05 are implemented harness behavior to validate on each platform/build.
Current macOS execution evidence belongs in its tracker; Debug and other native
platforms still need their own qualification. F06-F10 remain qualification work.

### F01

Supply fingerprinted compiler configuration for
[`port_SUITE:otp_3906`](../erts/emulator/test/port_SUITE.erl), which reads
`{'CC', CC}` from a `variables` file and builds a helper. Use the selected CMake
compiler and respect `ts_install_cth` lookup paths so preparation does not trigger
unsupported upstream Make machinery. Verify the actual case and helper cleanup.

### F02

On Unix, run `time_SUITE:univ_to_local` and `local_to_univ` with private-run
`TZ=Europe/Stockholm`, recording the effective timezone. These ordinary cases
must execute; do not change the host timezone or accept avoidable timezone skips.
The approved Windows exception preserves the host timezone: the unchanged
upstream guard reads `tzutil /g`, which a private `TZ` variable cannot override.
Only these two exact cases may match their pinned guard when a successful
native probe reports a timezone other than `W. Europe Standard Time`. Record
the native timezone ID and fresh event-time guard facts. Missing/failed probes
remain preparation failures; do not change the host or fabricate probe output.

### F03

Apply the explicit functional scope consistently in selection and accounting:

| Selection | Disposition |
| --- | --- |
| `hash_SUITE:phash2_benchmark` group | Exclude pure benchmark. |
| `estone_SUITE:estone` | Exclude scoring benchmark. |
| `map_SUITE:benchmarks` and `map_no_opt_SUITE:benchmarks` | Exclude timed throughput loops. |
| `jit_SUITE:perf` group | Exclude specialized profiler integration; retain ordinary JIT cases. |
| `hash_SUITE:phash2_benchmark_tests` group | Retain reduced-iteration correctness checks and expected hashes. |
| `binary_SUITE:iolist_size_benchmarks` group | Retain size/error assertions. |
| `pseudoknot_SUITE:test` | Retain numerical-result assertion. |
| `distribution_SUITE:message_latency` group | Retain fragmentation regression coverage. |
| `smoke_test_SUITE` | Retain startup/options/native-atomic/jump-table coverage. |

### F04

Audit Debug guards for `time_SUITE:now_update`,
`process_SUITE:processes_default_tab`/`processes_this_tab`, and
`nif_SUITE:consume_timeslice`. Match exact cases, source guards, reasons and
runtime conditions. Require applicable Release execution; do not accept arbitrary
skips from these suites or weaken Debug settings.

### F05

Match resource acceptance to the build and event-time measurements:

| Process case | Release requirement | Debug requirement |
| --- | --- | --- |
| `processes_large_tab` | 2 GiB | 4 GiB |
| `otp_7738_waiting`, `otp_7738_suspended`, `otp_7738_resume` | 3 GiB | 6 GiB |

The two process-table Debug setup skips belong to F04, not resource rules for
unreachable bodies. Match hash guards to the actual build type. Preserve the
binary huge-term guard's integer-GiB rounding and distribution's threshold of
strictly more than 6 GiB. Missing/broken resource discovery is a preparation failure;
genuine exceptional requirements, such as map's approximately 31 GiB, may exceed
a normal machine's resources.

### F06

Finish platform-specific guard review: dump scheduler availability/count,
scheduler binding/topology, dirty schedulers and the driver's extreme descriptor
wrapper. First supply ordinary preparation. Accept only exact measured limits,
never blanket unsupported/low-resource patterns. These review areas are not
preapproved exclusions.

### F07

Port full-harness support to Linux glibc amd64. Current
[native fixture rules](../tests/cmake/FullEmulatorNative.cmake) still reject Linux,
and its [process helper](../tests/emulator/emulator_process.c) implements macOS
ownership/listener inspection. Port linking, process identities,
listener ownership, launcher/resolver/helpers and safe cleanup. Verify epoll and
Linux functional cases natively; macOS platform skips do not establish coverage.

### F08

Native Windows MSVC x64 product/build foundations are restored. All four presets
build, pass runtime checks and install; 52 compiler/header checks pass. All four
selected gates pass 24 cases each with zero failures or skips (96 accepted case
executions).
The [Windows tracker](emulator-testing-progress-windows-amd64.md) records evidence.

Windows full private fixtures, standalone-executable launchers, owned EPMD,
Job Object containment, identity-checked cleanup, resource discovery and exact
native guard accounting are implemented. Initial native full preparation and
the independent baseline passed. Interpreter Release initial discovery is now
complete across 121 suite rows/120 execution units: **108 Passed, 7 N/A,
6 Failed/deferred, 0 Not run**. Its independent audit is consistent and all
run-owned process cleanup remainders are empty. These are historical discovery
results at fingerprint
`0545f77512645af2c27f10a89668ba1e78277d4f690c2554f5693bcffbe12abf`;
they do not qualify post-repair inputs or an ordered campaign.

The six deferred rows have source-backed diagnoses:

- `alloc_SUITE` and `driver_SUITE` need exact pinned native guard accounting.
  The post-discovery proposal adds eleven case-scoped rules with source hashes;
  unavailable mseg acceptance requires a successful fresh candidate query that
  returns `false`, with query errors kept distinct. No failed cases, automatic
  skips or new functional-scope exclusions are accepted by that proposal.
- `dirty_nif_SUITE` and `port_SUITE` expose overlong Windows attempt/private
  paths, including native CRT cwd/file limits and the retained quoted executable
  parser's stricter budget. Reproduce after a project-owned compact-path fix;
  preserve upstream fixtures, assertions and timeouts.
- `hash_SUITE` reproduces the known Config-marker defect: 14 automatic skips
  before correctness bodies run. An upstream repair remains unapproved.
- `smoke_test_SUITE:jump_table` conflicts with the deliberate MSVC interpreter
  switch-dispatch configuration. Startup/options and native atomics passed;
  the optimized jump-table assertion failed. No upstream skip guard applies.
  A scope exception or upstream test change requires the user's agreement.

Post-discovery support repairs, focused reproductions and whole-suite reruns
on final inputs are pending. Retain all six rows as failed until justified
repairs and complete-suite verification establish their outcomes. Reconfirm
other stale units on final inputs before the ordered campaign. F08 remains
open until native qualification is complete.
Preserve native semantics without a compatibility runtime or fallback VM;
product/selected/preparation passes do not establish full coverage.

The transferred Windows checkout uses Git CRLF working-tree conversion for
retained upstream files. Keep those source bytes unchanged. Policy source
identities bind the pinned LF bytes and may match either exact checkout bytes
or exactly CRLF-to-LF canonical bytes; no other transformation is accepted.
Record both identities when auditing a guard, and keep the raw checkout hashes
in preparation/execution input fingerprints.

### F09

Reconcile product guidance/CI descriptions separately from qualification. The
[Linux](../.github/workflows/linux.yml) and [macOS](../.github/workflows/macos.yml)
workflows enable selected tests in Release/Debug and leave full testing off;
README/guidance still contain differing CI descriptions. No full-corpus workflow
or CI expansion is authorized by this job.

### F10

Qualify every configuration on final inputs, recording complete execution and
actual durations. Refresh affected available standard builds/installations after
relevant source changes. Keep preparation, execution and total wall time separate;
the six-hour execution timeout is a cap, not an expected duration. Report
unavailable hosts and unrun checks accurately.

## Known hash-suite blocker

Original `hash_SUITE` puts the bare atom `phash2_benchmark_tests` into Config.
Pinned Common Test rejects that non-tuple entry in `init_per_testcase`, before
14 correctness-test bodies execute. The suite and framework sources match the
upstream pin, and staged/compiled files match preparation. This remains a failed
suite, not N/A or an authorized exclusion.

The [current macOS review](emulator-testing-progress-macos-arm64.md#upstream-review-required)
contains the whole-suite result, minimal reproduction and unapplied two-line
proposal: store the marker as a tuple and use a matching proplist reader,
preserving one-iteration behavior and expected hashes. An upstream edit and the
matching project-owned source fingerprint update require the user's agreement.
Windows interpreter Release initial discovery reproduced the same 14 automatic
skips with matching canonical suite/framework source hashes; see its platform
tracker. Neither platform's result authorizes an upstream edit or an exclusion.

## Optional facilities outside the baseline

- Property frameworks: the hash suite accepts PropEr or EQC; do not add these
  dependencies to satisfy the baseline.
- Earlier OTP comparison peers: absent OTP 28/27 fixtures may match exact guards;
  they are comparison inputs, never candidate runtime fallbacks.
- Lock counting: a separate build feature from Debug lock checking.
- LTTng: separate Linux tooling/provider support, not ordinary Erlang tracing.
- Specialized profilers: JIT's `perf` group remains excluded regardless of whether
  tools happen to be installed.

## Acceptance evidence for every completed configuration

| Field | Required record |
| --- | --- |
| Source and inputs | Source commit/working-tree state, upstream pin, actual fingerprints, complete bootstrap/imported support and native manifest |
| Runtime | Candidate/helper hashes, flavor, build type, runtime diagnostic facts and required aliases |
| Host and toolchain | OS/architecture, compiler/CMake/Ninja, SDK/sysroot or glibc identity; identify private support versus an installed public SDK |
| Environment | Timezone, owned loopback/EPMD, tool paths and measured guard/resource conditions |
| Selection | Scope/policy identities, requested/resolved inventory, specification hashes, exclusions, exact invocation and per-suite ledger |
| Result | Planned/terminal counts, accepted/rejected skips, diagnostics, audits, timings and evidence paths |
| Cleanup | Recorded run/process identities and no remaining run-owned processes |

Acceptance requires complete applicable execution, no failures, no automatic or
unexplained skips, matching final inputs and successful cleanup. Only the
complete ordered campaign closes the full-execution status. Keep runtime,
selected gate, preparation, diagnostics and complete-campaign results separate.
