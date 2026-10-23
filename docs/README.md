# BEAM/Erlang documentation

BEAM/Erlang provides the Erlang language, the BEAM virtual machine, the `beam`
command, modern libraries and an embedding SDK for C hosts. These manuals
describe the current experimental interfaces. Coverage is incomplete;
interfaces and documentation may evolve together.

## Start here

- [Preview installation and verification](getting-started/preview.md): release status,
  authenticated downloads, first use and maintenance.
- [Get started](getting-started.md): select an installation, open the shell and
  run an Erlang source file.
- [Understand the runtime](concepts.md): modules, explicit capabilities and VM
  lifetimes, including the relationship with the separate Erlang/OTP product.
- [Read Erlang code](language/README.md): a small introduction to modules,
  values, processes and messages.

## Manuals

| Manual | What it covers |
| --- | --- |
| [C API and embedding](c-api/README.md) | First call, embedding guides, API reference and SDK deployment. |
| [Native extensions](native-extensions/README.md) | NIF and driver C APIs, SDK examples and provisional loading/lifetime behavior. |
| [Runtime reference](runtime/README.md) | Time, memory, crash dumps and advanced VM formats. |
| [Modern standard library](stdlib/README.md) | Core and base modules, service facades and their availability. |
| [File component](components/file/README.md) | `libbeam-file.so`: file capabilities and `beam_file`. |
| [Network component](components/network/README.md) | `libbeam-network.so`: socket capabilities and `beam_socket`. |
| [Zlib component](components/zlib/README.md) | `libbeam-zlib.so`: compression capabilities and `zlib`. |
| [Command line](cli/README.md) | Shell, evaluation, source execution, compilation and project commands. |
| [Packages and projects](packaging/README.md) | Build/install/run workflow and the canonical package-format reference. |

The [component overview](components/README.md) explains shared selection and
activation concepts. Each component has its own manual; consult that manual for
its requirements and API. Component APIs have one documentation home and are
linked from the standard-library catalog.

## Installation and scope

Use the [build and installation instructions](../README.md#building) to obtain
the current Linux interpreter or JIT distribution. Public interfaces are
experimental, and native components require a matching build.

The selected release model follows the integrated upstream ERTS version plus a
distribution revision. This does not establish a stable C API or binary ABI;
public interface stability and support periods require explicit declarations.
The [preview guide](getting-started/preview.md) describes the current
`17.0-r1-preview.1` candidate and the remaining publication boundary.

These manuals describe the modern BEAM/Erlang product. See the
[product relationship](concepts.md#beamerlang-and-erlangotp) for the independent
OTP distribution's applications, commands and operational guidance.

The manuals provide a starting language guide and library catalog,
not a complete language or BIF reference. Runtime integration and compatibility
coverage is recorded separately in the [test guide](../tests/README.md).

Advanced references cover [external term encoding](reference/external-term-format.md),
[abstract Erlang forms](language/abstract-forms.md) and
[match specifications](reference/match-specifications.md). These describe formats
and mechanisms; their presence does not add library modules or capabilities to a
host composition.

- [Building the SDK](getting-started/building.md): target presets, native host tools and cross compilation.
