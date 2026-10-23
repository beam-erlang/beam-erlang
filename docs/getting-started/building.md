# Build the SDK from source

Select a matching package-tool checkout explicitly and a CMake preset for the
machine that will run the SDK. [CI architecture](../../ci/README.md) lists the
compiler, libc, deployment baseline and flavor matrix. All target builds expose
the same public SDK interfaces; a concrete recipe does not establish that its
platform port currently passes qualification.

```sh
cmake --preset linux-jit -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package
cmake --build --preset linux-jit
ctest --preset linux-jit
cmake --install build/linux-jit
```

Use CMake 3.25+, Ninja, Python 3.11+, Perl and the selected C/C++ compiler.
`ERTS_BUILD_FLAVOR` selects `emu` or `jit`. Compiler, sysroot, toolchain and Apple
deployment settings use standard CMake options. `linux-emu` and `linux-jit` are
compatibility entrypoints; named target presets cover every matrix entry.
Development Debug/RelWithDebInfo and Linux sanitizer presets are also available.

Android presets use `ANDROID_NDK_ROOT` and API 26. iOS device and simulator presets
use separate Apple toolchains and deployment minimum 15; only interpreter builds
are allowed. Cross compilation creates an isolated native build for compiler and
generators from the same source and attested seeds. It never executes a target
binary to generate bytecode and never uses an external Erlang installation.
To reuse a native build explicitly, build its `beam_host_tools` target and set
`BEAM_HOST_TOOLS_ROOT` to that build's absolute path. Source, seed, host and payload
identities are verified; relocation or mismatches are rejected. Native tools stay
outside the target SDK. Cross consumers select their native compiler separately.

Target installation exports `BEAM::Runtime`, `BEAM::RuntimeStatic`, `BEAM::NIF`,
`BEAM::Driver` and, where supplied, `BEAM::CLI`. iOS shared runtime/components are
framework bundles within the installation. Copy complete frameworks into the
application, retain explicit component selection and sign the application with
its embedded frameworks. Static-runtime composition restrictions remain unchanged.
See [deployment](../c-api/deployment.md) and the product-owned
[target acceptance fixture](../../tests/target/CMakeLists.txt) for SDK consumption.
