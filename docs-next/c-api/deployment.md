# Installed SDK and deployment

Candidate guide for the current experimental SDK.

The installed SDK serves bare VMs, modern returning applications and independent
process-owned hosts. They use public headers and one selected runtime target; composition
and lifecycle are explicit in the host. Deployment packages availability and
never initializes a VM, activates services or selects application policy.
Interfaces and native descriptors remain experimental matching-build contracts.

## Discovery and installed layout

```cmake
find_package(BEAM CONFIG REQUIRED)
add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE BEAM::Runtime)
```

Set `CMAKE_PREFIX_PATH` to the installed prefix or `BEAM_DIR` to its
`lib/cmake/BEAM` directory. `BEAM::Runtime` supplies the shared library and public
include directory; clients do not enumerate VM-private headers or dependencies.
Use `find_package(BEAM CONFIG REQUIRED COMPONENTS RuntimeStatic)` and link
`BEAM::RuntimeStatic` for a static C host. Its complete archive carries retention,
native symbol exports and required system libraries, including JIT C++ support.
It needs no shared runtime, CLI or private archive. `COMPONENTS Runtime` explicitly
requires the shared form. Linking both forms into one target graph is rejected.
Modern commands and components require shared linkage; static hosts reject
modern component acquisition with `BEAM_UNSUPPORTED` before mapping.
Runtime-only discovery does not require the CLI or modern component files.
Use `find_package(BEAM CONFIG REQUIRED COMPONENTS CLI)` when build-time Erlang
compilation needs `BEAM::CLI`. Native extensions use `BEAM::NIF`/`BEAM::Driver`.
CMake version selection matches the exact distribution version, not a stable ABI.

| Installed location | Owner and purpose |
| --- | --- |
| `bin/beam`, `bin/beam-pkg -> beam` | Modern commands and package-tool alias. |
| `lib/libbeam.so`, `lib/libbeam.a` | Shared and static forms of the same VM implementation. |
| `lib/libbeam-{file,network,zlib}.so` | Explicit native capability components. |
| `lib/beam/{library,boot,services}/` | Ordinary bytecode inventories, each with `modules.txt` and `ebin/`. |
| `include/beam.h`, `include/beam/nif/` | Public embedding and native-extension headers. |
| `lib/cmake/BEAM/` | Imported targets and deployment/acquisition helpers. |
| `lib/beam/sdk.toml`, `lib/beam/runtime-artifacts.json` | SDK provenance and verified artifact catalog. |
| `lib/beam/core/{ebin,src}/` | Verified nine-module inspection bytecode and source inputs for distribution tools. |
| `lib/beam/ebin/beam_host.beam` | Optional explicitly loaded host-request bridge. |
| `lib/beam/runtime/beam/` | Explicit private command/service inventories. |
| `lib/beam/tools/{compiler,pkg}/` | Optional compiler and package-tool inventories. |
| `libexec/beam/erl_child_setup` | Generic external-program helper. |

BEAM installs no OTP application tree, `runtime/otp`, OTP commands or hostname
helper. OTP owns its complete installation and native artifacts. Neither product
layout specifies a future package-cache format.

`BEAM_HOST_MODULE` identifies the separately selectable host bridge, copied from
the same producer as the services inventory. `BEAM_{BASE,BOOT,SERVICES}_MODULES`
list the three ordinary inventories. Each also exports `INVENTORY_DIR`,
`MANIFEST`, `EBIN_DIR` and `INVENTORY_HEADER` under that prefix. Native units use
`BEAM_COMPONENT_DIR`, `BEAM_FILE_COMPONENT` and `BEAM_NETWORK_COMPONENT`.
These are configuration-time locations, never automatic VM code paths.

## Verify ordinary bytecode

Use `beam_target_bytecode_verifier(target INVENTORIES base boot services)` for
selected inventories. Producer-generated CMake expectations verify assets during
consumer configuration; generated C records bind names, sizes and SHA-256 to the
loaded runtime's build identity. The host checks the exact manifest, reads its
explicit files, calls `beam_bytecode_verify`, and supplies those same buffers.
The verifier performs no I/O or VM initialization. Do not regenerate expectations
from installed replacements. Runtime-only discovery does not require inventories.

Installed targets contain no source/build-tree paths. A prefix can move before
consumer configuration; moving it after linking also requires a compatible
client library search policy. `beam_library_path` reports the actual loaded
runtime image (shared library or static executable) without assuming a layout.

## Application bundles

```cmake
beam_target_deployment(my_host PROFILE beam-returning COMPONENTS file network)
beam_install_deployment(my_host)
```

Declare deployment once on an executable, then install it once. Stage with
ordinary `cmake --install --prefix`; application resources use normal CMake
install rules. Unknown/incompatible selections and missing selected assets fail
configuration. Ordinary SDK discovery does not require those optional assets.

| Profile | Packaged assets |
| --- | --- |
| `bare` | `libbeam.so`; the host supplies its explicit code/composition. |
| `base` | Runtime and the base bytecode inventory. |
| `components` | Runtime, base and services bytecode inventories. |
| `beam-returning` | Runtime, base, services and boot bytecode inventories. |

`components` and `beam-returning` accept `file`, `network`, `zlib` and `program`.
The first three add their component units; program adds `erl_child_setup`.
Output/code use services without another deployment selection. Zlib's unit
contains its Erlang wrapper and native implementation: a host does not acquire
a separate loose wrapper. Runtime preparation still requires explicit component
paths and service bits. A returning program recipe supplies `bindir`; generic
composition supplies the exact `program_helper` executable path.

Bundles place executables in `bin`, libraries in `lib`, resources under
`lib/beam` and helpers under `libexec/beam`. The installed executable RPATH is
`$ORIGIN/../lib`. This packages known project dependencies, not arbitrary Erlang
applications, operating-system libraries, the CLI or compiler tooling.

Each target receives a private `<identifier>_deployment.h` with uppercase
macros describing SDK-relative asset paths, for example
`MY_HOST_LIB_BEAM_BOOT_MODULES_TXT` as `"lib/beam/boot/modules.txt"`. CMake converts the
target name to a C identifier. No development prefix is compiled into these
macros. Hosts anchor paths to their deployment root or loaded library, not a
permanent configure-time SDK location.

## Verified runtime artifacts

Independent distribution assembly uses the generic artifact catalog. Supply the
expected full version and build identity from a separately verified candidate
record, not from the prefix being checked. Preview suffixes are significant;
numeric CMake package-version discovery does not establish exact compatibility:

```cmake
beam_check_runtime_sdk(VERSION "${EXPECTED_BEAM_VERSION}"
    BUILD_ID "${EXPECTED_BEAM_BUILD_ID}" FLAVOR emu)
beam_get_runtime_artifact(runtime_library NAME runtime KIND runtime)
beam_get_runtime_artifact(child_helper NAME helper.child_setup
                          KIND executable-helper RELATIVE)
beam_install_runtime_artifacts(ARTIFACTS runtime helper.child_setup)
```

`BEAM_SDK_VERSION`, `BEAM_SDK_FLAVOR`, `BEAM_SDK_BUILD_ID`,
`BEAM_SDK_ARCHITECTURE` and `BEAM_SDK_TRIPLE` identify the installation.
`beam_check_runtime_sdk` accepts exact `VERSION`, `FLAVOR` and `BUILD_ID`
requirements; flavors are `emu` and `jit`. The build identity is not an ABI
version. `BEAM_SDK_METADATA` points to `sdk.toml`.

The adopted ERTS-based release numbering does not change these checks. The
upstream runtime version alone cannot identify the complete distribution or
establish compatibility with an embedding host. Use the complete identity,
such as `17.0-r1-preview.1`; see [release operations](../../release/README.md).

`BEAM_RUNTIME_ARTIFACTS` lists IDs; `BEAM_RUNTIME_ARTIFACTS_METADATA` points to
JSON recording kind, relative path, SHA-256, dependencies and SDK provenance.

| ID | Kind | Dependencies |
| --- | --- | --- |
| `runtime` | `runtime` | None. |
| `runtime.static` | `runtime-static` | `link.static`, `link.static.configuration`, `link.static.exports`. |
| `link.static*` | `link-support` | None; hashed installed CMake exports and native symbol inventory. |
| `core.<module>` for the nine embedded modules | `core-module` | None. |
| `core-source.<module>` for the nine embedded modules | `core-source` | None. |
| `helper.child_setup` | `executable-helper` | None. |
| `header.beam_native` | `native-header` | None. |
| `header.erl_nif` | `native-header` | `header.erl_drv_nif`, `header.erl_nif_api_funcs`. |
| `header.erl_driver` | `native-header` | `header.erl_drv_nif`. |
| `header.erl_drv_nif` | `native-header` | `header.erl_int_sizes_config`. |
| `header.erl_nif_api_funcs`, `header.erl_int_sizes_config` | `native-header` | None. |

`beam_get_runtime_artifact(out NAME id [KIND kind] [RELATIVE])` validates the
selected artifact and dependency closure. It returns an absolute build-time
path, or an SDK-relative path with `RELATIVE`. Missing IDs/files, changed hashes,
wrong kinds and metadata/inventory disagreement fail configuration. Hashes are
checked once per configuration invocation, including after SDK relocation.

`beam_install_runtime_artifacts(ARTIFACTS ids... [DESTINATION relative-root])`
installs the selected dependency closure, preserving its relative layout.
Without DESTINATION it uses the consumer installation root. Installation checks
hashes again; a changed SDK requires reconfiguration. Prefix overrides and
`DESTDIR` work. Consumers configure their runtime library search path and locate
deployed resources through the returned relative paths.

The catalog supplies no OTP NIFs/drivers, hostname helper, application code,
boot scripts, modern components or CLI. OTP builds its own native sources and
uses public composition/admission. Modern component deployment uses the separate
bundle interface above. OS and third-party dependencies remain explicit.

## Native headers and bindings

`BEAM::NIF` and `BEAM::Driver` are interface-only targets exposing installed
public headers; neither links a native extension to an absolute runtime path.
The running VM supplies ordinary NIF/driver symbols after explicit admission.

```cmake
add_library(example_driver MODULE example_driver.c)
target_link_libraries(example_driver PRIVATE BEAM::Driver)
set_target_properties(example_driver PROPERTIES PREFIX "")
beam_install_native_headers(DESTINATION usr/include)

add_library(example_nif MODULE example_nif.c)
beam_target_nif(example_nif)
```

`beam_install_native_headers` installs the six-header public closure flat into
a required relative destination without parent traversal. It includes generated
integer-size configuration and `beam_native.h`, preserves SDK hash checks at
configuration/installation, and supports prefix overrides and `DESTDIR`.
The optional `ARTIFACTS` selection can request only the standard native-header
closure, omitting the composition-specific `beam_native.h`. Independent products
can call it for common and versioned ERTS include paths;
they must not reconstruct the closure from private source headers.

`beam_target_nif` adds `BEAM::NIF` and the exported matching-build descriptor
needed by explicit composition bindings. Descriptor validation is independent
of package provenance and does not establish a stable provider ABI. Mapping
trusted libraries may execute platform constructors; NIF load callbacks wait
for activation. Runtime binding and cleanup rules are in
[the composition reference](reference.md#composition).

`beam_native.h` also exposes:

- `beam_native_fail(mode, format, ...)`: process-fatal native failure reporting.
  ERROR uses the diagnostic/dump/abort path, ABORT skips the Erlang dump, DUMP
  uses diagnostic/dump/failure exit; unknown modes follow ERROR. Existing dump
  limits/suppression apply and `%T` accepts public native terms. This never
  returns and is not recoverable shutdown.
- `beam_native_trace_backend_get()`: allocation-free, initialization-free query
  of the loaded VM's constant none/DTrace/SystemTap/LTTng backend. OTP owns
  corresponding native builds and external dependencies. Default Linux builds
  report none; this does not certify instrumented builds.

## Core bytecode for distribution tools

`beam_install_core_modules(DESTINATION relative-directory)` installs the nine
VM-owned core inputs into a consumer-owned directory. It excludes modern base
modules and uses the verified `core.<module>` records. The required destination
cannot traverse parents; hashes are checked at configuration and installation.
Prefix overrides and `DESTDIR` are supported. Consumers deploy the runtime
separately and must not compile substitute copies of VM-owned modules.

Installed core files retain abstract code, attributes, compile information and
documentation. They are the exact committed inputs used to generate embedded
payloads; that generator strips non-execution chunks. Runtime digest comparison
therefore applies the same chunk selection. These files support tools such as
Xref and documentation lookup; they neither load code nor change composition.
OTP places them beside its source-built preloads and generates its own `erts.app`.

## Working consumers

The [first-call project](../../tests/installed/sdk/consumer/first-call/) demonstrates
a runtime consumer whose own Erlang module is embedded at build time. The
[returning SDK consumer](../../tests/installed/sdk/consumer/returning/) and
[deployment consumer](../../tests/installed/sdk/deployment/) demonstrate explicit
inventories and component paths. They are maintained in the product's
installed acceptance tests; start from those sources instead of copying an
untested recipe from prose.

Deploy all selected assets together, locate them relative to the deployed
application, and verify before entry. A bundle should also work after moving it
and running from an unrelated directory with the original SDK unavailable.
The deployment helpers do not provide automatic package resolution, native
activation or a fallback to another installation.

[Return to C embedding](README.md).
