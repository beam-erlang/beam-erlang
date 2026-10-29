# Standard library

The modern BEAM/Erlang library is a small, explicit set of Erlang interfaces.
It uses ordinary Erlang values and processes, with separate selection of native
capabilities. It does not yet provide a complete replacement for the Erlang/OTP
`stdlib` or `kernel` applications.

Start with the [base-library reference](base.md) for processes, collections,
bytes, text, formatting and time. Use the [boot and host-service
reference](services.md) for completion, output and explicit code acquisition.
File, socket and compression references live with their owning components.

## Module catalog

### Embedded core

Every VM embeds exactly these nine modules in `libbeam`, without acquiring
external bytecode:

| Modules | Role |
| --- | --- |
| `erlang` | Language/runtime primitives; availability of optional facilities still depends on the composition. |
| `atomics`, `counters`, `persistent_term` | Shared-data primitives. |
| `erts_internal`, `erts_code_purger`, `erts_literal_area_collector`, `erts_dirty_process_signal_handler`, `erts_trace_cleaner` | Internal runtime support, not application-level modern library APIs. |

Embedded core does not mean every export is usable without its optional
dependencies. These modules retain VM machinery and do not bring in OTP
applications. The retained shared-data source references are
[atomics](../../src/erts/preloaded/src/atomics.erl),
[counters](../../src/erts/preloaded/src/counters.erl) and
[persistent_term](../../src/erts/preloaded/src/persistent_term.erl); a complete
reader-facing reference for these inherited APIs remains to be integrated.

### Ordinary base library

The host explicitly supplies the following seven modules from
`lib/beam/library/ebin`, using the `lib/beam/library/modules.txt` inventory.
They are ordinary loaded code, not additional preloads. Loading them starts no
extra service or process and selects no OS capabilities.

| Module | Interface |
| --- | --- |
| [`beam`](base.md#beam) | Process identity, spawning, messages, monitors, links and exceptions |
| [`beam_list`](base.md#beam_list) | List mapping and left folding |
| [`beam_map`](base.md#beam_map) | Map lookup and insertion |
| [`beam_bytes`](base.md#beam_bytes) | Binary size and slicing |
| [`beam_text`](base.md#beam_text) | UTF-8 validation and concatenation |
| [`beam_format`](base.md#beam_format) | Decimal integers and bounded diagnostic rendering |
| [`beam_time`](base.md#beam_time) | VM-local monotonic deadlines |

### Boot and host services

| Public interface | Supplied inventory | Availability |
| --- | --- | --- |
| [`beam_boot`](services.md#beam_boot) | `lib/beam/boot` | Returning-boot context |
| [`beam_output`](services.md#beam_output) | `lib/beam/services` | Explicit output service and host sink |
| [`beam_code`](services.md#beam_code) | `lib/beam/services` | Explicit host acquisition for `fetch/3`; publication is separate |
| [`beam_host`](services.md#beam_host) | `lib/beam/services` | Low-level host-request bridge for embedding protocols |

The complete boot inventory also includes internal `beam_return_boot`; the
aggregate services inventory also includes `beam_startup`. Independent
`startup`, `host`, `output` and `code` inventories permit smaller selections.
Loading either inventory alone grants no capabilities. Hosts supply and verify
whole selected inventories; applications use the public facades above.

### Native component interfaces

| Module | Owning component and reference |
| --- | --- |
| `beam_file` | [libbeam-file.so](../components/file/README.md) |
| `beam_socket` | [libbeam-network.so](../components/network/README.md) |
| `zlib` | [libbeam-zlib.so](../components/zlib/README.md) |

Each component document owns its module reference, embedded support inventory,
capability requirements and resource behavior. This catalog links to those
documents rather than duplicating their contracts.

## Command support is not the public base library

The CLI supplies private compiler, source-run and interactive inventories.
Names such as `io`, `io_lib`, `erl_scan`, `erl_eval`, `shell`, `lists`, `maps`,
`unicode`, `sets` and `queue` can therefore appear in a command's loaded code.
Their presence in that command does not make them part of a minimal embedding
composition or establish a general modern-library extraction.

`lists` and `maps` also occur in the network component's support inventory;
the CLI supplies matching fallback copies when networking is absent. Inventory
composition must avoid duplicate module identities. The supported command I/O
and shell behavior is documented by the [CLI guide](../cli/README.md).

Compiler-private helpers belong to a separate compiler VM lifetime. They are
not an application API and do not carry into the execution VM. Broader library
coverage, complete language-reference integration and extraction of retained
helpers are future documentation/product work, not capabilities implied by this
catalog.

## API conventions

The modern base interfaces generally put the value first, for example
`beam_list:map(List, Fun)` and `beam_map:put(Map, Key, Value)`. Successful pure
operations return the value directly. Fallible operations use their documented
result shape; `beam_map:find/2` returns `not_found`, while service operations
usually return `{error, ErrorMap}`. Do not assume that retained `zlib` follows
the same convention.

Explicit argument checks reject invalid types, options and ranges with
`badarg`; list operations expect proper lists. User callback exceptions
propagate. Binary bytes, UTF-8 text and filesystem paths have distinct
contracts. The library does not implicitly convert charlists or normalize text,
and applications should not turn unbounded external names into atoms.

An omitted module remains absent. Undefined calls do not trigger directory
searches or implicit loading. See [native component selection](../components/README.md)
and the [C API guide](../c-api/guide.md) for constructing a composition.
