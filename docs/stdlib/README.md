# BEAM libraries

The modern BEAM/Erlang libraries are an explicit set of Erlang interfaces.
They use ordinary Erlang values and processes, with separate selection of native
capabilities. They do not yet provide a complete replacement for the Erlang/OTP
`stdlib` or `kernel` applications.

Start with the [base-library reference](base.md) for processes, collections,
bytes, text, mathematics, drivers, debugger support, formatting and time. Use the [boot and host-service
reference](services.md) for completion, output and explicit code acquisition.
File, socket and compression references live with their owning components.
The separate [tracing library](../components/tracing/README.md) provides session
and sequential tracing through ordinary `beam_trace`.

## Module catalog

### Embedded core

Every VM embeds exactly these six modules in `libbeam`, without acquiring
external bytecode:

| Modules | Role |
| --- | --- |
| `erlang` | Language/runtime primitives; availability of optional facilities still depends on the composition. |
| `erts_internal`, `erts_code_purger`, `erts_literal_area_collector`, `erts_dirty_process_signal_handler`, `beam_internal` | Internal runtime support, not application-level modern library APIs. |

The array and persistent-store mechanisms are linked once into the VM. Their
public facades are ordinary selected code. Modern hosts use `beam_atomics`,
`beam_counters` and `beam_persistent_term`; OTP supplies its unchanged `atomics`,
`counters` and `persistent_term` preloads. The six VM-private array mechanisms
remain runtime mechanisms for OTP's ordinary constructor/counter wrappers.
Their callable helper MFAs require explicit native bindings.

### Ordinary base library

The host explicitly supplies the following ordinary modules from
`lib/beam/library/ebin`, using the `lib/beam/library/modules.txt` inventory.
They are ordinary loaded code, not additional preloads. Loading them starts no
extra service or process and selects no OS capabilities.

| Module | Interface |
| --- | --- |
| [`beam`](base.md#beam) | Process identity, spawning, messages, monitors, links and exceptions |
| [`beam_binary`](base.md#beam_binary) | Binary byte operations |
| [`beam_lists`](base.md#beam_lists) | List operations |
| [`beam_maps`](base.md#beam_maps) | Map operations |
| [`beam_string`](base.md#beam_string) | Unicode string operations |
| [`beam_unicode`](base.md#beam_unicode) | Unicode encoding and normalization |
| [`beam_records`](base.md#beam_records) | Native record operations |
| [`beam_ets`](base.md#beam_ets) | VM-owned term tables, matching and traversal |
| [`beam_math`](base.md#beam_math) | Scalar mathematics and floating constants |
| [`beam_ddll`](base.md#beam_ddll) | Linked-in driver loading, monitoring and information |
| [`beam_debugger`](base.md#beam_debugger) | Experimental debugger instrumentation and process inspection |
| [`beam_format`](base.md#beam_format) | Decimal integers and bounded diagnostic rendering |
| [`beam_time`](base.md#beam_time) | Clocks, timers, unit conversion and VM-local monotonic deadlines |
| [`beam_atomics`](base.md#beam_atomics) | Shared 64-bit atomic arrays |
| [`beam_counters`](base.md#beam_counters) | Atomic or write-concurrency counters |
| [`beam_persistent_term`](base.md#beam_persistent_term) | VM-wide persistent values |

The six collection/binary/text modules contain 224 source exports, including
51 native slots selected explicitly by modern hosts. Private
`beam_runtime_unicode_util` supplies their shared Unicode algorithms and is part
of the ordinary library inventory. Array/store and clock APIs retain their own
bindings. Supplying code starts no services and adds no implicit acquisition.

`beam_math`, `beam_ddll` and `beam_debugger` preserve 57 upstream exports with
44 selected native slots. Their ordinary constants and wrappers remain in the
base library. Driver admission and debugger enablement are separate from module
delivery. OTP independently retains `math`, `erl_ddll` and `erl_debugger`.

### Ordinary standard library

The [standard-library reference](collections.md) covers `array`, `dict`, `graph`,
`gb_sets`, `gb_trees`, `orddict`, `ordsets`, `proplists`, `queue`, `sets`, `sofs`,
`digraph` and `digraph_utils`. These modules preserve their OTP 29.0 names,
467 function exports and 44 exported types. All functions are ordinary Erlang;
their implementations call the base library and existing core language helpers.

Normal `beam run`, shell and eval verify and acquire
`lib/beam/stdlib/{modules.txt,ebin/*.beam}` before execution. Embedding hosts
select this inventory explicitly. Supplying it starts no process or service.
The modules are maintained directly under `src/beam/stdlib`. Ordinary functions
in a base module remain with that module.

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
Names such as `io`, `io_lib`, `erl_scan`, `erl_eval`, `shell` and `ms_transform` can
therefore appear in an execution command's loaded code.
Their presence in that command does not make them part of a minimal embedding
composition or establish a general modern-library extraction.
The shell also retains private `beam_runtime_queue`, `beam_runtime_sets`,
`beam_runtime_proplists` and `beam_runtime_orddict` helpers. Public collection
APIs come from the separate standard-library inventory.

The network component carries the same verified `beam_lists`/`beam_maps` bytes
as base deployment, plus its private legacy string helper. A host selecting both
must explicitly partition those two module records; generic composition rejects
duplicates. The CLI verifies the complete base inventory before that partition.
The helper is supplied by CLI fallback only when networking is absent.

`beam_ets` shares all 51 native ETS contracts with OTP's `ets` facade, using
host-selected names and ordinary native dispatch. Its public ordinary base
module has 63 source exports, including 12 Erlang wrappers/algorithms. Native
functions remain callable in explicitly selected native-only compositions;
ordinary helpers require the supplied bytecode. The facade acquires no DETS,
QLC, file services or evaluator. Compiler and shell profiles supply match-spec
tooling explicitly. Four hidden native orchestration exports retain their
implementation-support roles.

Compiler-private helpers belong to a separate compiler VM lifetime. They are
not an application API and do not carry into the execution VM. Broader library
coverage, complete language-reference integration and extraction of retained
helpers are future documentation/product work, not capabilities implied by this
catalog.

## API conventions

The six upstream-based modules preserve upstream argument order and result
conventions. For example, `beam_lists:map(Fun,List)` takes the callback first,
`beam_maps:put(Key,Value,Map)` takes the map last, and `beam_maps:find/2` uses
`error` for absence. Other interfaces retain their individually documented
contracts. A uniform value-first or tagged-result convention is not implied.
