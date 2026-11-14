# BEAM/Erlang repository guidance

This repository owns the shared VM and the modern BEAM/Erlang distribution.
Follow the parent workspace guidance and work on `master`. Preserve unrelated
changes; do not commit, push or publish unless requested.

## Upstream provenance

`UPSTREAM_COMMIT` records the upstream Erlang/OTP commit through which this
repository has been synchronized. Advance it for upstream synchronization, not
for local changes or product releases. It is provenance, not a product version,
an exact local source identity or a runtime compatibility identifier. Equal
upstream pins do not guarantee compatibility, and different pins do not establish
incompatibility. `BEAM_VERSION` records the product and runtime ERTS version,
currently `17.0`. Generate runtime version headers with the pinned `make_version`
recipe: OTP release `29`, OTP version `29.0` and ERTS version `17.0` are distinct
inputs. The upstream OTP identity is pinned in
`erts/emulator/cmake/ErlangGenerators.cmake`; update it during upstream
synchronization. Use `BEAM_VERSION` as `ERLANG_VERSION`.

The intended architecture separates the shared VM from independent OTP
applications through installed public interfaces. The current CMake product is
a minimal VM executable, plus the Unix child helper; an installed SDK, runtime
archive and returning embedding lifecycle are not implemented here. Do not introduce a
second VM, an external-runtime fallback or dependencies on a sibling product's
sources, build outputs or installations.

## Source layout

- Retained upstream Erlang/OTP sources keep their upstream-relative paths under
  root `erts/` and `lib/`. Do not assume a `src/` relocation has occurred.
- The upstream emulator suites, helpers and data remain together under
  `erts/emulator/test/`. Preserve their source bytes and remaining relative paths
  when integrating tests.
- The Unix process entry and required `erl_child_setup` helper remain under
  `erts/emulator/sys/unix/`. The Windows console entry is
  `erts/emulator/sys/win32/win32_beam_main.c`; do not link `sys/win32/erl_main.c`
  or define `ERL_RUN_SHARED_LIB`. Windows does not build `erl_child_setup`.
  Upstream `erts/etc/`, `erts/start_scripts/` and `erts/lib/internal/` are
  omitted from the supported runtime closure.
- Keep build support under `cmake/` or alongside its owning component, new
  project-owned tests under `tests/`, and the bootstrap fixture under root
  `bootstrap/`. Place tightly coupled additions beside their owning source and
  explain the placement in the source or build rule.
- Keep `tests/CMakeLists.txt` as the test entry point, with included CMake
  modules and script-mode drivers under `tests/cmake/`. Product runtime checks
  live under `tests/runtime/`; upstream emulator integration code, native process
  support, skip policy and restored dependencies live under `tests/emulator/`.
  Generated test outputs stay under `<build-dir>/tests/`.
- Platform detection and core-header configuration live under root `cmake/`,
  with `cmake/CoreHeaders.cmake` writing `config.h`,
  `include/erl_int_sizes_config.h` and
  `include/internal/ethread_header_config.h` under `<build-dir>/erts/`.
  Keep the sizes header unguarded and limited to the six upstream basic sizes;
  VM and NAPI types remain in their retained source-header owners. Configuration
  includes the common ethread macro fragment, with internal thread settings in
  `config.h`; it does not include size or atom-table headers. Bare sizes-header
  includes resolve through `<build-dir>/erts/include/`; do not recreate the
  obsolete copy at `<build-dir>/erts/erl_int_sizes_config.h`.
  Emulator generator rules live in
  `erts/emulator/cmake/ErlangGenerators.cmake`, beside the other emulator build
  modules; their outputs remain under `<build-dir>/emulator/<flavor>/`.
  The PCRE header generator and executable-alias installation template also
  live in `erts/emulator/cmake/`. Generated PCRE headers remain under
  `<build-dir>/erts/emulator/pcre/`, and the configured installation script
  remains at `<build-dir>/erts/emulator/InstallAliases.cmake`.
- `tests/emulator/support/` is the private test-support exception. Its `lib/`
  and `erts/` trees preserve pinned upstream application/helper sources, headers,
  native utility sources and version files without restoring them to the product
  closure or creating public SDK assets. Preserve upstream-relative paths,
  including `lib/kernel/src/logger_internal.hrl` for Common Test's relative
  include. Select shared imports in `tests/CMakeLists.txt` and additional full
  imports in `tests/cmake/FullEmulatorTests.cmake`; their generated
  `support-files.txt` lists stay in the build tree. Corpus compilation, variant
  and preload settings belong to `tests/emulator/beam_emulator_full_prepare.erl`.
  Preserve source provenance through Git and `UPSTREAM_COMMIT`; preparation
  fingerprints the actual inputs and direct test runs reject stale fixtures.
  Do not restore checked-in per-file hash inventories or unused upstream
  Makefiles, configure scripts and build templates. CMake owns native test build
  rules; keep the four `vsn.mk` inputs
  used for application metadata and the EI `config.h.in` template used for
  native feature detection.
- Preserve source origins, notices and meaningful names. Adapt executable paths
  and metadata deliberately; upstream reference text is not a path to rewrite
  indiscriminately. Keep this guidance and source/build path mappings current
  when changing layout.

## Build and validation

The [README](README.md#building-the-runtime-with-cmake) owns supported targets,
prerequisites, preset commands and installation behavior. Use the native
`linux-x86_64-{emu,jit}`, `linux-aarch64-{emu,jit}`,
`macos-arm64-{emu,jit}` or `windows-x86_64-{emu,jit}` presets for the matching
host and compiler architecture. Windows is native x64 MSVC only.
These select Release; the corresponding `-debug` presets select Debug with
separate standard output directories. Release remains the default. Support only
the single-config `Ninja` generator and the `Release` and `Debug` build types.
Reserve `build/<preset-name>` and `install/<preset-name>` for maintained standard
outputs and refresh affected, available native outputs after source changes.
Report stale or unvalidated outputs. Put experimental configurations, temporary
builds and installations, and validation payloads in workspace `tmp/`, including
variants based on standard presets; never repurpose a standard output directory
with task-specific options.

CMake reads retained sources and generator inputs from `erts/` and `lib/`.
The `erts` subdirectory keeps generated core headers and runtime build rules
under `<build-dir>/erts/`; flavor-specific generator outputs remain under
`<build-dir>/emulator/<flavor>/`. Runtime support libraries are built from
`erts/lib_src/`. The installed Unix product contains only the VM, child helper
and relative executable aliases. The installed Windows product contains `beam.exe`, its
flavor copies, and Debug program databases. The bootstrap and private test
support stay in the test closure.

Debug corresponds to upstream `TYPE=debug`, independently of `emu` or `jit`.
On Unix, keep `-g -Og`, `DEBUG` and active assertions consistent across
consumers of the internal ERTS target interface, including ethread and the
child helper. On Windows, use MSVC `/Od /Zi /UNDEBUG` with the same `DEBUG`
checks; there is no `erl_child_setup.debug` alias. Keep
the VM macro out of vendored libraries and the generator host tool, except for
the modified PCRE matcher's required Erlang diagnostic hooks. Match the
pinned allocator and yielding-C Debug generator recipes, and invalidate their
outputs when build type changes. Unix Debug publishes the required relative
`erl_child_setup.debug -> erl_child_setup` alias in build, installation and
private execution images. Windows Debug publishes program-database copies
beside the flavor executables. Retain the existing VM executable names.

The Linux, macOS, and Windows CI workflows use standard native presets and run
the runtime checks in both flavors. The Windows workflow loads the x64 MSVC
environment before configure and also runs the selected 24-case emulator gate.
Full emulator tests stay off in CI. Validate affected builds with CTest, including build-tree
execution, installation, `DESTDIR` staging and relocation. Preserve the existing
`runtime.build` and `runtime.install` tests. Report unavailable platforms and
unrun checks accurately. Report sandbox-denied network tests as failures until
an authorized run with loopback access establishes their outcome.
These checks must verify `{build_type, debug_compiled, lock_checking}` as
`{debug, true, true}` for Debug and `{opt, false, false}` for Release, including
installed, staged and relocated boots. Refresh affected native Release and
Debug standard outputs. The Windows CI matrix covers Release and Debug;
the Linux and macOS workflows retain their existing matrices.

`BEAM_BUILD_EMULATOR_TESTS` is an optional, default-OFF Common Test integration
for either candidate flavor. The selected set is the unchanged `prim_eval_SUITE`,
`atomics_SUITE`,
`counters_SUITE`, `num_bif_SUITE` and `process_max_heap_size_SUITE`: 24 cases in
total. Do not imply that passing this set establishes full emulator-corpus
coverage. Configure the option in workspace `tmp/`, build the private support
with the candidate VM and retained bootstrap compiler, and run `check-emulator`
or the CTest `emulator` label. Keep its five tests and 24-case, zero-skip gate
independent of the full-corpus integration.

Prepare shared syntax_tools/Common Test support through
`beam_emulator_harness:prepare_framework/1`. The selected-suite preparer adds
its five suites afterward; the full integration calls framework preparation
directly before compiling the complete corpus. Do not compile the selected
suites into the full image as an intermediate step. Keep selected preparation's
immediate module loading and full preparation's basename compile metadata
semantics separate.

Require complete selected-suite execution with no failed or skipped cases.
Record the candidate, bootstrap and pinned support identities with the results,
retain failure logs, and report all unvalidated flavors and platforms.

`BEAM_BUILD_FULL_EMULATOR_TESTS` independently defaults to `OFF`; its native
fixture implementation targets macOS arm64 and Windows amd64 MSVC with either
`emu` or `jit`. Windows full-corpus qualification is pending; successful native
fixture construction or preparation is not an accepted complete campaign.
Use a separate workspace `tmp/` configuration and `check-emulator-full` or the
CTest `emulator-full` label. Preserve the unchanged base `emulator.spec` in a
single ordered Common Test run, with `a_SUITE` first and `z_SUITE` last. Include
all retained suites and the exact upstream no-optimization/stripped-type
generation recipes. Honor all 29 corpus recipes, including their Erlang data
and probe steps. Native fixtures, EI, OS_Mon and launcher/resolver/heart/EPMD
helpers are private test outputs. Link native
extensions against callbacks in the candidate process, never a runtime copy.
Keep fixture `assert()` checks active despite the CMake Release configuration.
On Windows, build the native DLL callback tables against the candidate headers,
use `/UNDEBUG`, and isolate each suite's import-library/linker side products.
Use the candidate's generated headers and Win32 thread settings, including
Debug's internal-layout checks. Audit native recipes that do not apply on a
platform against the unchanged suite guards rather than compiling substitutes.

Windows full execution uses the retained upstream erlexec argument parser with
the project-owned Windows process backend to launch the exact standalone
`beam.exe`. The private launcher, process tool, EI, `heart.exe`,
`inet_gethost.exe`, `epmd.exe`, and Windows OS_Mon helpers belong only to the
test image. Preserve the pinned Windows helper imports under
`tests/emulator/support/`; never restore the upstream DLL VM lifecycle.
Use native Windows paths, process creation-time/owner/image identities and
per-run job containment for launch receipts and verified cleanup. Windows has
no child-setup executable or Debug helper alias.

Full preparation and execution receipts bind the candidate, the Unix child
helper when applicable, flavor, build type, complete bootstrap, imported sources,
corpus/data/specifications, generated artifacts, native source/flag manifest
and staged output hashes.
Inspection metadata compiled from matching preload sources belongs solely to
the full test image; preserve the retained executable chunks and seed bytecode.
Each execution uses its own image, owned loopback EPMD endpoint, process journal
and logs. Clean up only processes whose recorded run identity still matches,
and fail when owned processes remain.
On Unix, verify the Debug helper alias's relative target separately from the
helper binary hash.

Full execution may accept only individually audited upstream user skips whose
exact scope, source hash/guard, reason and observed runtime/platform/resource
condition match `tests/emulator/emulator_skip_policy.json`. Reject failed cases,
automatic skips, framework failures, incomplete event/coverage records and
unaudited skips. Missing applications, helpers, native fixtures or resource
discovery are preparation gaps, not resource-based skip justifications.
Distinguish successful preparation from an accepted complete corpus run.

For both integrations, do not discover an external `erl` or `erlc`, consume a
sibling distribution, alter suites or invent exclusions to obtain a pass.
Preserve the upstream specification's own hook policy. Keep all private support
out of production installation and do not widen CI coverage without a request.

Keep epoll/kqueue as the independently selected primary I/O backend on Unix.
Windows selects `WaitForMultipleObjects` and leaves kernel poll off. Poll
symbol/header availability alone does not establish fallback device usability;
changes to fallback selection need a native device-usability check. Preserve
upstream startup, emulator arguments, signals and process termination.
