# Public C API reference

Candidate reference for the current experimental SDK.

This reference describes the experimental Linux interface. `<beam/beam.h>`
exposes `<beam/runtime.h>`. Term operations require `<beam/beam_enif.h>`;
process management additionally requires `<beam/process.h>`.
Optional returning-boot declarations require `<beam/boot.h>` and linkage with
`BEAM::Host`.
The [source header](../../include/beam/beam.h) supplies VM declarations;
[Embedding BEAM from C](guide.md) supplies a working introduction. Interpreter
and JIT expose the same API. Use matching installed headers and libraries:
there is no source/binary compatibility promise, and every `struct_size` must
match exactly rather than provide forward-compatible minimum sizing.

Use this reference by topic:

- [Common contracts and status](#common-contracts)
- [Discovery and configuration](#discovery-and-configuration)
- [Composition](#composition)
- [System and VM lifecycle](#system-and-vm-lifecycle)
- [Term environments](#term-environments)
- [Loading, inspection and scheduler control](#loading-inspection-and-scheduler-control)
- [Process management](#process-management)
- [Calling and results](#calling-and-results)
- [Host services](#host-services)
- [Returning BEAM boot](#returning-beam-boot)
- [SDK and service boundaries](#sdk-and-service-boundaries)

## Common contracts

`beam_init` selects the host's main thread; `beam_init_with_options` can select a persistent calling OS thread for host-controlled execution. The selected thread owns every VM
under it; system/VM operations are serial and reject callback reentry. There is no
cross-thread stop, concurrent observation, call cancellation or pause/resume.
One system owns one VM handle, including initialized/stopped handles. Terminating
the VM permits a fresh composition; terminating the system does not permit system
reinitialization or shared-library unloading.

`beam_system`, `beam_vm`, `beam_composition`, `beam_return_boot` and
`beam_host_reply` are opaque. Do not allocate, copy or free their contents.
Initialize owning handle variables to NULL. Termination invalidates aliases;
a copied handle does not retain ownership.

| Value | Ownership |
| --- | --- |
| `beam_bytes { const unsigned char *data; size_t size; }` | Input/borrowed view, measured in bytes; no terminating NUL requirement. |
| `beam_buffer { unsigned char *data; size_t size; }` | Library-allocated copied output, released through its enclosing result API. |
| VM options, module bytes/names/origins, call inputs | Copied/consumed by the operation; caller storage may be released after return. Composition mappings are retained separately. |
| Result buffers, module/process lists and module/scheduler/process snapshots | Caller-owned until their release function; survive VM/system termination while the library stays loaded. |
| Callback context | Borrowed host state, kept valid through stop/run completion. |
| Callback request and reply handle | Borrowed only for that invocation; copy request bytes to retain them. |
| Diagnostic/observation records | Caller-owned values without separate release obligations. |
| Version/status strings | Borrowed immutable library storage. |

Never shallow-copy an owning result and release both copies. No movable Erlang
heap pointer crosses this interface. Copied bytes do not preserve the liveness
of encoded PIDs, references, funs or native resources.

### Status and diagnostics

API status and Erlang outcome are separate. Returned errors print nothing;
internal native allocation, initialization, worker-start or invariant failures
can still terminate the process. The API does not promise universal rollback.

| Status | Value | Meaning |
| --- | ---: | --- |
| `BEAM_OK` | 0 | C operation succeeded; inspect any Erlang outcome separately. |
| `BEAM_INVALID_ARGUMENT` | 1 | Invalid pointer, size, option, string or handle. |
| `BEAM_OUT_OF_MEMORY` | 2 | Recoverable allocation failure. |
| `BEAM_RUNTIME_NOT_FOUND` | 3 | Required runtime asset/helper is absent or inaccessible. |
| `BEAM_SYSTEM_ERROR` | 4 | OS or runtime preparation/execution failure. |
| `BEAM_ALREADY_STARTED` | 5 | System initialization claimed, or VM slot occupied. |
| `BEAM_STOPPED` | 6 | VM halt/stop prevents admission. |
| `BEAM_WRONG_THREAD` | 7 | Selected owner thread required. |
| `BEAM_INVALID_BEAM` | 8 | Invalid/incompatible compiled module. |
| `BEAM_INVALID_TERM` | 9 | ETF cannot be accepted or a result cannot be encoded. |
| `BEAM_UNSUPPORTED` | 10 | Unsupported operation/configuration. |
| `BEAM_ALREADY_LOADED` | 11 | Module replacement is unavailable. |
| `BEAM_INVALID_STATE` | 12 | Lifecycle or callback reentry disallows the operation. |
| `BEAM_NOT_FOUND` | 13 | Requested module/code, process or explicit group leader is absent. |
| `BEAM_BUSY` | 14 | Code or native coordination is in use; retry after it settles. |

Functions declaring an optional `beam_error *` clear that record on entry.
It contains `int native_code` and NUL-terminated `char message[256]`.
Native code is zero when inapplicable; invalid options name the offending field.
Branch on status, not diagnostic text, and do not reuse an older operation's
error record as a new failure explanation.

`beam_build_id()` borrows the opaque identity of the loaded runtime build. It
performs no filesystem access or VM initialization and is available before
`beam_init`. The identity is a matching-build check, not a stable ABI version.

`beam_version()` returns `BEAM/Erlang <release> (erts-<version>)`.
`beam_status_message(status)` returns a general description or `"unknown error"`.
Neither starts a VM. `beam_module_core_names(&names, &count)` borrows the immutable
six-module inventory for the loaded library's lifetime. The pure
`beam_system_is_initialized()` query observes process admission, not a reservation.

`beam_library_path(buffer, capacity, &required, &error)` reports the absolute
containing runtime image: the shared library or statically linked executable.
It neither initializes the VM nor chooses an installation layout.
NULL buffer/zero capacity queries the required size including NUL. An insufficient
buffer returns `BEAM_INVALID_ARGUMENT`, leaves it unchanged and reports the size.

## Discovery and configuration

`beam_query_host(&info, &error)` fills an exact-size `beam_host_info` without
initialization, workers or resource reservation. It is repeatable and needs no
system/owner handle. Output is unchanged on error; missing individual data is
zero, not a failed query. Invalid output/size returns `BEAM_INVALID_ARGUMENT`;
recoverable allocation failure returns `BEAM_OUT_OF_MEMORY`.

| CPU field | Meaning |
| --- | --- |
| `configured_cpus` | OS-configured CPUs. |
| `online_cpus` | OS-online CPUs. |
| `available_cpus` | Main-thread Linux affinity availability, falling back to online count. |
| `quota_cpus` | Whole-number quota hint, capped by online count; positive fractions truncate with minimum one. It does not additionally apply affinity. |

Discovery chooses no scheduler policy. Call `beam_vm_options_init(&options)`
to populate all defaults; NULL is harmless. Passing NULL options to initialization
selects those defaults. The current 64-bit implementation accepts:

| VM option | Default | Contract |
| --- | --- | --- |
| `struct_size` | Exact structure size | Required exact match. |
| `schedulers` | 1 | Normal capacity, 1–1024. |
| `schedulers_online` | 1 | 1 through normal capacity. |
| `dirty_cpu_schedulers` | 1 | 1 through normal capacity. |
| `dirty_cpu_schedulers_online` | 1 | At least 1, no greater than dirty CPU capacity or normal online count. |
| `dirty_io_schedulers` | 1 | 1–1024. |
| `async_threads` | 1 | 0–1024; unlike scheduler/poll counts, zero is allowed. |
| `poll_threads` | 1 | 1–1024. |
| `process_limit` | 1048576 | 1024–134217727; native capacity rounds up to a power of two, capped at the maximum usable size. |
| `atom_limit` | 1048576 | 8192–2147483647. |
| `min_heap_size` | 233 words | Positive per-process minimum, rounded to a supported heap size fitting `INT_MAX`. |
| `min_bin_vheap_size` | 32768 words | Positive binary virtual-heap threshold, independently rounded. |
| `max_heap_size` | 0 | Per-process maximum; zero disables it, otherwise at least rounded minimum heap and at most the native small-integer maximum. |
| `composition` | NULL | Immutable preparation retained by successful initialization. |
| `execution` | `BEAM_HOST_CONTROLLED` | Alternatively `BEAM_PROCESS_OWNED`. |
| `initial_root` | NULL | Required only for process-owned mode; copied `beam_entry` with module/function and ETF proper-list arguments. |
| `startup_context` | Empty | Optional copied uncompressed ETF term; empty means `undefined`. |
| `process_policy` | 0 | Explicit process-owned signal/stdio/diagnostic bits. |
| `program_name` | NULL | Copied diagnostic identity; NULL uses `libbeam`. |
| `allocator_tagging` | `BEAM_ALLOCATOR_TAGGING_DEFAULT` | Preserve mixed allocator defaults, or explicitly select `BEAM_ALLOCATOR_TAGGING_ENABLED` / `BEAM_ALLOCATOR_TAGGING_DISABLED`. Invalid values fail before VM initialization. |
| `port_limit` | Native default | At most `INT_MAX`; native tables round/clamp requested capacity. |
| `port_limit_ignore_files` | 0 | Port capacity policy flag. |
| `ets_limit` | 0 | ETS sizing hint. |
| `fullsweep_after` | 65535 | Default generational-GC count, 0–65535. |
| `filename_encoding` | `BEAM_FILENAME_UTF8` | Alternatively `BEAM_FILENAME_LATIN1`. |
| `break_policy` | `BEAM_BREAK_DEFAULT` | Process-owned interrupt policy below. |

Counts describe configured roles, not every OS helper thread. Schedulers start
unbound; CPU binding is not public configuration. No automatic scaling to CPU
availability occurs. Process/atom limits include runtime objects and reserve
no guaranteed memory. Heap words are eight bytes on supported 64-bit builds;
32768 rounds to 46422, while 233 is already supported. The largest accepted
minimum is 2115685508 words; the maximum heap upper bound is 576460752303423487.
The binary threshold is accounting, not an eagerly allocated buffer. Maximum
heap need not exceed that threshold and is not a VM-wide memory budget.
These bounds are implementation limits, not future ABI promises.

Allocator tagging applies to binary, standard, ETS, process-heap, long-lived,
driver, fixed-size and short-lived allocators. Temporary, literal and test
allocators keep their defaults. Selection belongs to the VM lifetime; a fresh
VM may choose differently, while OTP `init:restart()` retains the selection.
There is no environment parsing or per-allocator tuning in the public API.
Consumers must rebuild for the matching SDK; options use an exact structure
size check.

Embedding ignores emulator-tuning environment variables. Allocator strategies,
reader/counter groups and internal thread coordination remain derived settings.
OTP derives filename policy from its host locale without changing locale during
VM initialization. There is no runtime-family field or system family lock.

### Process policy and context

`startup_context` must encode exactly one uncompressed ETF term with its version
byte, without trailing data. Initialization validates/copies it before execution.
`erts_internal:vm_context/0` decodes a fresh value per call; immutable bytes survive
framework restart. No context yields `undefined`.

`BEAM_PROCESS_SIGNALS`, `BEAM_PROCESS_STDIO` and `BEAM_PROCESS_DIAGNOSTICS`
require process-owned execution. Effects begin at explicit entry, not initialization.
The copied break policy selects:

| Policy | Effect with process signal ownership |
| --- | --- |
| `BEAM_BREAK_DEFAULT` | Install the default break handler. |
| `BEAM_BREAK_DISABLED` | Leave the break handler uninstalled. |
| `BEAM_BREAK_IGNORE` | Ignore terminal interrupt, quit and suspend signals. |
| `BEAM_BREAK_INPUT` | Keep the handler but make the interrupt character available as terminal input; also requires STDIO ownership. |

Every non-default break policy requires SIGNALS. Normal process exit restores
managed terminal settings. Process-wide policy and native admission do not add
returning teardown of OTP.

## Composition

`beam_composition_prepare(&options, &composition, &error)` copies supplied code,
names, origins and native registrations. It creates no VM, maps no image and
runs no native initializer. The output must initially be NULL and `struct_size`
must match exactly. Failure leaves the output unchanged. Structural validation
happens during preparation; the VM loader performs complete bytecode validation.
Worker destinations are validated during initialization and before workers start.
Neither preparation nor initialization runs Erlang or acquires libraries. These
selections survive framework restart and are cleared with the VM lifetime.

| Composition field | Contract |
| --- | --- |
| `module_count`, `modules` | Up to 4096 ordinary `beam_module_spec` inputs. |
| `defer_modules` | Retain ordinary bytes for the host to load explicitly before use. |
| `preload_count`, `preloads` | Up to 4096 additional preloads, staged even when ordinary modules are deferred. |
| `builtin_nif_count`, `builtin_nifs` | Up to 1024 resident NIF registrations. Table access occurs at VM initialization; load callbacks occur at the selected native `load_nif/2` binding. |
| `builtin_driver_count`, `builtin_drivers` | Up to 1024 resident driver registrations; requires driver admission. |
| `binding_count`, `bindings` | Up to 1024 explicit `beam_nif_binding` declarations, bound individually after start. |
| `native_owner_count`, `native_owners` | Opaque retain/release records keeping native pointers valid through reclamation. |
| `program_helper` | Copied absolute executable path for admitted ports/programs; preparation does not launch it. |
| `native_admission` | Explicit NIF, driver, port, distribution or managed-port permissions. |
| `builtin_binding_count`, `builtin_bindings` | Up to 1024 optional native implementation bindings, described below. Zero binds none, including when no composition is supplied. |
| `default_tracer_module` | Copied UTF-8 module identity for PID/port tracer delivery. NULL selects no default backend; a name neither loads nor activates code. Explicit `{Module, State}` tracers remain independent. |
| `system_worker_entry_count`, `system_worker_entries` | Finite immutable worker-entry table; currently only `BEAM_SYSTEM_WORKER_TRACE_CLEANER`. The default is `beam_internal:trace_cleaner_start/0`. An override must name an exported zero-argument function in a supplied preload. |
| `core_export_count`, `core_exports` | Explicitly select declared ordinary core exports; zero selects none. Native core slots are selected through matching native bindings. |

`beam_module_spec` contains `name`, complete `bytes` and optional UTF-8 `origin`.
NULL origin becomes `memory`. Names contain at most 255 Unicode characters;
origins are diagnostic labels, never acquisition paths. Duplicate names across
ordinary modules and preloads, conflicts with the six core modules, and
conflicting resident/explicit native registrations are rejected. Generic
composition requires no product support modules or named facilities.

### Optional native BIF bindings

`BEAM_BUILTIN_BINDING_API_VERSION` is `1`. A `beam_builtin_binding` contains
`struct_size`, `implementation`, `module`, `function` and `arity`. Implementation
identifiers select compiled calling contracts; they expose neither internal BIF
indices nor executable pointers. Identifiers are experimental inputs to an exact
SDK. Each contract fixes arity, argument interpretation, BIF classification and
scheduling/continuation behavior. All native callable contracts are selectable,
including heavy, guard/operator, dirty and instruction-backed entries. Language
guard eligibility is recorded separately from calling class. The
[callable catalog](native-contracts.md) lists IDs and execution traits.

Preparation copies each identifier and both public names without creating atoms.
Module/function names must be nonempty valid UTF-8 with at most 255 Unicode
characters. Unknown implementations, wrong arities, repeated implementations,
duplicate destination MFAs and unauthorized core destinations are rejected with
`BEAM_INVALID_ARGUMENT`. Core-module namespaces are reserved except for declared native slots paired
with their exact implementation descriptor. Validation
failure preserves the caller's output pointer.

Bindings are installed after atom initialization, before preloads and ordinary
code. They remain fixed through reload and framework restart, until VM
termination. A later fresh VM can select different names. Every loaded module
must export its installed bound BIFs, including on reload. Binding a name does
not acquire module code, execute initialization or grant native permissions.
The ordinary loader and BIF dispatch resolve the binding; calls perform no
additional implementation-name lookup. An unbound descriptor creates no public
export and contributes no MFA to `system_info(snifs)`.

Embedded core native stub exports are optional core slots. They
are published only when their matching implementation is bound to that canonical
MFA. Selecting another destination leaves the canonical export absent. The
loader filters publication, retaining the compiled bodies; live export queries
report the selected surface. Ordinary module loading retains its existing
requirement to declare installed bound native exports.

`beam_core_export` contains `struct_size`, `module`, `function` and `arity`.
`beam_composition_options.core_export_count/core_exports` select declared
ordinary core exports independently of native bindings. The only current slot
is `erlang:convert_time_unit/3`. Zero selects none. Unknown slots, duplicates,
incorrect record sizes and malformed pointer/count pairs are rejected with
`BEAM_INVALID_ARGUMENT`; preparation copies names without creating atoms and
preserves caller outputs on failure. Selection remains fixed across framework
restart and lasts until VM termination. This exact-SDK preview interface does
not allow replacing core code or exporting arbitrary private functions.

Canonical hosts select all clock/timer native descriptors at their `erlang`
MFAs, legacy `now/0`, and the ordinary converter. Modern hosts bind the 15 modern
operations under `beam_time` and select no canonical time exports. The VM's
private conversion continuation remains available without any public time
selection. No family selector or execution-time registry lookup is involved.

The optional identifiers are:

| Implementation identifier | Calling contract |
| --- | --- |
| `clock_monotonic_0` | Existing `erlang:monotonic_time/0` semantics. |
| `clock_monotonic_1` | Existing `erlang:monotonic_time/1` semantics. |
| `clock_system_0` | Existing `erlang:system_time/0` semantics. |
| `clock_system_1` | Existing `erlang:system_time/1` semantics. |
| `clock_offset_0` | Existing `erlang:time_offset/0` semantics. |
| `clock_offset_1` | Existing `erlang:time_offset/1` semantics. |
| `clock_timestamp_0` | Existing `erlang:timestamp/0` semantics. |
| `timer_start_3` | Existing `erlang:start_timer/3` semantics. |
| `timer_start_4` | Existing `erlang:start_timer/4` semantics. |
| `timer_send_after_3` | Existing `erlang:send_after/3` semantics. |
| `timer_send_after_4` | Existing `erlang:send_after/4` semantics. |
| `timer_cancel_1` | Existing `erlang:cancel_timer/1` semantics. |
| `timer_cancel_2` | Existing `erlang:cancel_timer/2` semantics. |
| `timer_read_1` | Existing `erlang:read_timer/1` semantics. |
| `timer_read_2` | Existing `erlang:read_timer/2` semantics. |
| `clock_legacy_now_0` | Existing `erlang:now/0` semantics; canonical compatibility only, no modern `now/0`. |
| `bytes_compile_pattern_1` | Existing `binary:compile_pattern/1` semantics. |
| `bytes_match_list_2` | Existing `binary:match/2` semantics. |
| `bytes_match_list_3` | Existing `binary:match/3` semantics. |
| `bytes_find_all_2` | Existing `binary:matches/2` semantics. |
| `bytes_matches_list_3` | Existing `binary:matches/3` semantics. |
| `bytes_common_prefix_size_1` | Existing `binary:longest_common_prefix/1` semantics. |
| `bytes_common_suffix_size_1` | Existing `binary:longest_common_suffix/1` semantics. |
| `bytes_first_1` | Existing `binary:first/1` semantics. |
| `bytes_last_1` | Existing `binary:last/1` semantics. |
| `bytes_at_2` | Existing `binary:at/2` semantics. |
| `bytes_part_2` | Existing `binary:part/2` semantics. |
| `bytes_part_3` | Existing `binary:part/3` semantics. |
| `bytes_from_list_1` | Existing `binary:list_to_bin/1` semantics. |
| `bytes_copy_1` | Existing `binary:copy/1` semantics. |
| `bytes_copy_2` | Existing `binary:copy/2` semantics. |
| `bitstring_referenced_size_1` | Existing `binary:referenced_byte_size/1` semantics. |
| `bytes_encode_unsigned_1` | Existing `binary:encode_unsigned/1` semantics. |
| `bytes_encode_unsigned_2` | Existing `binary:encode_unsigned/2` semantics. |
| `bytes_decode_unsigned_1` | Existing `binary:decode_unsigned/1` semantics. |
| `bytes_decode_unsigned_2` | Existing `binary:decode_unsigned/2` semantics. |
| `bytes_split_2` | Default split options. |
| `bytes_split_list_3` | OTP list split options. |
| `bytes_split_map_3` | Modern map split options. |
| `bytes_repeat_modern_2` | Repeat a binary; preserve empty/zero shortcuts and `system_limit` for unrepresentable nonempty counts. |
| `bytes_referenced_size_modern_1` | Referenced backing size, restricted to binaries. |
| `bytes_find_modern_2` | First range as `{ok, Range}`, or `not_found`. |
| `bytes_find_modern_3` | First range with strict map scope options. |
| `bytes_find_all_modern_3` | Nonoverlapping range list with strict map scope options. |

There are 573 implementation descriptors. Identical contracts share one identifier;
separate distributions choose different destinations. A descriptor has one MFA per
VM and cannot expose both facade spellings simultaneously. `bytes_split_2` is
shared by default splitting; `bytes_find_all_2` is shared by unscoped range-list
search. List/map options for `/3`, tagged search results, repeat and binary-only
referenced size remain distinct contracts. Pattern representation and kernels
are shared. The unselected experimental search variant `/3` accepts only `#{}` or exactly
`#{scope => {Offset, Count}}` with nonnegative in-range bounds; its result policy
survives native continuations.

Modern hosts select the 51 canonical binary, list, map, record, string and
Unicode contracts under the corresponding `beam_` module names. Canonical
compiler and OTP hosts select their upstream names. These are separate host
compositions, not simultaneous aliases. Ordinary algorithms require their
explicitly supplied module bytes.

The distinct experimental byte/list/map/text variant contracts remain available
for exact-SDK consumers, but no default composition selects the superseded
facade names. Their argument/result/error policies remain distinct from the
canonical contracts and must not be substituted by identifier spelling.
`list_reverse_2` retains upstream resumed-argument reporting on late errors;
`list_reverse_modern_2` retains its original-argument policy.

| Module | Native operations |
| --- | --- |
| `beam_binary` | `compile_pattern/1`, `match/2`, `match/3`, `matches/2`, `matches/3`, `longest_common_prefix/1`, `longest_common_suffix/1`, `first/1`, `last/1`, `at/2`, `part/2`, `part/3`, `list_to_bin/1`, `copy/1`, `copy/2`, `referenced_byte_size/1`, `encode_unsigned/1`, `encode_unsigned/2`, `decode_unsigned/1`, `decode_unsigned/2`, `split/2`, `split/3` |
| `beam_lists` | `member/2`, `reverse/2`, `keymember/3`, `keysearch/3`, `keyfind/3` |
| `beam_maps` | `find/2`, `get/2`, `from_list/1`, `is_key/2`, `keys/1`, `merge/2`, `put/3`, `remove/2`, `update/3`, `values/1`, `take/2`, `from_keys/2` |
| `beam_records` | `get_module/1`, `get_name/1`, `get/2`, `get_field_names/1`, `is_exported/1`, `create/4`, `update/4`, `get_definition/2` |
| `beam_string` | `list_to_float/1` |
| `beam_unicode` | `characters_to_binary/2`, `characters_to_list/2`, `bin_is_7bit/1` |

Array and persistent-store contracts retain the existing opaque resource
representations and one VM-wide persistent keyspace. The following identifiers
are independent slots; hosts may choose other module and function names.

| Implementation identifier | Product host binding |
| --- | --- |
| `atomic_array_get_2` | `atomics:get/2` / `beam_atomics:get/2` |
| `atomic_array_put_3` | `atomics:put/3` / `beam_atomics:put/3` |
| `atomic_array_add_3` | `atomics:add/3` / `beam_atomics:add/3` |
| `atomic_array_add_get_3` | `atomics:add_get/3` / `beam_atomics:add_get/3` |
| `atomic_array_exchange_3` | `atomics:exchange/3` / `beam_atomics:exchange/3` |
| `atomic_array_compare_exchange_4` | `atomics:compare_exchange/4` / `beam_atomics:compare_exchange/4` |
| `atomic_array_info_1` | `atomics:info/1` / `beam_atomics:info/1` |
| `persistent_value_put_2` | `persistent_term:put/2` / `beam_persistent_term:put/2` |
| `persistent_value_put_new_2` | `persistent_term:put_new/2` / `beam_persistent_term:put_new/2` |
| `persistent_value_get_0` | `persistent_term:get/0` / `beam_persistent_term:get/0` |
| `persistent_value_get_1` | `persistent_term:get/1` / `beam_persistent_term:get/1` |
| `persistent_value_get_2` | `persistent_term:get/2` / `beam_persistent_term:get/2` |
| `persistent_value_erase_1` | `persistent_term:erase/1` / `beam_persistent_term:erase/1` |
| `persistent_value_info_0` | `persistent_term:info/0` / `beam_persistent_term:info/0` |
| `atomic_array_new_list_modern_2` | `beam_atomics:new/2` |
| `counter_array_new_list_modern_2` | `beam_counters:new/2` |
| `counter_array_get_modern_2` | `beam_counters:get/2` |
| `counter_array_put_modern_3` | `beam_counters:put/3` |
| `counter_array_add_modern_3` | `beam_counters:add/3` |
| `counter_array_info_modern_1` | `beam_counters:info/1` |

The modern library recipe selects 558 bindings; OTP and canonical compiler
tooling select 556. Modern command profiles use that same selection, without an
additional private support binding list. Bare hosts still choose explicit
subsets or zero bindings. Remaining `erlang` primitives bind under `beam`.
The [callable catalog](native-contracts.md) records implementation contracts.
Shared descriptors have one destination each; independent modern and canonical
profiles select their respective MFAs. OTP's `atomics:new/2`, subtraction helpers and all
six `counters` functions remain ordinary Erlang functions. The six VM-private
array mechanisms retain their existing contracts; their `erts_internal` callable
exports require explicit native bindings. Modern native
counter operations call the typed kernels directly and need no public `atomics`
binding. Modern subtraction helpers are ordinary Erlang and require their own
module's addition binding.

ETS contributes 51 shared contracts. Each identifier is
`term_table_<function>_<arity>`, except `new/2`, whose identifier is
`term_table_new_list_2`. Modern recipes bind them under `beam_ets`; canonical
hosts bind them under `ets`. Hosts may choose other module and function names.

| Functions | Arities |
| --- | --- |
| `new` | 2 (identifier `term_table_new_list_2`) |
| `delete`, `lookup_element`, `info`, `match`, `match_object`, `select`, `select_reverse`, `update_counter`, `update_element` | `delete/1,2`, `lookup_element/3,4`, `info/1,2`, `match/1,2,3`, `match_object/1,2,3`, `select/1,2,3`, `select_reverse/1,2,3`, `update_counter/3,4`, `update_element/3,4` |
| `first`, `first_lookup`, `last`, `last_lookup`, `is_compiled_ms`, `match_spec_compile`, `whereis` | 1 |
| `delete_object`, `lookup`, `member`, `next`, `next_lookup`, `prev`, `prev_lookup`, `insert`, `insert_new`, `rename`, `safe_fixtable`, `slot`, `select_count`, `select_replace`, `setopts`, `take` | 2 |
| `give_away` | 3 |
| `internal_request_all`, `internal_delete_all`, `internal_select_delete`, `match_spec_run_r` | 0, 2, 2, 3 respectively |

The last four entries are implementation protocols, not additional public API
promises. They retain their existing replies, arguments and ordinary-wrapper
classification. Table IDs, compiled match programs, continuation tuples,
`'$end_of_table'` and `'ETS-TRANSFER'` messages are unchanged. A bound `select/1`
is the ordinary ordered-table resume target; without it, a VM-private resume
retains the initiating binding and arguments. `info(Table,binary)` needs no
optional `safe_fixtable/2` binding and retains diagnostic access to private tables.
Tracing's matcher and the private ETS mechanisms execute independently of
public exports. Callable `match_spec_test/3` and `erts_internal:ets_info_binary/1`
exports require their explicit bindings. ETS accounting follows the native implementation and
retains existing allocator and diagnostic category labels.

Installed optional native exports are callable through compiled calls, dynamic
calls and external funs before ordinary module code is supplied. Loading a module
still requires all its installed optional BIFs in the export list. Native wrappers
remain VM-owned across ordinary code unloading. Declared native provider slots
are filtered before publication when no native binding selects them. Ordinary
functions and retained compiled bodies keep their own loading behavior.

Array constructors validate option lists before size. Atomics default to signed;
repeated `{signed, Boolean}` options use the last value. Counters accept exactly
`[]`, `[atomics]` or `[write_concurrency]`. Indexing is one-based and arithmetic
wraps at 64 bits. Atomic counter `info/1` includes `min` and `max`; the striped
backend reports `size` and `memory`. Handles interoperate across the APIs within
one VM. Subtraction negates before reference validation, so a nonnumeric
decrement raises `badarith` with the original public arguments.

Persistent `put_new/2` succeeds for an identical existing value and raises
`badarg` for a differing value. Contention and native continuations retain the
bound identity, original arguments and result policy. These bindings do not add
an execution-time registry lookup. Unknown public names retain generic extended
cause metadata; canonical OTP names keep their established error formatter.

Modern map arguments are map-first and invalid maps raise `badarg`; OTP map
contracts retain their key-first arguments and `badmap` errors. Modern lookup
returns `not_found` on a miss. List membership uses exact equality and stops at
its first match; an invalid tail raises `badarg` only when reached. Reversal
requires a proper first list and accepts an arbitrary tail term.

Unicode conversion accepts `latin1`, `unicode`, `utf8`, `utf16`, `utf32`, and
`{utf16,big|little}` / `{utf32,big|little}`. Integers in lists are codepoints.
Conversion preserves nested error remainders and incomplete sequences without
requiring ordinary `unicode` code. The modern UTF-8 validator accepts only byte
binaries, returns the original binary on success, and reports invalid or
incomplete text as `{error, #{kind => invalid_data}}`.

Canonical library exports can also be selected by modern hosts for their
ordinary support modules. They are callable exports once installed. Core map
instructions and guards remain mandatory. Compiler rewrites of canonical map
calls can use these core operations even when the corresponding optional export
is absent; dynamic calls and external funs observe export selection directly.

Implementation diagnostics may identify an unbound descriptor by its identifier.
Extended microstate accounting preserves established bound-name labels where
those labels fit an atom and are unique, and uses the implementation label
for long or colliding custom names.

### Native registrations

`beam_builtin_nif` and `beam_builtin_driver` carry exact sizes, names, SDK build
identities, typed table accessors and taint flags. Their code must remain resident
through native-resource destruction, unload callbacks and registry cleanup.
Executable-resident registrations may use a borrowed lifetime; acquired code
supplies a `beam_native_owner` with context and retain/release functions.
Preparation retains owners before storing native pointers. A VM retains its
preparation, allowing the caller to release its own preparation reference early.

`beam_nif_binding` selects a module, exact SDK identity and either a normalized
resident accessor (`initialize`) or a platform initializer (`native_initialize`).
Exactly one initializer is supplied. The platform form preserves Windows callback
table injection. After start, `beam_vm_bind_nif(vm, module, &error)` invokes that
initializer and binds the declared module with `undefined` load information.
Successful repeated binding is idempotent. Invalid arguments, an undeclared
binding or missing code leave state unchanged. Failure after native initialization
begins stops the VM, retains diagnostics and requires termination before retry.
Ordinary reload/rebinding is unsupported for this route.

`beam_composition_get_modules` borrows the copied initial inventory;
`beam_composition_release` drops the caller's reference and clears its handle.
Preloads remain distinct from ordinary modules and retain their preload semantics.
See [native loading](../native-extensions/loading.md) for initializer timing,
image admission and the reviewed deferred-module policy.

### Optional libraries and startup

Include `<beam/library.h>` and link `BEAM::Host` for passive shared/static library
acquisition. Descriptors carry modules and resident registrations, with no startup
hooks, fixed kinds or dependency discovery. See [the library format](../native-extensions/libraries.md).

`<beam/host.h>` supplies optional orchestration:

| Operation | Contract |
| --- | --- |
| `beam_host_prepare` | Copy a `beam_host_options` plan and merge its generic composition, library inputs, explicit startup actions and deferred modules. Starts nothing. |
| `beam_host_composition` | Borrow the prepared generic composition until Host release. VM initialization independently retains it. |
| `beam_host_start` | On an already started VM, run the requested startup calls and complete explicit bindings. Failure stops the VM. Call once per VM lifetime. |
| `beam_host_release` | Release copied policy and the caller's composition reference. |
| `beam_host_open_nif` | Admit/map an explicit NIF image and obtain its typed binding and owner, without calling its initializer. |

A `beam_startup_action` supplies module/function names and an ETF argument list;
an empty span means `[]`. Startup actions must return `ok`. Actions and reviewed
deferred loading require explicitly supplied `beam_startup` bytecode. No unrelated
output or acquisition module is required. Library descriptors never choose these
actions. Raw embedders may implement the same ordering with public VM calls.

## System and VM lifecycle

| Operation | Contract |
| --- | --- |
| `beam_init(&system, &error)` | Main-thread system initialization, no workers/Erlang. Output unchanged on returned error. A successfully claimed system cannot be initialized again. |
| `beam_vm_init(system, options, &vm, &error)` | Copy options and construct initialized native/code state without workers/Erlang. Output unchanged on returned error. Existing VM handle returns `BEAM_ALREADY_STARTED`. |
| `beam_vm_start(vm, &error)` | Publish code and start workers. A process-owned root remains suspended for synchronous setup. Starting twice or after stop is invalid. |
| `beam_vm_enter(vm, &error)` | Commit a prepared process-owned VM, or start and enter an initialized one. Once committed it never returns. |
| `beam_vm_stop(vm, &halt_status)` | Close admission, cancel pending requests, terminate processes, drain cleanup and join workers. Cooperative, idempotent; also stops an unstarted VM. |
| `beam_vm_terminate(&vm, &halt_status)` | Stop if needed, reclaim VM resources and clear handle, permitting fresh initialization. |
| `beam_terminate(&system)` | Release system infrastructure only after its VM handle is gone, then clear handle. |

Initialization loads the [six core modules](guide.md#choose-a-composed-lifecycle),
plus explicit composition code. It acquires no boot script, application tree,
compiler, automatic loader or I/O server. Unconfigured bare VMs reject ordinary
NIF/driver loading, ports, distribution and direct debug output (`display`,
`display_string`, `process_display`); explicit facilities follow composition rules.
No default logger or host-stderr fallback is installed.

VM initialization validation permits a corrected attempt. Native construction
can still fail fatally. Recoverable allocation/synchronization errors return
OUT_OF_MEMORY/SYSTEM_ERROR. Incompatible host fault handlers or exhausted
system-local creation numbers return UNSUPPORTED. Each successful VM gets a
distinct creation number so old copied PID/reference identities cannot alias
new local identities; exhaustion never wraps.

Stop retains storage and an observable handle, which still occupies the system's
VM slot. Successful termination invalidates aliases. NULL pointer-to-handle
arguments are invalid; NULL handle values permit repeated cleanup, with VM halt
status zero. Returned cleanup failure retains the handle for inspection/retry.
System termination with any VM handle returns `BEAM_INVALID_STATE`; infrastructure
cleanup failure returns `BEAM_SYSTEM_ERROR` without clearing the handle.

Fresh VMs inherit no prior modules, dynamic atoms, processes, timers, callbacks
or halt state. A process-owned root uses the same initialization mechanisms as
host-controlled execution. It is the first system process, suspended until
startup finishes. Before entry commit, synchronous calls and explicit native
binding are permitted; ordinary process-lifetime native permissions remain
denied. Asynchronous call/pump extensions are host-controlled only. Entry rejects
pending work and unfinished bindings. Process policy is applied at commit on
the owner thread, with failure arbitration synchronized against entry. Returned
validation/startup errors retain an inspectable handle; after admission halt
exits the OS process and root failure is fatal.
Host callbacks run on the owner thread inside `beam_vm_enter`.

### States and permitted operations

`beam_vm_get_state(vm, &info)` copies `state` and `halt_status`, without Erlang
execution, callbacks or joining. Output remains unchanged on error. Zero status
means no accepted halt or a host-requested stop; the first accepted halt status
is retained. Termination leaves no queryable handle.

| State | Meaning |
| --- | --- |
| `BEAM_VM_INITIALIZED` | Native state/code exists; workers have not started. |
| `BEAM_VM_RUNNING` | Workers started; no terminal halt accepted. |
| `BEAM_VM_HALT_REQUESTED` | Halt accepted; owner shutdown/join incomplete, even if workers quiesced. |
| `BEAM_VM_STOPPED` | Workers joined; handle/storage retained. |

| Operation | Initialized | Running | Halt requested / stopped |
| --- | --- | --- | --- |
| State/scheduler-info/report query | Observe | Observe | Observe only. |
| Scheduler inventory/process list/process inspection | `BEAM_INVALID_STATE` | Completed copied result | `BEAM_STOPPED`; no implicit join. |
| Process spawn | `BEAM_INVALID_STATE` | Publish child | `BEAM_STOPPED` before publication. |
| Module query/list/unload | Native operation | Native operation | `BEAM_STOPPED`. |
| Load | Load | Load | `BEAM_STOPPED`; halted load completes joining. |
| Call | `BEAM_INVALID_STATE` | Execute after readiness | `BEAM_STOPPED`; halted call completes joining. |
| Scheduler online change | `BEAM_INVALID_STATE` | Change pair | `BEAM_STOPPED`. |
| Handler registration | Register/clear | Until first admitted call | `BEAM_STOPPED`. |
| Start | Start workers, suspending a process-owned root | `BEAM_INVALID_STATE` | `BEAM_INVALID_STATE`. |
| Stop | Stop without starting | Stop/join | Finish join or idempotent success. |
| Terminate | Release | Stop/release | Finish cleanup/release. |

The table assumes valid arguments on the owner thread outside callbacks.
Background activity can change state between observation and admission.

### Reports and host state

`beam_vm_get_report(vm, &report)` observes an exact-size copied `beam_vm_report`
without execution/joining, including after stop. `cause` is NONE, HALT,
FAILURE or HOST_REQUEST with prefix `BEAM_TERMINAL_`; `halt_status`,
`origin` and `failure` describe the winning terminal cause.
`has_cleanup_error`/`cleanup` remain separate. The origin string is an identity,
not text parsed from a diagnostic. Output is unchanged on error.

Native callbacks and library monitors can call `beam_native_request_failure`
with copied origin/reason text. It requests coordinated shutdown without waiting
for host callbacks. `beam_native_report_cleanup` records the first cleanup
diagnostic separately; `beam_native_stop_requested` remains usable during
quiescent resource destruction. These operations are declared in
`<beam/native.h>` and do not authorize calls from threads outliving their VM.

`beam_vm_terminate_report(&vm, &status, &report)` has termination semantics and
optionally captures the final report after native/code reclamation. Reports
survive VM/system termination. Cleanup failure retains the VM for retry. A
previously copied call result remains valid even if a later terminal event stops
the VM; the report records that terminal event separately.

Host-controlled lifetimes preserve signal dispositions, standard descriptors,
terminal, environment and floating-point state. VM workers block asynchronous
host signals and own alternate stacks. Native-stack JIT requires custom handlers
for synchronous faults (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGTRAP`,
`SIGSYS` where present) to use `SA_ONSTACK`. Initialization and start reject
incompatible handlers rather than rewriting them; maintain the policy while live.
Explicit process-owned policy is the separate route for process-wide effects.

## Term environments

Include `<beam/beam_enif.h>` and link `BEAM::Runtime` or `BEAM::RuntimeStatic`.
The header declares the host subset of the [NIF API](../native-extensions/nif.md).
Each `beam_enif_*` function has the corresponding `enif_*` operation's argument,
return, failure, and ownership rules. `BeamEnv`, `BeamTerm`, and the other
BEAM types in this header are aliases of the native types and use the existing
NIF flags and enum values.

`beam_enif_alloc_env()` creates a caller-owned, process-independent `BeamEnv`.
Call these functions on the VM owner thread outside VM callbacks after
successful `beam_vm_init()`. An unstarted VM supports ordinary term operations.
Use terms and identities from the current VM and follow the input requirements
of each operation, including finite doubles and valid text for constructors.

`beam_enif_clear_env(env)` releases the terms and retains the environment for
reuse.
`beam_enif_free_env(env)` releases the terms and the environment. Both require a
valid, non-NULL, owned environment. Terms become invalid when the environment
is cleared or freed. Free all host-owned environments before
`beam_vm_terminate()`. An empty environment or one holding ordinary terms can
be cleared or freed after `beam_vm_stop()` or a call that reports `BEAM_HALT`.

Host-owned environments may hold ordinary terms. Releasing NIF resource
references can require scheduler work, so those references are outside the
host environment contract. Destroy map iterators before clearing their source
environment. Release owned buffers and binaries before VM termination. Treat
borrowed views as valid only for their source lifetime.

`beam_enif_binary_to_term()` accepts caller-trusted ordinary ETF under the
native decoder's input contract. Exclude VM-local encodings from its input.
`ERL_NIF_BIN2TERM_SAFE` restricts native decoding but still permits VM-local
encodings. `beam_vm_call()` independently rejects those encodings at its
public call boundary.

Registered-name lookup, liveness queries, and sending require a started VM.
Pass `NULL` for the caller environment in `beam_enif_whereis_pid()`,
`beam_enif_whereis_port()`, and `beam_enif_send()` from the host thread.
Send to a live local PID. A successful send with a supplied message environment
invalidates its terms. Clear the environment for reuse or free it. A failed
send leaves the message and its environment valid.

`beam_enif_make_pid(env, &pid)` constructs the native PID term in `env`.
`beam_enif_compare_pids(&left, &right)` returns a negative value, zero or a
positive value according to native PID ordering. These helpers preserve the
corresponding NIF semantics. Use valid identities from the current VM; neither
construction nor comparison keeps a process alive. Use the undefined-PID helpers
for absence rather than inspecting native representation or structure padding.

## Loading, inspection and scheduler control

`beam_module_load(vm, module)` accepts the complete unpacked BEAM container from
copied memory. Data must be non-NULL, nonempty and representable by the native
byte-count type. Loading before or after start executes no Erlang and acquires
no dependencies; existing background execution can continue independently.
Malformed/incompatible code returns `BEAM_INVALID_BEAM`, `on_load` returns
`BEAM_UNSUPPORTED`, and existing code returns `BEAM_ALREADY_LOADED`. Rejection
leaves existing code callable. Use `beam_module_replace` for explicit hot code
publication; `beam_module_load` retains its first-load-only policy.

`beam_module_get_info(vm, name, &info)` fills exact-size `beam_module_info.flags`.
CURRENT, OLD, ON_LOAD and PROTECTED flags, prefixed `BEAM_MODULE_`, can combine.
An unknown valid name succeeds with zero flags and creates no atom. Names are
nonempty NUL-terminated UTF-8 of at most 255 Unicode characters. Output is
unchanged on errors; flags are observations, not code-retaining references.

`beam_module_list_names(vm, &list)` copies unique names having current, old or
pending code, sorted by UTF-8 byte order. Atoms without code are not listed.
Native module names that are empty or contain embedded NUL return
`BEAM_UNSUPPORTED` rather than being truncated. Initialize the list to `{0}`
and release an earlier snapshot before reuse. Output is unchanged on failure;
allocation failure returns `BEAM_OUT_OF_MEMORY`. `beam_module_list_release`
accepts NULL/empty values, frees the snapshot and clears it. Do not free or
modify individual names. The snapshot survives code changes and VM/system
termination while the library remains loaded.

Get-info and name-list observation remain available while an asynchronous entry
occupies its separate application slot. They share inspection's nonwaiting
code-permission policy and do not pump callbacks or join workers.

### Detailed module snapshots

`beam_module_inspect(vm, name, &query, &snapshot)` captures one module namespace;
`beam_module_inspect_all(vm, &query, &snapshot)` captures the selected inventory.
Both return a completed, independently owned `beam_module_snapshot`. A targeted
query returns `BEAM_NOT_FOUND` when its name is outside the selected scopes;
an empty inventory succeeds. These functions require no Host archive, ordinary
library inventory, code server or application helper.
Detailed inspection supports host-controlled VMs; process-owned execution
returns `BEAM_UNSUPPORTED`.

Initialize `beam_module_query` with `beam_module_query_init`. Set `struct_size`
exactly. Its default is `BEAM_MODULE_SCOPE_LOADED` with zero detail fields.
`scopes` selects a nonempty union of these memberships, without duplicate names:

| Scope | Membership |
| --- | --- |
| `BEAM_MODULE_SCOPE_LOADED` | Current, old or pending BEAM code. |
| `BEAM_MODULE_SCOPE_CALLABLE` | At least one effective externally callable MFA, including native-only destinations. |

Unknown scope/field bits or an empty scope selection return
`BEAM_INVALID_ARGUMENT`. Names in the new inspection, delete and soft-purge
functions use `beam_module_name`: UTF-8 bytes with authoritative byte length,
including empty and embedded-NUL atom identities. Lookup never creates an atom.
The data pointer must be non-NULL even for an empty name, and names retain the
255-Unicode-character atom limit.
The older get-info, names and unload interfaces retain their C-string rules.
Inputs remain valid until return and are not retained afterward.

A snapshot contains `count` read-only `beam_module_record` values in `items`,
sorted by name bytes and then length. Each record contains its copied name,
presence/protection flags and a summary for each present `current`, `old` and
`pending` code slot. Check `BEAM_MODULE_CURRENT`, `BEAM_MODULE_OLD` and
`BEAM_MODULE_ON_LOAD` before reading those slots; absent slots are zeroed.
Pending describes retained provisional `on_load` code, not a historical property
of successfully published code. Protection follows the six runtime core modules.

Each present `beam_module_code_info` contains these actual runtime observations:

| Member | Meaning |
| --- | --- |
| `loaded_code_bytes` | The instance's loaded-code accounting value; interpreter/JIT representations differ, and literals are separate. |
| `function_table_count` | Internal function-table entries, including unnamed stubs omitted from named functions. |
| `has_nif`, `has_line_table` | Actual attached NIF and retained line-table presence. |
| `md5[16]` | Existing module checksum over selected BEAM chunks; excludes attributes/compile information and is neither provenance nor a unique load identity. |

`query.fields` selects optional copied details. On success each record's
`fields` equals the requested mask, including when a requested group is empty:

| Field | Result |
| --- | --- |
| `BEAM_MODULE_FIELD_EXPORTS` | Parent record's effective namespace `exports`, each a function name and arity. |
| `BEAM_MODULE_FIELD_FUNCTIONS` | Each present slot's named `functions`, including locals and excluding NIL-name stubs. |
| `BEAM_MODULE_FIELD_NIFS` | Each present slot's attached NIF registrations in `nifs`, including unexported registrations; names are copied as UTF-8. |
| `BEAM_MODULE_FIELD_ATTRIBUTES` | Retained attributes ETF in each present slot's `attributes_etf`. |
| `BEAM_MODULE_FIELD_COMPILE` | Retained compilation-information ETF in each present slot's `compile_info_etf`. |

Exports represent active external dispatch, including tracing, and exclude
error-handler placeholders. They are not historical exports of old/pending
code. Native-only records can have exports with no code-presence flags. An
old-only record can have code with no effective exports. Neither inventory
guarantees later resolution, application readiness or successful execution.

Metadata bytes include their ETF version byte. A requested NULL pointer with
zero size means no retained chunk; encoded `[]` is present data. Unrequested
details are zeroed. Decoding uses `beam_enif` under its existing trusted-input
contract: copied bytes may outlive the VM, while a host `BeamEnv` must be freed
before VM termination. Inspection adds no decoder-safety guarantee.

Initialize snapshots to `{0}` and release an earlier result before reusing its
destination. Errors leave the output unchanged; allocation failure fails the
whole capture rather than returning a truncated inventory. Every nested pointer
belongs to copied snapshot storage. `beam_module_snapshot_release` frees all
of it and clears the snapshot; NULL/empty values are harmless. Release needs no
live VM. Keeping a snapshot does not pin executable code or native objects.

Inspection captures membership and selected code/export facts in one protected
observation. It can run before start without workers and while background
Erlang or an asynchronous application entry runs. It remains an owner-thread
operation outside callbacks. Conflicting code-management permission returns
`BEAM_BUSY`; inspection does not wait behind permission held by work that may
need a later host action. Scheduled work and copying can still take unbounded
time. Queries neither pump host callbacks nor join workers. Separate queries
can observe different code states. Fresh work is rejected after terminal
admission closes; a racing capture returns its completed result if completion
wins, otherwise `BEAM_STOPPED`, after its internal work retires.

### Publication, deletion and reclamation

`beam_module_replace(vm, module, &error)` publishes complete supplied BEAM bytes
using the runtime's current/old slot rules. With current code and no old code,
publication moves current to old and installs the supplied current code. An
empty or old-only namespace can also receive current code. Current plus old
returns `BEAM_BUSY`; no implicit purge occurs. Publication changes external MFA
dispatch. Existing processes can remain in old local code until the application
coordinates an external tail call. C does not migrate their state.

`beam_module_delete(vm, name, &error)` withdraws ordinary current external
dispatch and moves current code to old. An occupied old slot returns
`BEAM_BUSY`; no current code returns `BEAM_NOT_FOUND`. Deletion does not kill
processes or reclaim old code. Selected native exports can remain callable.

`beam_module_soft_purge(vm, name, &error)` asks the actual core purger to check
and reclaim old code without killing processes. No old code is a successful
no-op. A process still using old code returns `BEAM_BUSY`, preserving current
and old code. A successful purge leaves current code intact. Process-code
checks, native execution and runtime barriers can delay completion; there is no
timeout or promise of bounded latency. The call pumps no host callbacks.
Detected old-code execution returns `BEAM_BUSY` rather than waiting for processes
to switch versions.
If halt wins while a purge is accepted, the call drains and joins terminal VM
work before returning `BEAM_STOPPED`, keeping its requester storage alive until
that work retires. This execution-bearing cleanup differs from pure inspection,
which never joins workers.

These three operations require a running host-controlled VM, owner-thread use
outside callbacks, and no outstanding asynchronous application entry. Active
code management returns `BEAM_BUSY` without queueing behind it. Core code,
pending/incoming `on_load` and attached NIF instances remain unsupported;
explicitly selected NIF targets remain busy. Native replacement, hard purge,
prepared batches and `on_load` execution are not exposed by these functions.
Admission rejection and `BEAM_BUSY` leave published code and processes unchanged.
If an accepted purge loses its internal requester, it returns `BEAM_SYSTEM_ERROR`
only after the purger protocol retires; reclamation may already have completed.
Inspect the remaining code state before deciding whether to retry. Terminal
outcomes retain the VM's normal shutdown and cleanup semantics.

`beam_module_unload(vm, name, &error)` removes all current/old code only when safe
without terminating processes. It starts no workers before start, dispatches no
callbacks and does not freeze registration. Missing code returns NOT_FOUND;
core/protected, pending `on_load` and bound native code are unsupported. Active
code, uninspectable dirty work or conflicting code management returns BUSY and
leaves code callable. Coordinate application exit from that code before retry.
Unloading does not unregister processes, erase atoms/tables or reset application
state. Retained literal values survive while storage reclamation may be deferred.
A fun alone does not pin code and may become uncallable after removal.

### Scheduler control

`beam_scheduler_get_info(vm, &info)` fills exact-size `beam_scheduler_info` with
normal/dirty CPU totals and online counts plus the dirty-I/O total. No Erlang,
callbacks, joining or handler-registration freeze occurs. Output is unchanged
on errors. Counts describe selected policy, not utilization or live threads;
before start/after stop they remain meaningful configuration. A transition can
precede worker acknowledgment; Erlang may subsequently change online counts.
Async/polling workers are not included as schedulers.

`beam_scheduler_set_online(vm, normal, dirty_cpu, &error)` changes both
online counts together in a running VM. Positive counts must fit initialized
capacity and dirty CPU online must not exceed normal online. There is no scaling
or clamping; invalid input changes neither count. BEAM_OK waits for both groups
to acknowledge; an existing transition returns BEAM_BUSY. Halt retires pending
native work and returns BEAM_STOPPED. The operation pumps no callbacks and has
no timeout. Offline workers retain threads/resources; this does not resize
capacity, change dirty-I/O workers or pause the VM.

`beam_scheduler_inspect_all(vm, &snapshot)` returns a complete copied
`beam_scheduler_snapshot` with aggregate `info`, `count` scheduler records in
`items`, and `queue_count` distinct run-queue records in `queues`.
Initialize the destination to `{0}` and release an earlier result before reuse.
Failure leaves it unchanged. Allocation failure returns `BEAM_OUT_OF_MEMORY`
after freeing partial copies. The library fills `info.struct_size` on success.

Scheduler identities and queue identities each contain a `kind` and a one-based
class-local `index`. The arrays are sorted by kind, then index:

| Kind | Scheduler membership | Queue association |
| --- | --- | --- |
| `BEAM_SCHEDULER_NORMAL` | Initialized normal capacity, including offline selections | Scheduler index N references normal queue N. |
| `BEAM_SCHEDULER_DIRTY_CPU` | Initialized dirty CPU capacity, including offline selections | All reference dirty CPU queue 1. |
| `BEAM_SCHEDULER_DIRTY_IO` | Initialized dirty I/O capacity | All reference dirty I/O queue 1. |

Each `beam_scheduler_record` contains its `id`, boolean `selected_online`, and
`queue` identity. Normal and dirty CPU selection follows the captured online
prefix; dirty I/O is selected because it has no independent offline control.
Selection is policy, not worker acknowledgement, activity or CPU consumption.
Identities belong to this VM lifetime and are neither OS thread IDs nor CPU
numbers. Normal work can move between queues.

Each `beam_scheduler_queue_record` contains its `id` and actual `queued_tasks`
observation, counting queued process/port work and excluding executing work.
Each shared dirty queue appears once. Counts do not represent mailbox length,
total processes, utilization or a permanent assignment of processes to schedulers.
A zero queue length does not establish that a scheduler or VM is idle.

The aggregate policy and derived selection bits are consistent within the
snapshot. Queue lengths are individually synchronized observations taken while
execution continues; they are not a simultaneous global measurement and need
not sum to a separately captured statistic. Capacity and membership stay fixed
when online participation changes.

Inventory inspection requires a running host-controlled VM, the owner thread
outside callbacks, and management admission independent of the asynchronous
application-entry slot. Process-owned execution returns `BEAM_UNSUPPORTED`.
Queries invoke no callbacks, start no workers and do not join them. Stop races
return a completed copy if completion wins, otherwise `BEAM_STOPPED` after
native request references retire. Synchronous completion has no latency bound.
`beam_scheduler_snapshot_release` frees both copied arrays and clears the record;
NULL/empty values are harmless, no live VM is required, and the snapshot remains
readable through VM/system termination while the library stays loaded.

Scheduler wall-time measurement, CPU binding and pause/resume are not exposed
by this interface.

## Process management

Include `<beam/process.h>` and link the selected `BEAM::Runtime` or
`BEAM::RuntimeStatic`. Process management uses existing `BeamPid`, `BeamEnv` and
`BeamTerm` values; sending, registered lookup and liveness retain their
`beam_enif` interfaces. A copied PID identifies a process in its originating VM
lifetime and does not retain that process. After termination it is historical
data, unsuitable for fresh PID operations.

These functions require a running host-controlled VM on its owner thread,
outside callbacks. Process-owned execution returns `BEAM_UNSUPPORTED`, pre-start
calls return `BEAM_INVALID_STATE`, and fresh work after terminal admission closes
returns `BEAM_STOPPED`. Wrong-thread and callback calls retain
`BEAM_WRONG_THREAD` and `BEAM_INVALID_STATE`, respectively.
Read-only requests use management admission independently of an outstanding
asynchronous application entry; a conflicting management operation can return
`BEAM_BUSY`. Queries neither service callbacks nor join workers. An admitted
query racing stop returns its completed safe copy if completion wins, otherwise
`BEAM_STOPPED`, after all native request references retire. Target cooperation
and native/dirty execution can delay inspection; there is no timeout or promise
of bounded latency.

### PID membership

`beam_process_list_pids(vm, &list)` returns `beam_process_list { count, pids }`.
The read-only copied array contains local PIDs at one observation point during
the call, including runtime processes, without duplicates and in unspecified
order. It is a membership snapshot; it does not capture all process states,
heaps or mailboxes together. A listed process can disappear before inspection.
An empty membership is a successful observation, whereas a stopped VM returns
`BEAM_STOPPED`.

Zero-initialize the destination, release an earlier list before reuse, and leave
individual array elements owned by the list. Errors leave the destination
unchanged; allocation failure returns `BEAM_OUT_OF_MEMORY` rather than a partial
list. `beam_process_list_release` frees its storage and clears it, accepts
NULL/empty values, and requires no environment or live VM.

### Targeted process snapshots

Initialize `beam_process_query` with `beam_process_query_init`. It sets exact
`struct_size` and `fields = 0`, requesting PID and `is_system_process` only.
`beam_process_inspect(vm, &pid, &query, &snapshot)` returns a completed copied
`beam_process_snapshot`. Unknown field bits or an incorrect structure size
return `BEAM_INVALID_ARGUMENT`. An absent target, including one that disappeared
after enumeration, returns `BEAM_NOT_FOUND` without a fabricated terminal state.

Every successful snapshot contains its `pid`, actual runtime system-process
flag, and requested `fields` mask. Unrequested groups are zeroed. Inspect the
mask before using optional fields:

| Field bit | Snapshot members and meaning |
| --- | --- |
| `BEAM_PROCESS_FIELD_STATE` | `state`: observable process-info status, using `BEAM_PROCESS_RUNNABLE`, `RUNNING`, `WAITING`, `GARBAGE_COLLECTING`, `SUSPENDED` or `EXITING` with the same prefix. Inspection's temporary system work does not establish application activity. |
| `BEAM_PROCESS_FIELD_REDUCTIONS` | `reductions`: existing scheduling-work accounting in a `uint64_t` container, not elapsed time or a new nonwrapping counter. |
| `BEAM_PROCESS_FIELD_MEMORY` | `memory_bytes`: process-memory accounting in bytes, not host RSS or all storage retained by referenced binaries. |
| `BEAM_PROCESS_FIELD_MESSAGE_QUEUE` | `message_queue_len`: message count under process-info signal/mailbox semantics, not every pending signal. |
| `BEAM_PROCESS_FIELD_INITIAL_CALL` | `initial_call`: stored module/function atom names and unsigned arity in `beam_process_mfa`, not a current stack trace or inferred application entry. |
| `BEAM_PROCESS_FIELD_REGISTERED_NAME` | `has_registered_name` and `registered_name`: observed registration, with an explicit presence bit distinguishing no name from registration under an empty atom. |

`beam_process_atom_name` contains copied UTF-8 `data` and authoritative byte
`size`, preserving empty atoms and embedded NUL bytes. Names are not retained
registrations. Different inspections can observe different states, and a
snapshot keeps neither the target nor its code alive. Collection uses existing
process-info locking, signal preparation and accounting; ordinary processes
waiting for a host-request reply remain inspectable through system signals.
No helper process, implicit library module or host callback is required.

Zero-initialize snapshots and release an earlier result before reuse. Errors
leave output unchanged, including after partial-copy allocation failure; the
whole requested result succeeds or fails. `beam_process_snapshot_release` frees
nested copied names and clears the record, accepts NULL/empty values and needs
no live VM. Both process lists and detailed snapshots survive VM/system
termination while the library stays loaded. VM-bound terms and environments
retain their separate lifetime rules.

### Spawn and publication

`beam_process_spawn_options_init(&options)` sets exact `struct_size` and an
undefined `group_leader` PID. The initial options contain no other tuning.

```c
beam_status beam_process_spawn(beam_vm *vm,
    const char *module, const char *function,
    BeamEnv *arguments_env, BeamTerm arguments,
    const beam_process_spawn_options *options, BeamPid *out, beam_error *error);
```

Names use the existing entry-call nonempty UTF-8 C-string restrictions.
Arguments are a proper list in a valid host environment, within the runtime
arity limit. Spawn copies inputs and preserves the environment on both success
and failure; after return the host can clear or free it. This differs from the
consuming message-environment form of `beam_enif_send`.

`BEAM_OK` means the ordinary child was published and `out` contains its PID,
even if it exits immediately or stop races publication. Failure before
publication creates no child and leaves the PID output unchanged. The call
does not wait for readiness or the MFA's outcome. A missing export follows
ordinary child `undef` behavior; normal return ends the child and discards its
result. Use application acknowledgements or `beam_vm_call` when a result matters.

An undefined group leader selects the existing VM-root default. An explicit
leader must be a live local process at admission; absence returns
`BEAM_NOT_FOUND`. It is not kept alive. Root parent bookkeeping remains the
runtime's own; C does not acquire an Erlang sender identity or parent mailbox.
No code, I/O server, library inventory or native capability is acquired by spawn.
An occupied asynchronous application-entry slot returns `BEAM_BUSY` before
creation; background children from completed calls can continue independently.

Application stop messages and ordinary Erlang monitors can coordinate shutdown,
but sending success alone establishes enqueueing. Native host monitors/waits,
atomic monitored spawn, kill/general exit, links, suspended creation, scheduling
tuning and remote spawning remain outside this initial interface.

## Calling and results

`beam_vm_call(vm, module, function, arguments, &result)` invokes an export in a
new ordinary Erlang process after start/readiness. Module/function names follow
the UTF-8 atom-name rules above. Arguments must encode exactly one uncompressed
ETF proper list with its version byte, arity 0–255. `{131, 106}` encodes `[]`.
Improper lists, trailing data, compressed/internal-local/unsupported encodings
or a non-list term return `BEAM_INVALID_TERM`. Inputs are copied.

The owner thread blocks until entry return, exception, exit or terminal VM
action. Children can outlive a normal call. Missing modules/exports produce
Erlang `undef`, not a loading status. There is no timeout or asynchronous cancel.
The non-NULL output is cleared even on failure; release any earlier result
before reuse. Clearing does not free its old allocation. Inspect fields only
after BEAM_OK, which accompanies all four result kinds:

| `beam_result.kind` | `value` | `halt_status` |
| --- | --- | --- |
| `BEAM_RETURN` | One copied ETF value. | Zero. |
| `BEAM_EXCEPTION` | ETF `{Class, Reason, Stacktrace}`; external exits may have an empty stacktrace. | Zero. |
| `BEAM_HALT` | Empty buffer. | First accepted Erlang halt status. |
| `BEAM_COMPLETE` | Copied opaque ETF terminal host value. | Selected completion status. |

Ordinary integer halt (`beam:halt` in modern hosts, `erlang:halt` in OTP hosts)
returns to the host-controlled caller; its stored
status is masked with `INT_MAX`. A halt during an admitted call completes
shutdown and returns BEAM_HALT. A background halt before admission makes a
later call return BEAM_STOPPED; observe status or stop to complete cleanup.
Abort/crash-dump forms, disabled flushing and finite flush timeouts are rejected
with an Erlang exception. Process-owned halt instead exits the host process.

`beam_result_release(&result)` frees library-owned bytes and clears the record.
It accepts NULL/already cleared results and requires no live system/VM. Release
successful results even when ignoring the payload. Do not supply arbitrary
host buffers or release shallow copies twice.

## Host services

`beam_vm_set_host_handler(vm, handler, context)` registers one optional
`beam_host_handler(void *context, beam_bytes request, beam_host_reply *reply)`;
NULL clears it. Registration is allowed before the first call is admitted,
including while running. Admission freezes it even if scheduler-side ETF
decoding later fails; earlier host validation/allocation failure does not.
A frozen registration returns INVALID_STATE; halted/stopped VMs return STOPPED.

Load optional `beam_host` explicitly and call `beam_host:request(Request)`.
Registration alone loads no module or I/O server. `BEAM_HOST_MODULE` identifies
the installed bridge as a build input. Requests need not be lists. The handler
receives one copied ETF term and a reply handle borrowed only for its invocation.

Callbacks run serially on the owner thread, without VM locks: inside
`beam_vm_call` or `beam_vm_pump` for host-controlled execution, and inside `beam_vm_enter` for
process-owned execution. They must return and cannot reenter VM operations,
including state queries, or wait for another owner-thread callback. Reentry
returns INVALID_STATE; calling `beam_init` instead reports ALREADY_STARTED.
Another thread does not bypass these rules.

For host-controlled VMs, background requests are serviced by the next
`beam_vm_pump` or synchronous call, including when no asynchronous call is active. Load, observation and stop do not invoke handlers. Entry completion need
not service every child's request. Stop cancels pending work; requester exit or
halt can discard an admitted callback's reply without undoing its host effects.
Context storage remains valid until stop completes.

### Replies and terminal completion

`beam_host_reply_copy(reply, bytes)` copies one complete uncompressed ETF term
including its version byte; nonempty data must fit the native byte-count type.
Return BEAM_OK from the callback to select that reply. The handle must be the
current callback's reply; a second copy or use outside the callback returns
INVALID_STATE without replacing the first. Wrong-thread calls return WRONG_THREAD.
Invalid arguments or allocation can return INVALID_ARGUMENT/OUT_OF_MEMORY;
a failed copy does not mark the reply supplied.

BEAM_OK here means copied, not fully decoded. Scheduler-side validation later
rejects malformed/compressed/trailing/internal-local ETF. Source bytes can be
reused immediately after a successful copy. The bridge reports:

| Erlang result | Meaning |
| --- | --- |
| `{ok, Reply}` | Valid copied reply and successful callback; Reply may itself describe an application error. |
| `{error, no_handler}` | No registered callback. |
| `{error, no_reply}` | Callback succeeded without a reply. |
| `{error, invalid_reply}` | Copied bytes were not an acceptable term. |
| `{error, {callback_failed, Status}}` | Nonzero callback status as an integer. |
| `{error, out_of_memory}` / `{error, invalid_term}` | Recoverable request allocation/encoding failure. |
| `{error, stopped}` | Request admission closed. |

Shutdown can terminate the requester instead of delivering a tuple; delivery
after cancellation is not guaranteed.

`beam_host_reply_complete(reply, value, halt_status)` selects terminal completion
instead of an Erlang reply. It requires the current reply, a valid uncompressed
ETF term and nonnegative status. Unlike ordinary reply copy it validates before
selection; failure leaves the reply available for correction. The callback must
return BEAM_OK. After it returns, the VM arbitrates the request against earlier
outcomes, stops, and returns BEAM_COMPLETE to the active call if it wins. An
earlier outcome wins without callback reentry or cross-thread cancellation.

## Returning BEAM boot

The modern returning host uses public composition/lifecycle to manage one
complete application VM under a host-supplied system:

```c
beam_status beam_return_prepare(const beam_return_options *options,
                                beam_return_boot **out, beam_error *error);
beam_status beam_return_run(beam_system *system, beam_return_boot **boot,
                            beam_outcome *out, beam_error *error);
beam_status beam_return_discard(beam_return_boot **boot);
void beam_outcome_release(beam_outcome *outcome);
```

Preparation requires exact size and an initially NULL output. It copies inputs,
retains supplied library owners and starts nothing. Callback pointers/context remain
borrowed through the run. Run belongs to the system's owner thread and cannot
be called from a callback. Initialization failure leaves preparation available
for retry/discard; after successful initialization execution consumes it following
VM termination, including application failure. If final cleanup fails, preparation
retains its stopped VM and discard retries cleanup before release. Cleanup
diagnostics remain observable even if startup prevented an application outcome.

| Returning option | Contract |
| --- | --- |
| `struct_size` | Exact `sizeof(beam_return_options)`. |
| `host` | Optional `beam_host_options`: generic inputs, libraries, explicit startup actions and deferred modules. |
| `entry_module`, `entry_function` | Required export; argument list determines arity. |
| `arguments` | Uncompressed ETF proper list; empty bytes select `[]`. |
| `module_count`, `modules` | Copied `beam_return_module` entries: name, bytes, deferred flag and origin. |
| `output`, `output_context` | Host byte-output handler and borrowed state. |
| `code`, `code_context` | Later code-acquisition handler and borrowed state. |
| `input`, `input_context` | Input handler and borrowed state. |
| `vm` | Optional copied host-controlled options; no initial root, preexisting composition or process policy. NULL selects conservative defaults. |

Supply the ordinary returning boot modules and all code needed by the selected
startup actions and entry. A returning module with zero `deferred` loads before
start without `on_load`; nonzero selects reviewed deferred initialization in the
Host layer. Origins are copied labels. Conflicts follow generic composition
validation. Each preparation retains its own references.

Explicit startup must finish before application entry. Entry return, exception,
requested finish, halt or a reported terminal failure ends the VM, including
children. The first terminal cause wins; cleanup diagnostics remain separate.

Output/code/input callbacks follow owner-thread, copy and no-reentry rules.
`beam_output_handler(context, destination, bytes)` returns `beam_output_status`:
`BEAM_OUTPUT_OK`, UNKNOWN, NOT_READY, BACKPRESSURE or FAILED.
`beam_code_handler(context, source, module, &bytes, &size)` returns
`beam_code_status`: `BEAM_CODE_OK`, NOT_FOUND, INVALID or UNAVAILABLE;
acquisition is distinct from subsequent publication. Input returns
`beam_input_status` LINE, EOF or PENDING (each prefixed `BEAM_INPUT_`) with
bytes/size: PENDING is temporary lack of
input, distinct from an empty line and EOF. Returned byte pointers are copied
rather than retained as caller storage.

Zero-initialize `beam_outcome` and set `struct_size` to its exact size before
running. Release any previous outcome before reuse: run can overwrite the record
without freeing old storage, while early errors can leave it unchanged.
`beam_outcome_release` frees copied storage and clears the entire record,
including `struct_size`; restore that field before another run.

Successful run reports kind RETURN, EXCEPTION, FINISH, HALT or FAILURE
with prefix `BEAM_OUTCOME_`. `term` is copied ETF, `halt_status` carries halt
status, `origin` identifies the failure source, and
`has_cleanup_error`/`cleanup_error` preserve secondary cleanup failure. Run can
return a non-OK cleanup status while still assigning an owning outcome, or
supply cleanup diagnostics without an application outcome. Release the
initialized output even after a failed run; API failure does not prove its
copied storage is empty. API status and application outcome remain distinct.

Native resource counters belong to their respective libraries, not the VM API.
Acceptance also observes actual OS resources; counters alone do not prove cleanup.

## SDK and service boundaries

[SDK deployment](deployment.md) documents CMake targets, verified
artifacts, native headers and relative bundles. Deployment does not activate
services or infer code inventories from entry names. OTP owns its acquisition,
seventeen external preloads, commands and application policy through the generic
process-owned API; the SDK supplies no OTP boot adapter or catalog.

Output callbacks cannot report partial counts: a failed callback has zero
confirmed bytes and unknown progress. Admitted callbacks must finish even when
a requesting service has a deadline. File cleanup failures
populate copied VM cleanup diagnostics and returning outcomes without replacing
the primary cause.

## Asynchronous host control

| Entry | Contract |
| --- | --- |
| `beam_system_options_init` | Set exact structure size and default main-thread ownership. |
| `beam_init_with_options` | `BEAM_SYSTEM_CALLING_THREAD` opts into dedicated ownership; initialization and all later VM operations/cleanup use that OS thread. Process-owned VMs are rejected on this route. |
| `beam_vm_call_begin` | Copy arguments and names into the single call slot; return `BEAM_BUSY` until the prior slot is consumed. Host-controlled execution only. |
| `beam_vm_pump` | Owner-thread, nonreentrant pump. Initialize `beam_pump_result.struct_size`; callback budget must be positive. Zero timeout polls. Returns callback count, `call_ready`, and `stop_requested`. |
| `beam_vm_call_take` | Transfer a completed result and consume its slot. Busy/no-slot return `BEAM_BUSY`/`BEAM_INVALID_STATE` without changing output. A stopped unrecorded call returns `BEAM_STOPPED` with an empty result. |
| `beam_vm_wake` | Thread-safe wake of a valid, externally lifetime-synchronized VM handle. It performs no Erlang operation and grants no cross-thread stop permission. |

Callbacks remain serial and cooperative. The timeout bounds waiting, not arbitrary
native callback execution. Terminal outcomes use the first-recorded-outcome rule.
An owner may stop with an outstanding call; result retrieval remains available
after workers join. Destroying a VM releases an unconsumed slot. Existing copied
results remain host-owned.

Module get-info, copied name lists and detailed inspection can coexist with the
occupied entry slot. They do not pump callbacks or consume its result. Module
mutation and scheduler changes remain busy until the entry slot is consumed.

## Supplemental embedded dependencies

`beam_composition_options.embedded_dependency_count` and
`embedded_dependencies` supply immutable product-declared build facts for
`beam:system_info(embedded_3pps)`. Each `beam_embedded_dependency` has an exact
`struct_size`, a nonempty UTF-8 `name` of at most 255 Unicode characters and a
`state`: `BEAM_EMBEDDED_INCLUDED` or `BEAM_EMBEDDED_EXCLUDED`. At most 1024 entries
are accepted. Names are case-sensitive, unique across states and cannot collide
with the VM-owned dependency inventory, including excluded VM dependencies.

Preparation validates and copies entries without creating atoms, loading code,
invoking callbacks or granting capabilities. Invalid input returns
`BEAM_INVALID_ARGUMENT` without publishing a preparation; allocation failure
returns `BEAM_OUT_OF_MEMORY`. A successfully initialized VM retains the copied
facts across product restart until termination. A later VM receives only its own
composition. Zero entries select only VM-owned reporting.

The query preserves VM-owned facts and appends supplements in declaration order
to the corresponding included/excluded lists. Public list order is unspecified.
These facts describe embedded source/build choices, not current native mappings
or every dependency. Native packages are not automatically added. Startup
context remains an opaque independent ETF input. This exact-SDK interface is
experimental; rebuild consumers when its layout changes.
