# Build and CI architecture

The [target catalog](targets.json) owns orchestration: target IDs, preset names,
runner requirements, executors and publication roles. [CMake presets](../CMakePresets.json)
own compiler settings. `python3 ci/catalog.py check` rejects disagreement. The
catalog contains 36 routes: 34 platform routes and two Linux OTP integration
routes. Android has three build hosts, but only its Linux producer publishes.

| Target | Compiler | Flavors | Execution |
| --- | --- | --- | --- |
| Debian 13 AMD64/ARM64 | GCC/glibc | emu, jit | Native container |
| Alpine 3.24 AMD64/ARM64 | GCC/musl | emu, jit | Native container |
| Windows AMD64 | MSVC, MSYS2 UCRT64 GCC, MSYS2 CLANG64 | emu, jit | Native Windows |
| Windows ARM64 | MSVC, MSYS2 CLANGARM64 | emu, jit | Native Windows |
| macOS 15 Apple Silicon | Apple Clang | emu, jit | Native macOS |
| Android API 26 ARM64/x86_64 | NDK Clang on Linux, Windows, macOS | emu, jit | Android emulator |
| iOS 15 ARM64 device/simulator | Apple Clang on macOS | emu | Simulator; device compile/link only |

These are implemented build routes and intended support obligations, not a claim
that every platform port currently builds or passes. Platform compiler and runtime
failures remain visible. Current validation belongs in the
[architecture evidence](../../dev-documents/workflow-cmake-architecture.md).

## Everyday development

Select a configure preset, provide the local package checkout explicitly, then
use the matching build/test presets. Development builds and installations remain
under this repository's ignored `build/` and `install/` directories.

```sh
cmake --preset linux-emu -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package
cmake --build --preset linux-emu
ctest --preset linux-emu
cmake --install build/linux-emu
```

`linux-emu` and `linux-jit` remain compatibility entrypoints. Each has `-debug`,
`-relwithdebinfo` and `-asan` variants. `cmake --list-presets=all` lists the complete
matrix. `build.py --target ID --work DIRECTORY` uses the same presets, acquires
reviewed source inputs and transports SDK/test evidence. CMake never downloads
sources or falls back to external Erlang. Runner provisioning is owned here;
compiler flags and compilation commands belong in CMake.

PRs, pushes to `master`/`maint-*`, and merge queues run all main platform routes
and installed package/OTP integration. Matrices use `fail-fast: false` and keep
other targets running after a failure. Superseded PR runs may be cancelled.
Release operations are serialized and PR jobs receive no publication credentials.
Reports distinguish passes, failures, recognized exclusions, and unexecuted
infrastructure. An unexecuted required check never satisfies qualification.

## Workflow responsibilities

| Workflow | Trigger | Responsibility |
| --- | --- | --- |
| `checks.yml` | PR, push, merge queue, manual | Workflow syntax, preset/catalog agreement, tooling tests, local manual links |
| `ci-linux.yml` | PR, push, merge queue, manual | Debian/Alpine × architecture × flavor |
| `ci-windows.yml` | PR, push, merge queue, manual | All five compiler/architecture combinations × flavor |
| `ci-macos.yml` | PR, push, merge queue, manual | Apple Silicon × flavor |
| `ci-android.yml` | PR, push, merge queue, manual | Three build hosts × architecture × flavor; transported emulator tests |
| `ci-ios.yml` | PR, push, merge queue, manual | Device and simulator SDKs; simulator embedding |
| `integration.yml` | PR, push, merge queue, manual | Package acceptance and installed-only OTP compatibility on both Linux flavors |
| `qualification.yml` | Nightly, manual | Registered maintenance lines, whole matrix, sanitizers, repeated lifecycle tests, 16 KB Android |
| `release-candidate.yml` | Manual | Exact checkout, signing, archives, acquired qualification, evidence and attestations |
| `release-publish.yml` | Manual, protected environment | Verify and publish exact qualified bytes; emit index handoff |
| `containers-candidate.yml` | Recipe PR, weekly, manual | Verified SDK input, pinned base, image consumers and multi-architecture indexes |
| `containers-publish.yml` | Manual, protected environment | Promote tested OCI digests and approved channel tags |
| `upstream-audit.yml` | Daily, manual | Read-only upstream release-line comparison; no automatic merges |
| `_build-target.yml` | Reusable | Provision, configure, build, test, install, package one target |
| `_test-artifact.yml` | Reusable | Download, verify and test one SDK through its executor |
| `_build-container.yml` | Reusable | Build and test one image profile |

Schedules execute from the default branch. Qualification dispatches the branches
registered in [release policy](../release/policy.json), including maintenance
lines. A branch name alone never establishes support. A missing registered branch
is an actionable dispatch failure; audit jobs do not create branches.

## Infrastructure and credentials

Linux matrix jobs use native AMD64/ARM64 runners and digest-pinned Debian/Alpine
containers. Docker is orchestration here, not an alternate CMake recipe. Windows
provisioning selects MSVC or a separate MSYS2 compiler environment; delivered
executables are tested as native Windows programs outside an MSYS2 shell.
`toolchains.json` selects Android and MSYS2 inputs; actual compiler/dependency
versions are recorded in candidate evidence. MSYS2 package updates are recorded
inputs, not a promise of byte-reproducible native builds.

Set repository variable `BEAM_ANDROID_ARM64_RUNNER` to a dedicated Apple Silicon
runner label with Android command-line tools and usable hardware virtualization.
Use disposable isolated runners for untrusted PR jobs; never expose developer
machines or signing credentials. The complete ARM64 emulator route is retained
when the variable is absent and produces an explicit unexecuted report.
[GitHub-hosted ARM64 macOS runners lack nested virtualization](https://docs.github.com/en/actions/reference/runners/github-hosted-runners#limitations-for-arm64-macos-runners).
Android x86_64 executes on Linux. Extended Android acceptance checks both 4 KB and
16 KB pages on each ABI. iOS device execution is explicitly untested; simulator execution is
separate evidence, not physical-device validation.

Candidate jobs use the protected `release-signing-windows` and
`release-signing-apple` environments; ordinary builds
use the credential-free `ci` environment. Restrict signing to registered protected
branches and required reviewers. Set environment secrets `BEAM_SIGN_CERTIFICATE`
(base64 PKCS#12) and `BEAM_SIGN_PASSWORD`. macOS also requires
`BEAM_KEYCHAIN_PASSWORD`, `BEAM_NOTARY_KEY` (base64 API key),
`BEAM_NOTARY_KEY_ID`, and `BEAM_NOTARY_ISSUER`. Environment variables
`BEAM_SIGN_IDENTITY`, `BEAM_NOTARY_PROFILE` and `BEAM_TIMESTAMP_URL` select the
identity, notary credential profile and Windows timestamp service. Keep each certificate in its OS-specific environment. `signing.py` imports disposable
credentials and removes them even on failure; `finalize.py` signs/notarizes before
hashing. iOS frameworks are signed by the consuming application.

Publication uses the separately protected `release` environment. Configuration,
release-owner review and unresolved compatibility gates are documented in
[release operations](../release/README.md). No workflow addition configures those
external services or makes a candidate eligible.

## Independent tool bootstrap and prepared consumers

The protected `sdk-snapshot.yml` workflow builds canonical installed SDKs with
`BEAM_BUNDLE_PACKAGE_TOOL=OFF`. Desktop bootstrap qualification includes installed
compilation and unrelated generic applications; mobile SDKs retain target-layout
checks. Source-attested compiler seed reproduction remains a build prerequisite.
`sdk_snapshot.py` exports immutable CI-only HTTPS prerelease assets identified by
source revision, workflow run and attempt, with manifest/archive sizes and hashes.
`ci/sdk-snapshot-policy.json` disables publication until a separate operational
activation. These snapshots do not satisfy product or OTP release gates.

Package CI then builds and qualifies its independent payload against installed
inputs. Normal distribution production selects reviewed package sources, bundles
the tool through its package-owned recipe, and reacquires/qualifies the actual
bundled bytes through the existing artifact pipeline. Source pins must be updated
to real reviewed commits after this migration; local dirty checkouts are not pins.

The exported `beam-consumer-v1` interface accepts a `consumer.json` inventory and
prepared native bundle. `ci/executor_v1.py` verifies file hashes and runs bundle-owned
assertions through owned Android/iOS provisioning. Existing BEAM mobile tests retain
their assertions and share only this transport. Android consumers require 4 KB and
16 KB execution. iOS simulator execution and device compilation/linking are distinct;
physical device execution remains untested. This interface exports no test fixtures
for package production code to import.
