# BEAM/Erlang

**Erlang** is a programming language and runtime system for building massively scalable soft real-time systems with requirements on high availability. 

**OTP** is a set of Erlang libraries, which consists of the Erlang runtime system, a number of ready-to-use components mainly written in Erlang, and a set of design principles for Erlang programs. [Learn more about Erlang and OTP](https://www.erlang.org/doc/system/getting_started.html).

[Learn how to program in Erlang](http://learnyousomeerlang.com/).

## Examples

There are several examples [on the website](http://erlang.org/faq/getting_started.html) to help you get started. The below example defines a function `world/0` that prints "Hello, world" in the Erlang shell:

```erlang
-module(hello).
-export([world/0]).

world() -> io:format("Hello, world\n").
```

Save the file as `hello.erl` and run `erl` to enter the Erlang shell to compile the module.

```sh
Erlang/OTP 24 [erts-12.2] [source] [64-bit] [smp:8:8] [ds:8:8:10] [async-threads:1] [jit]

Eshell V12.2  (abort with ^G)
1> c(hello).
{ok,hello}
2> hello:world().
Hello, world
ok
```

Learn more about the Erlang syntax of [modules](http://erlang.org/doc/reference_manual/modules.html), [functions](http://erlang.org/doc/reference_manual/functions.html) and [expressions](http://erlang.org/doc/reference_manual/expressions.html) on [Erlang.org](https://www.erlang.org).

## Source layout

Retained upstream Erlang/OTP sources keep their upstream-relative paths under
root `erts/` and `lib/`. Emulator suites, helpers and data remain together under
`erts/emulator/test/`. Project-owned build support lives under `cmake/` or beside
its component; project-owned tests live under `tests/`, with CMake modules and
scripts in `tests/cmake/`. `tests/CMakeLists.txt` remains the test entry point,
runtime integration checks live in `tests/runtime/`, and upstream emulator
integration code lives in `tests/emulator/`. `bootstrap/` supplies the shared
retained runtime test fixture.

Pinned private emulator test inputs live under `tests/emulator/support/`,
preserving upstream-relative `lib/` and `erts/` paths. These include Common Test,
syntax_tools, supporting application/header sources and native helper sources.
The shared and full test CMake rules select imported support files; the Erlang
preparation code owns corpus compilation, variant and preload settings. Git and
`UPSTREAM_COMMIT` retain source provenance. Preparation fingerprints current
inputs so direct test runs reject stale fixtures. See
[repository guidance](AGENTS.md) for placement and maintenance rules.
Native test build rules live in CMake; upstream Makefiles are omitted. The four
retained `vsn.mk` files supply application versions during test preparation.

## Building the runtime with CMake

This repository builds the BEAM virtual machine. Unix builds also include the
`erl_child_setup` helper. Supported native targets are Linux x86_64/AArch64,
macOS 15 or newer on Apple Silicon, and Windows x64. Both the interpreter
(`emu`) and JIT (`jit`) flavors support Release and Debug builds; Release is
the default. macOS uses Apple Clang from Xcode or Command Line Tools and the
selected macOS SDK, with deployment target `15.0`; Intel, universal, and cross
macOS builds are not supported. Windows is native x64 MSVC from Visual Studio
2022 with the Windows SDK. MinGW, Clang, and ARM64 Windows builds are not
supported. The interpreter uses switch dispatch under MSVC.

Prerequisites are CMake 3.25+, Ninja, and Perl. Unix also needs a C11/C++17
compiler and pthreads. Windows needs the x64 MSVC toolset on `PATH` (the
presets do not hard-code the compiler; load it with `vcvarsall.bat x64`).
Linux requires epoll development headers; macOS requires the SDK's kqueue APIs.
Windows I/O uses `WaitForMultipleObjects` and does not use kernel poll.
Terminal support is enabled when a usable termcap/ncurses library and headers
are available. zlib, zstd, PCRE, and Ryu are built from the vendored sources.
No installed Erlang runtime or compiler is needed.

`BEAM_VERSION` versions the product and runtime by the retained ERTS release,
currently `17.0`. Runtime identity keeps ERTS and upstream OTP versions separate:
`ERLANG_VERSION` comes from `BEAM_VERSION`, `ERLANG_OTP_VERSION` is `29.0`, and
`ERLANG_OTP_RELEASE` is `29`. CMake pins the upstream OTP identity separately
and invokes the retained version generator. `UPSTREAM_COMMIT` records source
synchronization provenance separately.

On macOS, build, test, and install the interpreter with:

```sh
cmake --preset macos-arm64-emu
cmake --build --preset macos-arm64-emu --parallel 3
ctest --preset macos-arm64-emu
cmake --install build/macos-arm64-emu
```

Use `macos-arm64-jit` for the JIT. On Linux, use `linux-x86_64-emu`,
`linux-x86_64-jit`, `linux-aarch64-emu` or `linux-aarch64-jit` for the matching
native architecture. On Windows, use `windows-x86_64-emu` or
`windows-x86_64-jit`. Each has a corresponding `-debug` preset. For example:

```sh
cmake --preset macos-arm64-jit-debug
cmake --build --preset macos-arm64-jit-debug --parallel 3
ctest --preset macos-arm64-jit-debug
cmake --install build/macos-arm64-jit-debug
```

Debug corresponds to upstream `make TYPE=debug`, independently of the emulator
flavor. Unix Debug uses `-g -Og`. MSVC has no `-Og`, so Windows Debug uses
`/Od /Zi` and `/UNDEBUG`. Debug enables the VM's `DEBUG` checks (including
assertions, term checks and lock checking) and keeps assertions active in
runtime support.
It is intended for VM development and is slower than Release. Erlang `.beam`
debug information is a separate compiler setting. Direct CMake configurations
can select `-DCMAKE_BUILD_TYPE=Debug`; supported build types are `Release` and
`Debug`. Only the single-config `Ninja` generator is supported.

Presets are available only on their matching host OS. CMake detects the target
system architecture. Standard build output lives in
`build/<preset-name>` and installs in `install/<preset-name>`.
Use a separate prefix for each flavor and build type. Existing preset names
select Release; `-debug` presets use their own directories. For a custom
installation destination, pass `--prefix /absolute/path` to `cmake --install`;
`DESTDIR` staging is also supported. Direct CMake configurations retain the standard
`CMAKE_INSTALL_PREFIX` and `CMAKE_INSTALL_BINDIR` controls.

A Unix prefix contains `bin/beam`, `bin/erl_child_setup`, and relative aliases
`beam.smp` and `beam.emu` or `beam.jit`. Debug also includes the relative helper
alias `erl_child_setup.debug -> erl_child_setup`, which the Debug VM requires
at startup. A Windows prefix contains `bin/beam.exe` and file copies
`beam.smp.exe` and `beam.emu.exe` or `beam.jit.exe`. Windows does not build
`erl_child_setup`. Debug also installs `beam.pdb` and copies of it beside the
flavor executables. This is a minimal VM installation:
OTP applications, an `erl` launcher, bootstrap bytecode, private test support,
and a public SDK are not installed. An OTP application tree and boot file must
be supplied explicitly to run Erlang code. The general Erlang examples above
assume an OTP distribution with its own command-line tools.

### Runtime validation

The standard CTest configuration registers `runtime.build` and `runtime.install`.
They check the build-tree VM, installation into a path containing spaces,
repeat installation, `DESTDIR` staging, and relocation. Every boot verifies the
selected runtime type and whether Debug and lock checks are enabled. The checks
boot with an isolated environment using a copied fixture from `bootstrap/`, exercising the
selected poll backend and emulator flavor, timers, messaging, files, sockets,
child execution, and newly compiled and loaded bytecode. The fixture is
upstream's bootstrap input from commit
`550d7b7898706c7822362c42e7b93120c1d1f29a`, matching `UPSTREAM_COMMIT`; its
application versions are bootstrap versions rather than an installed OTP 29
distribution. Set `BUILD_TESTING=OFF` to omit these checks. An absolute
`CMAKE_INSTALL_BINDIR` skips the installation/relocation test; the build-tree
runtime test still runs.

For a manual boot using that fixture from the repository root:

```sh
mkdir -p ../tmp/manual-boot
printf '{lookup,[file]}.\n{domain,"beam.test"}.\n' > ../tmp/manual-boot/inetrc
runtime_bin="$PWD/install/macos-arm64-emu/bin"
ERL_INETRC="$PWD/../tmp/manual-boot/inetrc" BINDIR="$runtime_bin" \
  "$runtime_bin/beam" -S 2:2 -SDcpu 1 -SDio 1 -K true -- \
  -root "$PWD/bootstrap" -bindir "$runtime_bin" -progname beam \
  -boot "$PWD/bootstrap/bin/start_clean" -noshell -noinput \
  -eval 'io:format("~p~n", [erlang:system_info(emu_flavor)]), erlang:halt(0).'
```

`BINDIR` must be absolute and contain the built helper. Raw `beam` takes
single-dash VM flags before `--`; boot/init arguments follow the delimiter.
`beam -V` checks version reporting but does not boot the VM.
The test/manual resolver policy uses files and a fixed domain so startup does
not require the `inet_gethost` DNS helper, which is outside this minimal install.

### Selected upstream emulator suites

`BEAM_BUILD_EMULATOR_TESTS` defaults to `OFF`. Enabling it adds private Common
Test support and five unchanged suites from `erts/emulator/test/`:

- `prim_eval_SUITE`
- `atomics_SUITE`
- `counters_SUITE`
- `num_bif_SUITE`
- `process_max_heap_size_SUITE`

These suites contain 24 cases in total. This selection runs complete suites,
retains their upstream hooks, and requires no failed or skipped cases. It is
independent of the full-corpus integration below. The existing runtime CTest
checks remain available.

Use a separate temporary configuration for this optional integration. From the
repository root, the local macOS interpreter commands are:

```sh
cmake --preset macos-arm64-emu \
  -B ../tmp/beam-emulator-tests-macos-arm64-emu \
  -DCMAKE_INSTALL_PREFIX="$PWD/../tmp/beam-emulator-tests-macos-arm64-emu/install" \
  -DBEAM_BUILD_EMULATOR_TESTS=ON
cmake --build ../tmp/beam-emulator-tests-macos-arm64-emu \
  --parallel 3 --target check-emulator
```

Use `macos-arm64-jit` and the corresponding temporary directory suffix for JIT.
To check Debug, use the corresponding `-debug` preset and a distinct temporary
directory, such as `../tmp/beam-emulator-tests-macos-arm64-emu-debug`.
Each configuration compiles and runs support on its own candidate VM.

The `beam_emulator_test_support` target prepares the isolated bootstrap, compiles
the pinned support and selected suites with the candidate VM and retained
bootstrap compiler, and records their identities. `check-emulator` builds this
support and runs the CTest `emulator` label. To rerun prepared checks directly:

```sh
ctest --test-dir ../tmp/beam-emulator-tests-macos-arm64-emu \
  -L '^emulator$' --output-on-failure --no-tests=error -j1
```

Tests use separate workspace `tmp/` work and log directories and the matching
built child helper. They do not discover an external `erl` or `erlc` or consume
a sibling distribution. Failure logs are retained for diagnosis. Private test
support is excluded from production installation. Keep task-specific options
out of standard `build/` and `install/` preset directories; report other flavors
and platforms as unvalidated until checked independently.

### Complete upstream emulator corpus

`BEAM_BUILD_FULL_EMULATOR_TESTS` independently defaults to `OFF`. The full native
fixture implementation targets macOS arm64 and Windows amd64 MSVC with either
emulator flavor and additionally requires Python 3.9 or newer. Windows full
corpus qualification is pending. The full integration remains disabled in
standard presets and CI.

The full integration prepares 104 retained suites and 17 variants generated
with the pinned upstream no-optimization and stripped-type recipes. It restores
the private helper/application closure, builds native fixtures, honors all 29
corpus recipes and stages matching preload inspection metadata. Native
extensions resolve callbacks in the candidate process; they do not link a
second runtime. None of this support enters the production installation.
Both integrations share framework preparation for syntax_tools and Common
Test. The full preparer compiles the complete corpus directly, without first
compiling the five-suite selection into a separate test directory.

For the interpreter, use a separate full-corpus configuration:

```sh
cmake --preset macos-arm64-emu \
  -B ../tmp/beam-emulator-full-macos-arm64-emu \
  -DCMAKE_INSTALL_PREFIX="$PWD/../tmp/beam-emulator-full-macos-arm64-emu/install" \
  -DBEAM_BUILD_FULL_EMULATOR_TESTS=ON
cmake --build ../tmp/beam-emulator-full-macos-arm64-emu \
  --parallel 3 --target check-emulator-full
```

Use `macos-arm64-jit` and a separate `...-jit` temporary directory for JIT. Debug
uses the corresponding `-debug` preset and a distinct temporary directory.
Preparation and execution verify the selected runtime type and helper alias.
On native Windows, load the x64 MSVC environment and use
`windows-x86_64-emu`, `windows-x86_64-jit`, or their corresponding `-debug`
presets with separate workspace `tmp/` build and installation directories.
Enable the full option in those temporary configurations and use the same
support targets, diagnostic CTest entries and focused launcher described below.
Windows validates the selected `beam.exe` and has no child-setup helper alias.
Run the independent selected-suite gate and account for each prepared suite
before attempting complete ordered qualification.

The `beam_emulator_full_native` target builds private native inputs;
`beam_emulator_full_test_support` prepares the complete test image and receipts.
`check-emulator-full` builds that support and runs the single CTest
`emulator.full` entry. To rerun prepared full checks directly:

```sh
ctest --test-dir ../tmp/beam-emulator-full-macos-arm64-emu \
  -L '^emulator-full$' --output-on-failure --no-tests=error -j1
```

For individual suites, prepare the same support target and use its separate
diagnostic CTest directory:

```sh
cmake --build <build-dir> --target beam_emulator_full_test_support
ctest --test-dir <build-dir>/tests/emulator/full/suites \
  -R '^emulator.suite.atomics_SUITE$' --output-on-failure --no-tests=error -j1
```

Preparation generates one `emulator.suite.<SUITE>` entry per retained suite and
generated variant. Root CTest traversal does not include this directory, so a
normal root run executes the complete campaign once. An unfiltered run in the
diagnostic directory executes suite diagnostics; it does not establish ordered
complete-corpus acceptance. Whole-suite selection of either `a_SUITE` or
`z_SUITE` runs that lifecycle pair, in order, in one VM and records both outcomes.
Group/case narrowing of those endpoints is rejected; select the complete pair.

Use the focused launcher for groups or cases in the same prepared image:

```sh
python3 <build-dir>/tests/emulator/full/run.py \
  --suite trace_call_count_SUITE --group legacy_pre_post --cases basic --timeout 300
```

Both interfaces invoke the same runner, with matching preparation, input
validation, scope/skip policy, isolated evidence and process cleanup. Grouped
cases require their group context. A fully accounted selection containing only
audited exclusions is reported as **Not applicable**, with exit code `77`; only
diagnostic CTest entries translate that code to a skipped test. Empty execution,
failures, automatic skips and unexplained skips remain failures. Companion-suite
passes cannot make a zero-pass target green. The full-corpus gate stays strict.
The latest diagnostic evidence pointer is
`tests/emulator/full/latest-focused-run.txt`.

Full execution uses the unchanged base `emulator.spec` in one ordered Common
Test run, preserving `a_SUITE`/`z_SUITE` lifecycle state and the specification's
own hook and benchmark policies. The selected-suite `emulator` label and
`check-emulator` target continue to run only the original five suites when that
separate option is enabled.

Each full attempt creates a private image and logs under workspace `tmp/` and
announces its evidence directory. The prepared receipt binds the candidate,
Unix child helper when applicable, flavor, bootstrap, source/data/specification
inputs, native sources/flags and staged hashes. A run starts an owned loopback
EPMD endpoint and records child-process identities; cleanup checks those identities and
fails if owned processes remain. It consumes no sibling product or external
Erlang runtime. Candidate preparation subprocesses have a one-hour bound; full
Common Test execution has a six-hour bound.

The private Windows `erl.exe` reuses the retained upstream argument parser and
launches the exact standalone candidate executable. DLL fixtures obtain their
callbacks from that candidate. Native Windows EI and OS_Mon helpers stay in the
test closure. Process receipts bind the Windows creation time, owner and image;
per-run job containment and identity checks govern child-process cleanup.

A full run passes only with complete event/coverage accounting, no failed or
automatically skipped cases, and no unaudited user skips. Accepted user skips
must match an exact pinned upstream guard and recorded runtime, platform or
resource condition in `tests/emulator/emulator_skip_policy.json`. Missing helpers,
applications, native artifacts or resource discovery remain failures. Evidence
records CT results, individual accepted/rejected skips and process cleanup;
`<build-dir>/tests/emulator/full/latest-prepare.txt` and `latest-run.txt` identify
the latest attempt directories. Successful preparation alone does not establish
a passing full run; report results separately for each flavor and platform.

## Bug Reports

Please visit our [GitHub Issues](https://github.com/erlang/otp/issues) page for reporting bugs. The instructions for submitting bugs reports [can be found here](https://github.com/erlang/otp/wiki/Bug-reports).

### Security Disclosure

Please do not report security vulnerabilities through public channels, like
GitHub issues, discussions, or pull requests.

Please disclose the security issues following our [SECURITY](SECURITY.md) guidelines.

## Contributing

We are grateful to the community for contributing bug fixes and improvements. Read below to learn how you can take part in improving Erlang/OTP. We appreciate your help!

### Contribution Guide

Read our [contribution guide](CONTRIBUTING.md) to learn about our development process, how to propose fixes and improvements, and how to test your changes to Erlang/OTP before submitting a pull request.

### Help Wanted

We have a list of [Help Wanted](https://github.com/erlang/otp/issues?q=is%3Aissue+is%3Aopen+label%3A%22help+wanted%22) bugs that we would appreciate external help from the community. This is a great place to get involved.

## Awesome-Erlang

You can find more projects, tools and articles related to Erlang/OTP on the [awesome-erlang list](https://github.com/erlang-punch/awesome-erlang). Add your project there.

## License

Erlang/OTP is released under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0).

For any license inquiry, please send an email to opensource@ericsson.com

> %CopyrightBegin%
>
> SPDX-License-Identifier: Apache-2.0
>
> Copyright Ericsson AB 2010-2025. All Rights Reserved.
>
> Licensed under the Apache License, Version 2.0 (the "License");
> you may not use this file except in compliance with the License.
> You may obtain a copy of the License at
>
>     http://www.apache.org/licenses/LICENSE-2.0
>
> Unless required by applicable law or agreed to in writing, software
> distributed under the License is distributed on an "AS IS" BASIS,
> WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
> See the License for the specific language governing permissions and
> limitations under the License.
>
> %CopyrightEnd%
