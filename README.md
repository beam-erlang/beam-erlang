# Erlangsters - BEAM/Erlang

BEAM/Erlang is an experimental modern distribution of the BEAM virtual machine
and Erlang language. It provides the `beam` command, modern libraries and
services, and `libbeam` with a public BEAM/Erlang SDK.

[Erlangsters - Erlang/OTP](https://github.com/beam-erlang/erlang-otp) is a
separately assembled distribution that uses the installed BEAM/Erlang SDK and
`libbeam` as its sole virtual machine. It owns its OTP applications, startup,
native services and commands.

## Status

The VM and distribution ownership migration is complete. The modern BEAM
command, compiler, shell and SDK have installed acceptance on the
Linux interpreter and JIT configurations. Full compatibility with the initial
OTP 29.0 target remains incomplete. Public interfaces and package tooling are
experimental.

The adopted release model follows upstream ERTS versions with an Erlangsters
distribution revision. Release branches follow the supported upstream
generations, with maintenance scope declared separately. The current source
identity is `17.0-r1-preview.1`. No qualified public preview is available. See
[release operations](release/README.md) for the identifier format, implementation
and readiness.

The source baseline is recorded in [UPSTREAM_COMMIT](UPSTREAM_COMMIT).
Start with the [user documentation](docs/README.md). See the
[test guide](tests/README.md) for the current repository and acceptance coverage.

## Native Windows development

Windows 11 AMD64 development uses Visual Studio 2022 MSVC, CMake, Ninja and Perl.
Desktop SDK acceptance also requires pkg-config or pkgconf; MSVC uses native
pkgconf with `--msvc-syntax`. Select its executable explicitly with
`-DBEAM_TEST_PKG_CONFIG=/absolute/path/to/pkgconf` when needed.
From an x64 Visual Studio developer environment:

```powershell
cmake --preset windows-amd64-msvc-emu
cmake --build --preset windows-amd64-msvc-emu --parallel 8
cmake --install build/windows-amd64-msvc-emu
```

Use `windows-amd64-msvc-jit` for a separate JIT installation. Both bundle the
package application and install shared/static BEAM/Erlang SDKs. Add the selected
installation's `bin` directory to `PATH` in ordinary PowerShell to use `beam`
and `beam-pkg`. Native package builds also need the MSVC developer environment.
See the [Windows development record](../dev-documents/windows-development-baseline.md)
for the tested commands, dependencies and remaining coverage. This is a working
development checkpoint; complete Windows compatibility and release qualification
remain separate work.

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
The [UCRT64 Windows checkpoint](../dev-documents/windows-ucrt64-development.md)
records installed command/package/SDK acceptance, GUI results and known limits.

## Building

The intended matrix includes Debian/glibc, Alpine/musl, Windows MSVC/MSYS2,
macOS Apple Silicon, Android and iOS. Concrete presets and CI routes are listed
in [CI architecture](ci/README.md); platform failures and unexecuted coverage
remain explicit. Local Linux builds require
CMake 3.25 or newer, Ninja, a GNU C11-compatible C compiler, a C++17 compiler and
Perl. The build supplies its own Erlang compiler bootstrap.

The default test suite also requires Python 3.11 or newer, Make, binutils,
pkg-config, system PCRE2 and zlib development files, POSIX terminals and loopback
networking. The system libraries exercise coexistence with the VM's bundled
copies. Install terminal development files for terminal support. On Ubuntu 24.04:

```sh
sudo apt-get install build-essential binutils cmake ninja-build perl python3 \
    pkg-config libncurses-dev libpcre2-dev zlib1g-dev
```

Distribution builds require a complete local `beam-erlang-package` checkout.
Select it explicitly by passing
`-DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package` when configuring.
The companion is public at [beam-erlang/beam-erlang-package](https://github.com/beam-erlang/beam-erlang-package).
CI acquires the exact commit in `release/inputs.json`; CMake never downloads sources.
See [preview release operations](release/README.md) for candidate production and
the remaining qualification and publication blockers.

From the repository root, build, test and install the interpreter flavor:

```sh
cmake --preset linux-emu -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package
cmake --build --preset linux-emu
ctest --preset linux-emu
cmake --install build/linux-emu
```

The JIT flavor uses a separate build and installation:

```sh
cmake --preset linux-jit -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package
cmake --build --preset linux-jit
ctest --preset linux-jit
cmake --install build/linux-jit
```

Presets use `build/<preset>/` and install into `install/<preset>/`; both are
gitignored. Installed acceptance suites use separate test prefixes. The
[test guide](tests/README.md) describes coverage and test selection.

## Using BEAM

After installation, these commands run from the repository root:

```sh
install/linux-emu/bin/beam run tests/fixtures/hello_world.erl
install/linux-emu/bin/beam compile -o build/example -- tests/fixtures/hello_world.erl
install/linux-emu/bin/beam shell
install/linux-emu/bin/beam help
```

Bare `beam` also opens the Erlang shell. Bindings persist between expressions;
`q().` shuts down the runtime. The separate Erlang/OTP distribution supplies
`erl`, `erlc` and `escript` for OTP workflows.

With the installed `bin` directory on `PATH`, a project containing a
`beam.toml` manifest uses the experimental package workflow:

```sh
beam-pkg build .
beam-pkg install .
beam-pkg run --project .
```

## Embedding

An installed prefix provides `beam/beam.h`, `libbeam.so`, `libbeam.a` and a CMake package under
`lib/cmake/BEAM`. A C host links the public runtime target:

```cmake
find_package(BEAM CONFIG REQUIRED COMPONENTS Runtime)
add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE BEAM::Runtime)
```

Configure the client with `-DCMAKE_PREFIX_PATH=/path/to/install`.
The [first-call example](tests/installed/sdk/consumer/first-call/) is a complete
client built and exercised by installed-SDK acceptance. It compiles an Erlang
module using the installed compiler, embeds its bytecode and calls it from C.

Runtime-only SDK discovery does not require the command-line tools.
`find_package(BEAM CONFIG REQUIRED COMPONENTS CLI)` explicitly requires
`BEAM::CLI` for build-time Erlang compilation. Native extensions use
`BEAM::NIF` or `BEAM::Driver`. Clients use the installed public headers and
targets.

For a statically linked C host, require the `RuntimeStatic` component and link
`BEAM::RuntimeStatic`. This target retains the complete archive, exports the
native entry points and supplies the platform libraries, including the JIT's
C++ dependencies. Discovery works without `libbeam.so` or the CLI. A target
graph cannot select both linkage forms. Native extensions continue to resolve
against their host; they must not link another runtime copy.

The static SDK supports generic embedding and the public NIF/driver interfaces.
Modern commands and components use shared linkage; static hosts reject modern
component acquisition. `beam_library_path()` returns the absolute path of the
containing runtime image, which is the executable for static hosts.
`beam_build_id()` identifies the source build for either linkage form. SDK
artifact metadata records separate shared/archive hashes and the hashed static
link-support files.

Hosts may explicitly compose already-linked native implementations using
`beam_builtin_nif` and `beam_builtin_driver`. Preparation copies and validates
the descriptors without invoking their initializers. The VM registers them at
normal native initialization time and removes the selection at termination.
The implementation code and native entry tables must remain resident for the
VM lifetime. This is an initial composition mechanism, not hot registration.

Hosts select their runtime composition explicitly. The API supports sequential
VM lifetimes within one initialized system; one VM may exist at a time.
The interface remains experimental.

## License and credits

The project primarily uses the [Apache License 2.0](LICENSE.txt).
Bundled sources retain their own license and copyright notices; additional
license texts are in [LICENSES/](LICENSES/).
See [upstream acknowledgments and runtime credits](AUTHORS).
