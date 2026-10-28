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

## Alpine Linux / musl

The Alpine 3.24 AMD64 presets select GCC, musl and separate interpreter/JIT SDKs.
Configure them inside Alpine with its native C/C++ toolchain. The build checks
both compilers' actual target libc; choosing an Alpine preset on a glibc host
fails rather than producing an incorrectly labelled SDK.

Install build-base, CMake, `ninja-is-really-ninja`, Perl, Python 3, Git, ncurses development files,
Linux headers, pkgconf, PCRE2 development files and zlib development files in the
Alpine build environment. Provision as root, then build and test as an ordinary
user so file-permission checks retain their meaning.

The virtual Alpine `ninja` package can select Samurai. Use the actual Ninja
implementation above; installed compiler acceptance requires its Unicode depfile
support. Select the package sources explicitly:

```sh
cmake --preset linux-alpine3.24-amd64-gcc-emu \
  -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package
cmake --build --preset linux-alpine3.24-amd64-gcc-emu
ctest --preset linux-alpine3.24-amd64-gcc-emu
cmake --install build/linux-alpine3.24-amd64-gcc-emu
```

Use `linux-alpine3.24-amd64-gcc-jit` for the independent JIT installation.
Native SDK consumers must use the matching OS, architecture and libc. Cross-libc
package compilation requires a consumer toolchain and is not provided by these
native presets. Pure Erlang packages retain compiler-generation compatibility.
See the provisional [native loading contract](../native-extensions/loading.md)
for musl's library lifetime and dependency inspection requirements. ARM64 presets
remain registered; AMD64 results do not establish their native qualification.

## Android from Linux

Use a native Linux x86-64 host and an explicitly installed NDK r28c
(`28.2.13676358`). SDK production needs the native build dependencies above and
the NDK; it needs no emulator, Gradle or Java. Set `ANDROID_NDK_ROOT` to that NDK
and `PACKAGE_SOURCE` to the matching package-tool checkout. From this repository,
build one native interpreter host and attest its generation tools:

```sh
HOST_BUILD="$PWD/build/android-host"
HOST_SDK="$PWD/install/android-host"
cmake --preset linux-emu -B "$HOST_BUILD" \
  -DCMAKE_INSTALL_PREFIX="$HOST_SDK" \
  -DBEAM_PACKAGE_SOURCE_DIR="$PACKAGE_SOURCE"
cmake --build "$HOST_BUILD" --parallel 2
cmake --install "$HOST_BUILD"
cmake --build "$HOST_BUILD" --target beam_host_tools --parallel 2
```

Keep this host build at its attested path while building the four target SDKs.
The same native generation tools serve both ABIs and both flavors:

```sh
for flavor in emu jit; do
  for architecture in arm64 amd64; do
    preset="android-$architecture-ndk-$flavor"
    build="$PWD/build/$preset"
    cmake --preset "$preset" -B "$build" \
      -DBEAM_HOST_TOOLS_ROOT="$HOST_BUILD" \
      -DBEAM_BUNDLE_PACKAGE_TOOL=OFF -DBUILD_TESTING=ON
    cmake --build "$build" --parallel 2
    ctest --test-dir "$build" -R '^target-sdk-layout$' --output-on-failure
    cmake --install "$build"
  done
done
```

Each preset installs under `install/<preset>`. Target SDKs use API 26,
`c++_shared` and native libraries compatible with 4 KB and 16 KB pages. They
contain no executable host compiler or package tool. Use the installed Linux
host's `beam-pkg` with `--target-sdk` to build Android packages; see
[Android embedding](../c-api/android.md).

For transported SDK/support archives, the [build-only acceptance command](../../tests/README.md#platform-orchestration-and-cross-builds)
compiles installed public SDK consumers without running Android code. Cross-build
and compile/link results do not establish Android runtime or release qualification.

## Native Apple Silicon development

Use `macos-arm64-appleclang-emu` or `macos-arm64-appleclang-jit` with the normal
bundled package tool. Select the matching package checkout, Python and the Make
executable used by installed-compiler acceptance explicitly:

```sh
cmake --preset macos-arm64-appleclang-jit \
  -DBEAM_PACKAGE_SOURCE_DIR="$PACKAGE_SOURCE" \
  -DPython3_EXECUTABLE="$PYTHON" -DBEAM_TEST_MAKE="$MAKE"
cmake --build --preset macos-arm64-appleclang-jit
ctest --preset macos-arm64-appleclang-jit
cmake --install build/macos-arm64-appleclang-jit
```

The deployment target is 15.0. Qualification must exercise the supported runtime
baseline, including compatibility of selected external dependencies.
Darwin native modules use their actual installed artifact names; SDK consumers
must use exported targets and artifact inventories rather than infer a suffix.
Static hosts export the applicable public runtime symbol inventory for admitted
native modules. Host-resolved module linkage does not suppress unresolved
executable symbols.

Ordinary BEAM builds and tests need no OTP installation. The independent
installed-OTP-hosted VM selection remains a separate project under
`tests/vm/common_test`. Linux integration and full OTP compatibility remain
separate from native development acceptance.

## Native Windows development

Windows 11 AMD64 development uses Visual Studio 2022 MSVC, CMake, Ninja and Perl.
Desktop SDK acceptance also requires pkg-config or pkgconf; MSVC uses native
pkgconf with `--msvc-syntax`. Select its executable explicitly with
`-DBEAM_TEST_PKG_CONFIG=/absolute/path/to/pkgconf` when needed.
Set `PackageSources` to the absolute matching package-tool checkout path.
From an x64 Visual Studio developer environment:

```powershell
cmake --preset windows-amd64-msvc-emu `
  "-DBEAM_PACKAGE_SOURCE_DIR=$PackageSources"
cmake --build --preset windows-amd64-msvc-emu --parallel 8
cmake --install build/windows-amd64-msvc-emu
```

Use `windows-amd64-msvc-jit` for a separate JIT installation. Both bundle the
package application and install shared/static BEAM/Erlang SDKs. Add the selected
installation's `bin` directory to `PATH` in ordinary PowerShell to use `beam`
and `beam-pkg`. Native package builds also need the MSVC developer environment.
The build recipe selects a native SDK; complete platform and release
qualification use the independently owned product fixtures and release gates.

### Windows UCRT64 GCC

Use the native MSYS2 UCRT64 tools from ordinary PowerShell. Select their paths
explicitly, and keep the UCRT64 `bin` directory on the build-time `PATH` so GCC's
own helper programs can find their DLLs. Do not select compilers from MSYS `/usr`.

Some packaged GCC companion tools lack a UTF-8 process manifest and lose Unicode
filenames. Prepare a separate copy once with the Windows SDK manifest tool:

```powershell
python tools/prepare-windows-ucrt64.py `
  --source "$env:USERPROFILE/msys64/ucrt64" `
  --destination $PreparedUcrt --manifest-tool $ManifestTool
```

`PreparedUcrt` is an absolute workspace dependency path ending in `ucrt64`;
`ManifestTool` selects the installed Windows SDK's `x64/mt.exe`. The original
installation is read-only. The script preserves existing manifest settings,
checks that executable code is unchanged, and records hashes and package versions.
Use `--verify-existing` to validate a prepared copy. This follows the
[GCC Unicode-path workaround](https://gcc.gnu.org/pipermail/gcc-bugs/2023-February/813518.html)
without changing package versions or the machine's locale.

```powershell
$Ucrt = $PreparedUcrt
$env:PATH = "$Ucrt/bin;$env:PATH"
$Preset = 'windows-amd64-ucrt64-emu' # or windows-amd64-ucrt64-jit
& "$Ucrt/bin/cmake.exe" --preset $Preset `
  "-DCMAKE_C_COMPILER=$Ucrt/bin/gcc.exe" `
  "-DCMAKE_CXX_COMPILER=$Ucrt/bin/g++.exe" `
  "-DCMAKE_RC_COMPILER=$Ucrt/bin/windres.exe" `
  "-DCMAKE_MAKE_PROGRAM=$Ucrt/bin/ninja.exe" `
  "-DCMAKE_OBJDUMP=$Ucrt/bin/objdump.exe" `
  "-DCMAKE_AR=$Ucrt/bin/ar.exe" "-DCMAKE_RANLIB=$Ucrt/bin/ranlib.exe" `
  "-DCMAKE_LINKER=$Ucrt/bin/ld.exe" "-DCMAKE_NM=$Ucrt/bin/nm.exe" `
  "-DPython3_EXECUTABLE=$Python" "-DPERL_EXECUTABLE=$Perl" `
  -DBEAM_BUNDLE_PACKAGE_TOOL=ON `
  "-DBEAM_PACKAGE_SOURCE_DIR=$PackageSources"
& "$Ucrt/bin/cmake.exe" --build "build/$Preset" --parallel 6
& "$Ucrt/bin/cmake.exe" --install "build/$Preset"
```

`Python`, `Perl` and `PackageSources` select absolute paths to the retained native
Python/Perl executables and the package repository.
EMU and JIT install separately. GCC and MSVC SDKs and native extensions must use
their matching compiler environment. The installation bundles its required native
GCC runtime DLLs; using installed commands does not require MSYS2 on `PATH`.
Installed SDK and package acceptance use the matching compiler environment;
compilation alone does not establish GUI, full OTP or release qualification.
