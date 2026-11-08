# Build maintenance and compiler bootstrap

CMake owns configuration order, compiler settings, feature probes and product
composition. Component recipes under `src/beam/` own source/output inventories;
shared helpers live here. [Building the SDK](../docs/getting-started/building.md)
owns contributor commands, [tests](../tests/README.md) owns acceptance, and
[release operations](../release/README.md) owns source acquisition and artifacts.

## Build ownership

The root configures project options and target capabilities, the six-module
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
The ordinary modern base modules retain their own source/seed verification.
Native-backed byte, list, map and text operations use host-bound optional BIF contracts.
Their ordinary module supplies the corresponding stubs and specs. Changes to
core exports or modern bytecode require the owning reviewed core/base candidates.
The separate compiler VM keeps its
explicitly selected ordinary support modules, including `binary`; they are not
added to modern base or runtime command inventories.

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
| `beam_refresh_core_artifacts` | Six core modules, including debug/inspection metadata. |
| `beam_refresh_base_artifacts` | Direct ordinary modern base modules and the remaining verified projected library artifacts. |
| `beam_refresh_eval_artifact` | Assembly-backed selective-receive support. |
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

When a new lowering feature is required to compile the compiler itself, stage
reviewed compiler-pass candidates in an explicit private compiler inventory.
Keep that producer and its matching VM immutable while generating the complete
candidate inventory. Run a second producer using those candidates and require
byte-for-byte reproduction before adoption. Preserve the producer identities
through review; normal source attestation and full seed reproduction must then
pass independently with the adopted inputs on both Linux flavors.

The evaluator artifact combines `prim_eval.S` executable instructions with
`prim_eval.erl` debug/type metadata. Its private bootstrap recipe validates the
assembly, resolves VM-private imports by implementation ID and reproduces the
complete committed bytes during normal builds. Compiling the Erlang stub alone
does not produce the evaluator implementation.

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

Regex is supplied by ordinary native providers; the VM contains no PCRE backend
or `re` BIFs. Installed CMake discovery retains
`BEAM_SDK_HAS_LEGACY_RE_BIFS` as constant `OFF` for provider compatibility checks.
The obsolete `BEAM_ENABLE_LEGACY_RE_BIFS` option rejects truthy requests; an
explicit `OFF` is a deprecated no-op. No regex provider is selected implicitly.

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

The compiler-private ordinary inventory contains 116 modules, including the
canonical `atomics`, `counters` and `persistent_term` facades. The VM embeds only
six runtime protocol modules; the modern base inventory remains separately
supplied. Artifact refresh must preserve metadata and reproduce the complete compiler inventory before
candidates are adopted. Use fresh SDK installations after inventory removal so
obsolete core inspection artifacts cannot survive an incremental installation.

Runtime support has a separate deterministic producer under
`src/beam/runtime_support`. Its bootstrap-compiled parse/Core transform and finite map
stage matching filenames and rewrite only owned runtime sources. The Core pass
routes helper calls introduced during lowering; a final bytecode-import audit
rejects unresolved facade dependencies. Declared relative documentation inputs
are staged and attested with their sources. Canonical
support remains unchanged by projection. The six public byte/list/map/record/text
modules are maintained and compiled directly under `src/beam/library/`, without
the projection transforms. Their explicit `beam_modern_map_comprehensions`
attribute preserves modern lowering through the otherwise canonical bootstrap.
Review upstream support changes for corresponding modern-library adaptations;
these maintained sources are not regenerated from the canonical copies.
The thirteen ordinary standard-library modules are direct sources under
`src/beam/stdlib`, compiled with verified `beam_bootstrap --deterministic
--modern-target`. Their producer captures source, SOFS documentation, compiler,
SDK and verifier identities before compilation and verifies them afterwards.
The resulting import audit permits only the selected peer/base providers and
core language helpers, with no native slots. One generated `stdlib` inventory
owns installation and SDK expectations; no additional bootstrap seeds or
checked-in bytecode are required. Canonical compiler copies remain separate.
The library owns the private SWAR header used by `beam_string`; header changes
participate in compilation dependencies and direct-source attestation.
The retained ETS source projects to
the public base `beam_ets` with 63 adopted exports; the canonical compiler/OTP
interface retains all 76. Its semantic `ets` match-spec dialect argument is data
and is not renamed. The owned match-spec tool is derived from unchanged attested
upstream source: its compiler variant preserves canonical imports and its shell
variant uses projected runtime support. The installed `ms_transform.hrl` is in
the compiler tooling include directory for explicit `-I` use. The six-core inventory includes a
native `erts_internal:ets_info_binary/1` stub sharing the VM diagnostic kernel. Modern command
compilation and source execution select core map-comprehension construction;
canonical bootstrap compilation retains facade calls. This is compiler language
lowering, with no user-MFA rewriting or additional optional bindings. The
private `beam_modern_build_compiler` uses the same modern command policy for
ordinary distribution package code; `beam_bootstrap` remains canonical for
compiler/artifact reproduction. Projected build receipts attest original/staged
sources and projection tools; direct-source receipts identify the maintained
sources and private headers without claiming a projection step. Both bind compiler
identity, native contracts and output hashes and retain executable-import auditing.
Source and projection inputs participate in SDK identity and rebuild dependencies.
Runtime list/map bytes have one verified library producer shared by
network containers and CLI fallback. Never apply this projection to user code.
The shell's queue, set, property-list and ordered-dictionary implementations
project to `beam_runtime_queue`, `beam_runtime_sets`, `beam_runtime_proplists`
and `beam_runtime_orddict`. Modern runtime production omits unused `dict` and
`gb_trees`; their canonical compiler sources and seed inventories remain intact.
