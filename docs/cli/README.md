<!--
%CopyrightBegin%

SPDX-License-Identifier: Apache-2.0

Copyright Ericsson AB 2023-2025. All Rights Reserved.

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
# Command-line manual

Use commands from one explicitly selected installation. `beam help` lists the
commands; `beam help <command>` describes a command's syntax. `beam version`
prints the distribution identity. Commands use modern BEAM/Erlang; the separate
OTP distribution owns `erl`, `erlc` and `escript`.

## Commands

| Command | Purpose |
| --- | --- |
| `beam`, `beam shell` | Open the interactive Erlang shell. |
| `beam eval '<expressions>'` | Evaluate a sequence with fresh bindings; print only requested output. |
| `beam run file.erl [-- args...]` | Compile in memory and execute `main/1` with Unicode charlist arguments. |
| `beam run --application descriptor [-- args...]` | Execute an explicit generic application inventory; see [applications](applications.md). |
| `beam compile [options] [--] files...` | Compile explicit source files into bytecode. |
| `beam-pkg build [directory]` | Build the local package graph; the directory defaults to `.`. |
| `beam-pkg run --project directory [-- args...]` | Run an already installed root package's entry. |
| `beam-pkg shell --project directory` | Open an installed selection, including a library with no entry. |
| `beam-pkg task [--project directory] [name [-- args...]]` | List or execute source-manifest tasks. |

`beam-pkg` supplies package commands, including `beam-pkg install`. See
[packages and projects](../packaging/README.md) for the workflow and canonical
package-command reference. Runtime-family flags are rejected. Arguments after
the program/task separator `--` are literal program data.

## Shell and evaluation

The shell supports persistent bindings within a session, multiline expressions,
local functions, history, Unicode editing and recovery after evaluation errors.
Expressions end with a period. `h()` shows history; `v(N)` retrieves a saved
result. History and saved-result limits default to 20; `history/1` and `results/1`
change them for that VM. `q().` or piped EOF exits successfully.

OTP helpers for compiling/loading (`c`, `l` and their variants), filesystem
navigation, record-file import and documentation discovery are unavailable.
They report an unsupported-helper error without making the session unusable.
Undefined calls do not trigger code discovery.

```sh
beam eval 'io:format("~p~n", [2 + 3]).'
```

Successful evaluation returns status 0. Parsing, startup and uncaught execution
errors report diagnostics and return 1. Supported `erlang:halt(Status)` preserves
the requested status after cleanup.

### Terminal editing

On a terminal, the host edits a UTF-8 input line before submitting it to the
shell. `Ctrl-X` below means holding Ctrl while pressing X. The following bindings
are implemented:

| Key | Action |
| --- | --- |
| Left / `Ctrl-B` | Move back one character. |
| Right / `Ctrl-F` | Move forward one character. |
| Home / `Ctrl-A` | Move to the beginning of the line. |
| End / `Ctrl-E` | Move to the end of the line. |
| Backspace / `Ctrl-H` | Delete the character before the cursor. |
| Delete | Delete the character at the cursor. |
| `Ctrl-D` | Delete at the cursor, or send EOF when the line is empty. |
| `Ctrl-U` | Delete from the beginning of the line to the cursor. |
| `Ctrl-K` | Delete from the cursor to the end of the line. |
| `Ctrl-W` | Delete the preceding whitespace and word. |
| Up / `Ctrl-P` | Recall the preceding submitted line. |
| Down / `Ctrl-N` | Recall the next submitted line, or restore the draft. |
| Enter | Submit the line; the shell continues reading if the expression is incomplete. |
| `Ctrl-C` | Interrupt input and return to a shell prompt. |

Line recall belongs to this terminal session. It is distinct from the shell's
expression history accessed through `h()`, `e(N)` and `v(N)`. These bindings do
not provide completion, a shell job-control menu, an external editor or history
search. Piped input is read without the terminal line editor.

## Source execution

`beam run` requires a source file exporting `main/1`. It compiles without writing
an intermediate `.beam`, terminates the compiler VM, and executes the result in
a fresh VM. Returning from `main/1` completes execution even if child Erlang
processes remain. An uncaught exception produces diagnostics and status 1.
See the complete [first source example](../getting-started.md#run-a-source-file).

## Compilation

```sh
beam compile --deterministic --depfile build/example.d \
    -I include -DDEBUG -o build/ebin -- example.erl
```

| Option | Meaning |
| --- | --- |
| `-o DIR` | Output directory; created if needed, defaulting to the current directory. |
| `-I DIR`, `-IDIR` | Additional include directory; repeatable in argument order. |
| `-D NAME[=TERM]`, `-DNAME[=TERM]` | Define a macro; the default value is `true`. Values are parsed Erlang terms. |
| `--warnings-as-errors` | Treat compiler warnings as failure. |
| `--deterministic` | Enable deterministic output policy; arbitrary transforms may still introduce nondeterminism. |
| `--no-transforms` | Reject source parse/core transforms before execution. |
| `--depfile FILE` | Write Make-compatible dependencies for exactly one source. |
| `--` | Treat all remaining arguments as filenames. |

The declared module must match the `.erl` filename. Output is `<module>.beam`.
The command is quiet on success and writes diagnostics to stderr. Multiple
sources compile in order; the first failure stops compilation, retaining earlier
successful outputs. A failed compilation does not replace that source's previous
output. Multi-file publication is not one transaction.

Unknown options and repeated macro definitions fail. `ERL_COMPILER_OPTIONS` is
ignored; there is no arbitrary compiler-option passthrough. Transform support is
limited by the compiler host's explicitly supplied facilities. Package builds
disable transforms. Depfiles record inputs observed during preprocessing;
transform-specific file reads must be declared separately by the build client.
Dependency paths containing newlines or `=` are rejected. Spaces and Unicode
are supported; other shell/Make metacharacters can encounter additional quoting
limits in the consuming build generator.

## Execution profiles

| Profile | Available facilities |
| --- | --- |
| Source run, plain shell and eval | Standard I/O plus output, explicit code acquisition, file, socket, zlib and external-program services. |
| Installed project run | Core/base, standard I/O, selected package modules and declared native bindings. |
| Installed project shell | Project facilities plus the interactive inventory. |

Project launch never builds or installs. It adds no implicit filesystem,
network, program or code-acquisition service. Source-run I/O supports line and
character input; term scanning and formatted input belong to shell/eval.
No profile gains OTP applications or hostname resolution implicitly.

Use the [standard library](../stdlib/README.md) and individual
[component manuals](../components/README.md) for Erlang APIs. Private modules
used to implement command I/O or compilation do not establish a general public
OTP standard-library package.

See [installed applications](applications.md) for the generic runner, descriptors,
continuations and native launcher. Package-aware commands belong to `beam-pkg`.
