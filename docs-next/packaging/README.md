# Packages and projects

The experimental package tool builds ordinary Erlang libraries and local
dependency graphs, with optional CMake native outputs. It uses the selected
BEAM/Erlang installation and its compiler. The format and commands remain
revisable.

## Build, install and launch

For a project with a `beam.toml` source manifest:

```sh
beam build .
beam-pkg install .
beam run --project . -- first second
```

These are separate steps:

1. **Build** compiles explicit sources and produces content-addressed package
   archives plus `beam.lock`. It does not install or run the application.
2. **Install** verifies the locked archive selection and publishes a project-local
   immutable environment under `.beam/`.
3. **Run** executes the installed root's declared `main/1` in a fresh VM.
   It does not rebuild, install or resolve missing sources.

Use `beam shell --project .` to explore the installed selection; library roots
need no entry for this command. Project execution supplies standard I/O and
selected package/native bindings. It does not implicitly add the services
available to plain source execution; see [execution profiles](../cli/README.md#execution-profiles).

Build needs CMake on `PATH`; native recipes additionally need their toolchain.
Install uses CMake for digest verification but does not compile or run recipes.
Installed launch needs neither the compiler nor CMake. Sources and dependencies
must already be available; the tool does not fetch them automatically.

## Format and command reference

The package repository owns the specification. Follow these maintained
references rather than treating this introduction as another schema:

| Reference | Contents |
| --- | --- |
| [Package format](../../../beam-erlang-package/docs/package-format.md) | `beam.toml`, supported TOML subset, exact dependency identities, native recipes/bindings, archive contents, locks and installed environment records. |
| [Package commands](../../../beam-erlang-package/docs/commands.md) | Build/install behavior, failure/status contracts, project shell and source tasks. |

These links require the companion `beam-erlang-package` checkout in the workspace.
The publication location for these cross-repository references is not yet set.

`beam task` executes root-manifest `[scripts]` as literal argument arrays. Tasks
belong to the source checkout and are excluded from archives, locks and installed
environments. They do not become automatic build or installation hooks.

## Current limits

Dependency-tree display (`beam-pkg tree`) remains unimplemented. Registry access,
whole OTP application recipes and compiler plugins are deferred. Package names
do not namespace Erlang modules; duplicate module providers are rejected.
Native recipes and libraries are trusted code and require matching SDK
provenance; they are not a stable third-party component ABI.

The separate Erlang/OTP distribution has its own assembly and commands. Delivering
it as these experimental packages is not a prerequisite for using that product.
