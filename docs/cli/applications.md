# Installed applications

`beam run --application application.etf -- arguments...` executes an explicit
application inventory. Everything after `--` is literal data. Arguments are
Unicode character lists, including empty strings. The application never searches
for additional modules or a substitute runtime. Ordinary source execution,
compilation, evaluation and the plain shell remain separate commands.

The application descriptor is an experimental runtime interface, separate from
package manifests. Generate it at build time using the installed
`share/beam/tools/beam-application.py` (Python 3.11 or newer):

```sh
python3 "$SDK/share/beam/tools/beam-application.py" create \
  --root ./application --inventory ./application/modules.txt \
  --entry example:main --name example --version 1 \
  --service output --output ./application/application.etf
"$SDK/bin/beam" run --application ./application/application.etf -- 'literal argument'
```

The inventory contains one module name per line, with its bytecode in
`ebin/<name>.beam`. `validate descriptor` checks the generated descriptor and its
files without executing Erlang. `--runtime-build` adds an exact supported runtime
build identity; repeated values describe explicitly qualified combinations. An
empty list requests only the declared feature contract, with bytecode admission
still performed by the selected VM. The package tool records the runtime used to
produce and qualify its payload.

The uncompressed ETF representation is:

```erlang
{beam_application, 1, IdBinary, VersionBinary, RootBinary,
 {main | command | shell, ModuleBinary, FunctionBinary},
 [application_v1 | command_v1 | native_v1],
 [output | code | file | network | zlib | program],
 [{file, ModuleBinary, RelativePathBinary, Size, SHA256Binary}],
 [{ModuleBinary, RelativeNativePathBinary}],
 [RuntimeBuildIdBinary], [RequiredHostModuleBinary]}
```

Paths are relative to the declared application root. That root is relative to the
descriptor directory and must remain within it. Resolved module and native paths
must remain inside the application root. Files, recorded sizes and SHA-256 hashes,
required features, runtime identities, host requirements and provider conflicts
are checked before application entry. Runtime-owned inventories retain their
matching-build verification. External module names are never compiled into `beam`.
Descriptors are bounded to 2 MiB, 4096 external modules and 256 MiB of module bytes;
the composition's total module limit also includes runtime support. ETF compression
and trailing data are rejected. SHA-256 values are 32 raw bytes, not hexadecimal.

`main(Argv)` is the ordinary application contract. A natural return succeeds,
regardless of its value. Requested halt preserves the status; an exception fails.
The runner supplies standard I/O only when output is selected. Capabilities are
explicit and selecting modules alone enables none.

`command(Context, Argv)` supports command applications. Its context is:

```erlang
{beam_context, 1, RuntimeExecutable, InstallationRoot, RuntimeIdentity,
 BuildIdentity, ApplicationRoot, WorkingDirectory, SearchPath, Platform,
 DescriptorETFBytes}
```

Paths and identities are UTF-8 binaries. `Platform` identifies native process
semantics. A command returns exactly one of these versioned actions:

```erlang
{beam_command, 1, {exit, Status}}
{beam_command, 1, {run, NextDescriptor, term_to_binary(NextArgv)}}
{beam_command, 1, {exec, WorkingDirectoryBinary, [ExecutableBinary | ArgumentBinaries]}}
```

Exit status is 0–255. A continuation copies its inputs, terminates the previous VM
and starts a fresh composition. Its descriptor may use an absolute application
root and `{memory, ModuleBinary, Bytes, OriginBinary}` records for bytes already
validated by application policy. The `shell` entry selects the runtime's ordinary
interactive composition without invoking a project entry. Native bindings remain
explicit and subject to matching-SDK admission. Static libraries require a newly
linked embedding consumer; an existing command cannot dynamically load them.

Native handoff takes place only after VM and system cleanup succeed. It preserves
literal arguments, standard streams, terminal behavior and native exit status. No
shell is inserted. Native process lookup follows the host platform; package command
selection belongs to the package application. Invalid actions fail. A process is
limited to 64 successive compositions.

`bin/beam-launch` is a generic native launcher with no `libbeam` dependency. Copy it
to an application's command name and install `<command>.beam-launcher` next to it.
The sidecar has four newline-terminated UTF-8 lines:

```text
beam-launcher-v1
relative
beam
../lib/beam/apps/example/1/application.etf
```

The runtime binding is either `relative` (from the launcher's directory) or
`absolute` (an explicitly selected installed executable). The descriptor path is
relative to the launcher's directory. Use the platform's executable suffix.
Missing selections fail; there is no PATH fallback to another runtime. Bundled
commands use relative bindings. Standalone tools bind an installed runtime and can
move independently while that runtime stays at its selected location.

This implementation is undergoing cross-platform qualification. The intended
platform matrix is not evidence that every host or native handoff has passed.
