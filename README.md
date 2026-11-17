# BEAM/Erlang

BEAM/Erlang distributes the BEAM virtual machine as an independently buildable
runtime and public SDK, derived from
[upstream Erlang/OTP](https://github.com/erlang/otp).

It ships the `beam` executable for running Erlang bytecode, separating the
runtime from the OTP framework, application libraries and tools such as `erl`
and `erlc`, which are supplied by a separate distribution.

The companion [Erlang/OTP project](https://github.com/beam-erlang/erlang-otp)
builds that framework, libraries and tools on top of this runtime, with the goal
of reproducing a fully compatible upstream Erlang/OTP distribution.

## Building the runtime with CMake

### Supported platforms

Build on the native host and architecture using a preset from
[CMakePresets.json](CMakePresets.json):

| Host | Interpreter preset | JIT preset |
| --- | --- | --- |
| Linux x86_64 | `linux-x86_64-emu` | `linux-x86_64-jit` |
| Linux AArch64 | `linux-aarch64-emu` | `linux-aarch64-jit` |
| macOS 15+ on Apple Silicon | `macos-arm64-emu` | `macos-arm64-jit` |
| Windows x64 with MSVC | `windows-x86_64-emu` | `windows-x86_64-jit` |

Each preset selects Release. Append `-debug` for its Debug configuration.
Cross compilation, Intel or universal macOS builds, and other Windows compilers
or architectures are outside the supported build.

### Prerequisites

All platforms need CMake 3.25 or newer, Ninja, Perl, and C11/C++17 compilers.
Only the single-config Ninja generator and the `Release` and `Debug` build types
are supported.

- Linux needs pthreads and epoll development headers.
- macOS needs Apple Clang from Xcode or Command Line Tools and the macOS SDK.
  Presets select a macOS 15.0 deployment target.
- Windows needs Visual Studio 2022's x64 MSVC toolset and the Windows SDK.
  Load the x64 developer environment, for example with `vcvarsall.bat x64`,
  before running CMake.

Unix terminal support is enabled when usable termcap/ncurses headers and a
library are available. zlib, zstd, PCRE and Ryu are built from retained sources.
No installed Erlang runtime or compiler is required: generation uses the newly
built VM and the retained bootstrap compiler.

### Configure, build, test and install

Run these commands from the repository root. This example builds the macOS
interpreter; replace the preset and directory suffix for your native target:

```sh
cmake --preset macos-arm64-emu
cmake --build --preset macos-arm64-emu --parallel 3
ctest --preset macos-arm64-emu
cmake --install build/macos-arm64-emu
```

The installation is in `install/macos-arm64-emu`. To build the JIT in Debug,
use `macos-arm64-jit-debug` in all four commands. List the presets available on
your host with `cmake --list-presets=all`.

Debug enables VM assertions, term checks and lock checking, independently of
interpreter or JIT selection. It is intended for runtime development and is
slower than Release. Erlang bytecode debug information is a separate compiler
setting. Direct configurations default to interpreter Release; the runtime
build selects its flavor with `ERTS_BUILD_FLAVOR=emu` or `jit`.

Keep `build/<preset-name>` and `install/<preset-name>` for standard preset
outputs. Use a separate directory under workspace `../tmp/` for experimental
configurations, temporary installations and validation payloads. Keep a separate
installation for each flavor and build type. Installation supports
`cmake --install <build-dir> --prefix /absolute/path` and `DESTDIR` staging.
Install-directory settings must remain relative and contained within the prefix.

### Installed contents

A normal installation contains one runtime configuration:

| Component | Contents |
| --- | --- |
| Executables | `bin/beam` on Unix or `bin/beam.exe` on Windows, plus `beam.smp` and flavor aliases or copies |
| Unix process helper | `bin/erl_child_setup`; Debug also supplies the required relative `erl_child_setup.debug` alias |
| Static runtime | The archive for the selected flavor and build type |
| Public interfaces | Process-entry header and matching NIF/driver headers under `include/beam/` |
| Preload inspection assets | Matching bytecode, sources and `erts.app` under `share/beam/erts-17.0/` |
| CMake SDK | Package, deployment helpers and assembly scripts under `lib/cmake/BEAM/` |
| SDK identity | `manifest.json` with source/build identities, configurations, artifact paths and hashes |

Paths above use the default install directories. Windows has no child-setup
helper; Debug installs program databases beside its executables. The preload
inspection files complement the VM's embedded execution images. Bootstrap and
private test support are build/validation inputs and are excluded from the
installation.

## Running the VM

A VM boot needs an application tree and boot file supplied explicitly. To smoke
test a Unix installation from this checkout, use the retained bootstrap fixture:

```sh
mkdir -p ../tmp/beam-readme-boot
printf '{lookup,[file]}.\n{domain,"beam.test"}.\n' > ../tmp/beam-readme-boot/inetrc
runtime_bin="$PWD/install/macos-arm64-emu/bin"
ERL_INETRC="$PWD/../tmp/beam-readme-boot/inetrc" BINDIR="$runtime_bin" \
  "$runtime_bin/beam" -S 2:2 -SDcpu 1 -SDio 1 -K true -- \
  -root "$PWD/bootstrap" -bindir "$runtime_bin" -progname beam \
  -boot "$PWD/bootstrap/bin/start_clean" -noshell -noinput \
  -eval 'io:format("~p~n", [erlang:system_info(emu_flavor)]), erlang:halt(0).'
```

Adjust `runtime_bin` for your installed preset. This prints the emulator flavor
and exits. The fixture is for validation; it is not an installed OTP release.
The file-only resolver policy avoids requiring the `inet_gethost` DNS helper,
which is outside the minimal installation.

Raw `beam` uses single-dash VM flags before `--` and boot/init arguments after
it. On Unix, `BINDIR` must be an absolute directory containing the matching
child helper, including its alias for Debug. `beam -V` reports the version
without booting the VM.

## Using the SDK

The static process-entry SDK is a preview interface. It does not provide a
stable C ABI or a returning embedding lifecycle.

### Supply a process entry point

An independent host starts the runtime through
[beam/beam.h](include/beam/beam.h). A Unix entry point is:

```c
#include <beam/beam.h>

int main(int argc, char **argv)
{
    return beam_main(argc, argv);
}
```

Call once on the original process main thread. The VM owns signals, standard
descriptors and process lifetime; halt and fatal errors terminate the process.
The integer signature does not promise a return to the caller. Windows hosts
must supply UTF-8 arguments; use a `wmain` wrapper that converts UTF-16 arguments
before calling `beam_main()`, as in the [Windows host fixture](tests/sdk/host_main.h).

A Windows process host can place the SDK-linked VM in its executable or in a
DLL loaded by its native launcher. `beam_main()` obtains the module that
contains its code, so the preload resources supplied by `BEAM::Runtime` belong
in that same executable or DLL. Enter the VM once on the process's original
thread in either arrangement.

The Windows-only `beam_windows_wait_for_console_key()` supports a launcher's
`-keep_window` exit handling through the initialized VM console input owner.
Call it only after entering `beam_main()`, on that same original thread during
shutdown. It is not a console operation available before VM startup.

Save the host as `main.c` in its own project, with this `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)
project(beam_host LANGUAGES C CXX)
find_package(BEAM 17.0 EXACT CONFIG REQUIRED)

add_executable(beam_host main.c)
target_link_libraries(beam_host PRIVATE BEAM::Runtime)
install(TARGETS beam_host RUNTIME DESTINATION bin)

if(NOT WIN32)
    beam_install_child_setup(DESTINATION bin)
endif()
beam_install_native_headers(DESTINATION include)
beam_install_preloaded_modules(DESTINATION "lib/erts-${BEAM_SDK_ERTS_VERSION}/ebin")
beam_install_preloaded_sources(DESTINATION "lib/erts-${BEAM_SDK_ERTS_VERSION}/src")
```

Select an installed SDK explicitly through `BEAM_DIR`:

```sh
cmake -S /path/to/host -B ../tmp/beam-host -G Ninja \
  -DBEAM_DIR=/path/to/sdk/lib/cmake/BEAM \
  -DBEAM_RUNTIME_FLAVOR=emu -DCMAKE_BUILD_TYPE=Release
cmake --build ../tmp/beam-host --parallel 3
cmake --install ../tmp/beam-host --prefix /path/to/deployment
```

`BEAM::Runtime` supplies the static runtime and its system link dependencies.
JIT hosts need the C++ language enabled, as in the example. Native extensions use
`BEAM::NIF` or `BEAM::Driver` for their headers and link requirements and must
not link another runtime archive. The former `BEAM::RuntimeStatic` target has
been removed.

`BEAM_RUNTIME_FLAVOR` defaults to `emu`. The consumer's `CMAKE_BUILD_TYPE`
selects Release or Debug; an unset build type selects the Release runtime
without changing consumer compiler flags. An unavailable selection fails.
Package versions match exactly, and platform, architecture, macOS deployment
floor and Windows MSVC/CRT requirements must match the SDK.

Deployment helpers honor prefix overrides, staging and relocation. On Unix,
`beam_install_child_setup()` copies the selected helper and creates the Debug
alias when needed; set `BINDIR` to its deployed directory. Windows hosts embed
the SDK's preload resources automatically through `BEAM::Runtime`. A deployed
host runs without the SDK prefix or archive, but still needs its own boot files
and application environment. See [the SDK acceptance fixture](tests/sdk/) for
C/C++ hosts, NIFs and drivers that exercise this boundary.

### Combine installed configurations

Each installation supplies one of these archives:

| Configuration | Unix | Windows |
| --- | --- | --- |
| Interpreter Release | `libbeam.a` | `beam.lib` |
| Interpreter Debug | `libbeam_debug.a` | `beam_debug.lib` |
| JIT Release | `libbeam_jit.a` | `beam_jit.lib` |
| JIT Debug | `libbeam_jit_debug.a` | `beam_jit_debug.lib` |

An SDK can combine one to four installed configurations behind the same CMake
package. After building and installing both macOS Release presets, for example:

```sh
cmake \
  "-DBEAM_SDK_INPUT_PREFIXES=$PWD/install/macos-arm64-emu;$PWD/install/macos-arm64-jit" \
  "-DBEAM_SDK_OUTPUT_PREFIX=$PWD/../tmp/beam-sdk-macos-arm64" \
  -P cmake/AssembleSDK.cmake
```

The output must be a fresh directory. Assembly reads only the selected installed
prefixes and rejects duplicate configurations, incompatible identities, unequal
common assets, missing artifacts and input/output overlap. The script is also
installed at `<sdk>/lib/cmake/BEAM/AssembleSDK.cmake`, so assembly does not
require this checkout or a build tree.

Combined SDKs share headers and preload inspection assets while retaining each
configuration's archive, executable, helper and build identity. Executables use
`beam`, `beam_debug`, `beam_jit` and `beam_jit_debug` basenames, with `.exe` on
Windows. Unix helpers live under `libexec/beam/<flavor>/<Release|Debug>/`; manual
boots must select the matching helper directory. Consumer deployment helpers
continue to copy the selected assets into the consumer's own layout.

The package exports `BEAM_SDK_AVAILABLE_VARIANTS`, `BEAM_SDK_SOURCE_ID` and the
selected `BEAM_SDK_BUILD_ID` for discovery and diagnostics. The installed
`manifest.json` records all configurations. See [package selection](cmake/BEAMConfig.cmake.in)
and [SDK assembly](cmake/AssembleSDK.cmake) for the complete rules.

## Testing the runtime with CTest

Standard `ctest --preset <preset-name>` runs `runtime.build`, `runtime.install`
and `sdk.install`. These check build-tree execution, installation, staging,
relocation, independent native hosts and extensions, and execution after the
SDK and consumer build/source trees are removed. Boots check the selected
flavor, build type and Debug/lock-checking identity. Set `BUILD_TESTING=OFF` to
omit tests while retaining production SDK generation.

Linux checks require a `C.UTF-8` or `C.utf8` locale. SDK affinity checks require
at least two allowed CPUs and permission to change the test process's affinity.
Runtime and emulator tests need local networking, including loopback access.

Optional integrations are independent of the default checks:

| Configure setting | Build target | Scope |
| --- | --- | --- |
| `BEAM_BUILD_EMULATOR_TESTS=ON` | `check-emulator` | Five unchanged upstream suites, 24 cases, no failures or skips accepted |
| `BEAM_BUILD_FULL_EMULATOR_TESTS=ON` | `check-emulator-full` | Complete retained emulator specification in one ordered Common Test run |
| `BEAM_SDK_TEST_INPUT_PREFIXES=<semicolon-separated installed prefixes>` | `check-sdk-bundle` | Assemble, relocate and test consumers of each supplied SDK configuration, including invalid selections |

Both emulator options default to `OFF`; SDK bundle inputs default to empty.
Use a temporary configuration for optional checks, for example:

```sh
cmake --preset macos-arm64-emu -B ../tmp/beam-emulator-tests \
  -DCMAKE_INSTALL_PREFIX="$PWD/../tmp/beam-emulator-tests/install" \
  -DBEAM_BUILD_EMULATOR_TESTS=ON
cmake --build ../tmp/beam-emulator-tests --parallel 3 --target check-emulator
```

The selected suites are `prim_eval_SUITE`, `atomics_SUITE`, `counters_SUITE`,
`num_bif_SUITE` and `process_max_heap_size_SUITE`. Passing them does not establish
full-corpus coverage. Emulator support is compiled using the candidate VM and
retained bootstrap compiler, without an external Erlang installation.

Full native fixture support targets Linux glibc amd64, macOS arm64 and Windows
x64 MSVC and also requires Python 3.9+. Full-corpus qualification is pending.
Linux process cleanup requires native pidfd support and access to the run's
process identities and socket ownership through `/proc`. To attempt a
full run, use a distinct temporary build/install directory, enable
`BEAM_BUILD_FULL_EMULATOR_TESTS` and build `check-emulator-full`. Full runs can
take hours and remain off in CI. A pass requires complete accounting, no failed
or automatically skipped cases, and only individually audited upstream user
skips. Successful preparation alone does not establish a passing campaign.

For full-suite diagnostics, prepare `beam_emulator_full_test_support` and run
CTest under `<build-dir>/tests/emulator/full/suites`. The generated launcher at
`<build-dir>/tests/emulator/full/run.py` also accepts suite, group and case
selections. Latest evidence pointers are under `<build-dir>/tests/emulator/full/`;
full and focused runs retain separate logs. See the
[test entry point](tests/CMakeLists.txt) for the validation configuration.

## Source map

| Path | Responsibility |
| --- | --- |
| `erts/` | Retained VM, runtime support libraries and preloaded modules, at upstream-relative paths |
| `lib/` | BEAM opcode table and Kernel headers used by runtime generators |
| `include/beam/` | Public process-entry declaration |
| `cmake/` | Platform configuration, SDK packaging, deployment and assembly |
| `erts/emulator/cmake/` | Emulator build rules and generators |
| `erts/preloaded/cmake/` | Production preload inspection metadata generation |
| `bootstrap/` | Retained compiler/build inputs and runtime validation fixture |
| `tests/runtime/` and `tests/sdk/` | Product runtime and installed-interface acceptance checks |
| `tests/emulator/` | Upstream emulator integration and private support inputs |
| `erts/emulator/test/` | Retained upstream suites, helpers and data |

Preserve upstream source origins and notices, keep private test support out of
the product, and use installed public interfaces for distribution integration.
When reporting a problem, include the source revision, preset, SDK identities
where applicable, and failing command with its logs.

## Version and provenance

[BEAM_VERSION](BEAM_VERSION) is the product and ERTS runtime version (`17.0`).
The retained upstream version-generator inputs separately identify OTP
`29.0` and release `29`; these are distinct identities.
[UPSTREAM_COMMIT](UPSTREAM_COMMIT) records the upstream synchronization point,
not the exact modified source tree, a product release or an ABI guarantee.
SDK source and build identities describe the production inputs and selected
configuration. Equal upstream pins alone do not establish compatibility.

## License

BEAM/Erlang is derived from Erlang/OTP and retains its upstream copyright and
license notices. See [LICENSE.txt](LICENSE.txt) for the Apache License 2.0 and
[LICENSES/](LICENSES/) for additional component licenses.

<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright Ericsson AB 2010-2025. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

%CopyrightEnd%
-->
