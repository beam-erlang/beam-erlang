# Build maintenance and compiler bootstrap

CMake owns configuration order, compiler settings, feature probes and product
composition. Component recipes under `src/beam/` own source/output inventories;
shared helpers live here. [Building the SDK](../docs/getting-started/building.md)
owns contributor commands, [tests](../tests/README.md) owns acceptance, and
[release operations](../release/README.md) owns source acquisition and artifacts.

## Build ownership

The root configures project options and target capabilities, the nine-module
core inventory and matching-build identity before assembling modern components
and the emulator. The modern product owns its command hosts and distribution
aggregate; the emulator owns VM objects, core preloads and core verification.
Compiler hosts and deterministic reproduction remain with the compiler recipe.
Forward target references preserve this dependency graph without executing
target binaries during cross compilation.

Installed consumer helpers and package templates live under [sdk/](sdk/).
Their installed locations under `lib/cmake/BEAM` remain independent of this
source organization. Platform facts, common capability probes and toolchains
retain separate owners. Emulator-only generators live beside the emulator's
other build helpers. Documentation staging is implemented in
[`tools/stage-documentation.py`](../tools/stage-documentation.py).

Base, boot and services publish explicit private inventory properties on their
producer targets. SDK installation reads those records, and the same module
lists configure consumer metadata. Cataloged file installation declares its
destination and artifact record together; exported targets retain ordinary
CMake installation rules. Final artifact hashes are computed after install-time
binary changes and dependency discovery.

Debug and sanitizer options are applied directly to locally compiled targets
after assembly, including object libraries and tests. They introduce no private
options target into installed SDK exports. Source paths and bytes contribute
to component and reusable host-tool identities, so source reorganization requires
fresh matching artifacts and host-tool attestations.

## Source-attested compiler

Normal builds use the independent modern bootstrap. The private
`beam_seed_compiler` loads the declared compiler seeds in a bare VM through
public runtime APIs and host callbacks. It compiles owned compiler/support
sources; `beam_bootstrap` consumes those outputs only after byte-for-byte
reproduction and source attestation succeed. Neither executable is installed,
boots OTP or substitutes system Erlang. OTP independently owns its compiler
bootstrap against the installed SDK; it does not consume BEAM's private hosts.

[The compiler recipe](../src/beam/compiler/CMakeLists.txt) owns the declared
inventory. [The seed manifest](../src/beam/compiler/seeds/manifest.json) binds
seed hashes to source and generated inputs. Verification rejects missing,
undeclared, duplicate and tampered seeds and changed/missing attestation.
Compiler/debug metadata remains part of strict reproduction. Each output has
one producer; committed artifacts must never be installed over generated outputs
to bypass verification.

Owned parser/Unicode sources retain their grammar, generator/template and data
inputs under `src/beam/support/`. Review generated source before updating its
attestation. Opcode generation uses the owned `beam_makeops` and
`src/lib/compiler/src/genop.tab`. `beam_host` compiles directly from source,
avoiding a dependency on the services inventory that the compiler must build.

Core and base bytecode are also verified deterministically by mandatory
`beam_verify_core_artifacts` and `beam_verify_base_artifacts` build targets.
The test suite does not repeat those successful build compilations; its compiler
checks exercise independent integrity rejection and source/seed correspondence.

The public in-memory zlib interface and VM internals use one runtime-owned
codec. Optional zlib libraries contain only ordinary module bytes and their NIF
bridge. Native consumers use `<beam/zlib.h>` from the selected SDK; they do not
include the private vendor header or compile another project codec.

## Reviewed artifact refresh

Refresh targets write reviewable candidates beneath the selected build directory.
They never adopt tracked replacements automatically:

| Target | Candidates |
| --- | --- |
| `beam_refresh_compiler_seeds` | Owned compiler/support seeds. |
| `beam_refresh_native_artifacts` | Modern native-wrapper bytecode. |
| `beam_refresh_core_artifacts` | Nine core modules, including debug/inspection metadata. |
| `beam_refresh_base_artifacts` | Seven ordinary base modules. |
| `beam_refresh_boot_artifacts` | Aggregate native/core/base refresh. |

For example, after configuring the interpreter build:

```sh
cmake --build build/linux-emu --target beam_refresh_compiler_seeds
cmake --build build/linux-emu --target beam_refresh_boot_artifacts
```

Review candidate code, metadata and their source/generated-input changes before
adopting them and updating hashes. Rebuild both `linux-emu` and `linux-jit`, then
run the owning integration acceptance. Never accept changed inputs by weakening
the verifier or invoking an external compiler. OTP seed maintenance belongs to
its [build guide](../../erlang-otp/docs/build.md).

## Native host tools and cross compilation

Cross generation uses an isolated native build from the same sources and
source-attested seeds. Target binaries are never executed for generation, and
native host tools never enter the target SDK. `beam_host_tools` attests a native
build. `BEAM_HOST_TOOLS_ROOT` explicitly reuses that verified build at its
recorded absolute path; source, seed, host and payload identities must match.
Moving it or supplying mismatched inputs is an error.

`BEAM_BUNDLE_PACKAGE_TOOL=OFF` produces an SDK without package sources.
Bundled builds select `BEAM_PACKAGE_SOURCE_DIR` explicitly. The adapter
[PackageTool.cmake](PackageTool.cmake) calls the package-owned shared recipe with
the verified modern compiler and dependencies. Standalone package builds select
installed `BEAM::CLI`. Inventory changes require rebuilding the matching generic
descriptor and distribution. Missing sources never trigger a network fetch.

## Outputs and installations

Use ignored `build/<preset>` and `install/<preset>` directories for ordinary
development. Layout changes and distribution validation use clean build trees
and empty prefixes. Do not add obsolete-layout migration hooks or in-place
upgrade tests. Disposable validation copies/builds belong to the selected scratch
directory and are cleaned on success, failure and catchable interruption after
owned processes stop. Retain compact identities/results and bounded diagnostics,
not whole build trees for their logs. Never remove another job's inputs.

Runtime exports are shared `BEAM::Runtime`, static `BEAM::RuntimeStatic`, public
`BEAM::NIF`/`BEAM::Driver` and optional `BEAM::CLI`. Generated SDK expectations
bind selected module/native inventories to producer identity. Private source
paths, compiler hosts and target-inappropriate executables are not SDK inputs.
