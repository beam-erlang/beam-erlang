# Base-library reference

The ordinary base inventory requires no boot context or selected OS service. A C host supplies their verified bytecode explicitly; command profiles
acquire them as part of their own composition. The [module catalog](README.md)
describes that boundary. These modules use Erlang terms and the shared VM's value
and resource mechanisms. Their public functions use ordinary Erlang code and
explicitly bound native byte, collection, text, mathematical, driver, debugger, array and persistent-store
contracts. Guard and operator language instructions execute independently of
public native exports. Qualified guard calls require a matching guard-eligible
contract selected by the host.

## beam

`beam` exposes VM primitives selected by the modern host. The
[native callable catalog](../c-api/native-contracts.md) lists the complete primitive
surface and its contracts. Native functions retain their native arguments,
results, exceptions and execution behavior. Clock and timer APIs use `beam_time`.
`spawn/1`, `spawn_monitor/1`, `monitor/1` and `halt/1` are ordinary helpers.

| Function | Result and behavior |
| --- | --- |
| `self()` | Calling process PID. |
| `spawn(Fun)` | PID of a new process running a zero-argument function. |
| `spawn_monitor(Fun)` | `{Pid, Ref}` with atomic spawn-and-monitor semantics. |
| `send(Destination, Message)` | Returns `Message` after asynchronous send; no delivery acknowledgement. |
| `monitor(Pid)` | Reference for ordinary `{'DOWN', Ref, process, Pid, Reason}` notifications. |
| `demonitor(Ref)` | Returns `true`; removes the monitor without flushing pending notifications. |
| `link(Target)` / `unlink(Target)` | Returns `true` with native link semantics. |
| `exit(Reason)` | Exits the caller. |
| `exit(Target, Reason)` | Returns `true` after sending an exit signal; does not acknowledge termination. |
| `error(Reason)` | Raises an Erlang error. |
| `raise(Class, Reason, Stack)` | Raises the supplied exception; malformed arguments return `badarg`. |
| `halt(Status)` | Calls the selected `beam:halt/2` with an empty option list; preserves the requested exit status. |

Spawning a process does not establish application readiness. Use an explicit
message/acknowledgement protocol when the caller depends on completed startup.
Likewise, observe a monitor notification when termination must be confirmed.

## Collection, binary and text modules

These six ordinary modules expose the OTP 29.0 APIs with BEAM module names.
Function names/arities, argument order, options, results, errors and exported types
retain upstream contracts except for the explicit omissions below. Native slots
require host-selected bindings; supplying bytecode alone enables no capability.
Their implementations are maintained directly in
[the modern library sources](../../src/beam/library/) and compiled with
deterministic artifact verification.

Ordinary calls such as `beam_maps:get/2` and `beam_binary:part/2,3` are not guard
forms. Use the existing language guards or appropriately selected native guard
operations. Hidden exports below remain support interfaces, with their original
annotations; export presence does not imply stability beyond the preview API.

## beam_binary

Access bytes with `at/2`, `first/1` and `last/1`; select ranges with `part/2,3`, including upstream negative-length forms. `copy/1,2` copies or repeats. `match/2,3` returns `{Offset,Length}` or `nomatch`; `matches/2,3` returns ranges. Search/split options are lists, including `{scope,{Offset,Length}}`, `global`, `trim` and `trim_all` where applicable. `referenced_byte_size/1` is a retention diagnostic and accepts bitstrings. Binary ranges may retain shared backing storage. Compiled patterns use opaque VM-local `cp/0` terms.

Exports: `at/2`, `bin_to_list/1`, `bin_to_list/2`, `bin_to_list/3`, `compile_pattern/1`, `copy/1`, `copy/2`, `decode_hex/1`, `decode_unsigned/1`, `decode_unsigned/2`, `encode_hex/1`, `encode_hex/2`, `encode_unsigned/1`, `encode_unsigned/2`, `first/1`, `join/2`, `last/1`, `list_to_bin/1`, `longest_common_prefix/1`, `longest_common_suffix/1`, `match/2`, `match/3`, `matches/2`, `matches/3`, `part/2`, `part/3`, `referenced_byte_size/1`, `replace/3`, `replace/4`, `split/2`, `split/3`.

## beam_lists

Functions retain upstream argument order: `map(Fun,List)`, `foldl(Fun,Initial,List)`, `member(Element,List)` and `keyfind(Key,Index,List)`. Membership uses exact equality and short-circuits; tuple-key lookup retains upstream numeric equality. `reverse(List,Tail)` accepts an arbitrary tail. Sorting, merging and callback algorithms retain their upstream semantics. Deprecated `zf/2` is omitted.

Exports: `all/2`, `any/2`, `append/1`, `append/2`, `concat/1`, `delete/2`, `droplast/1`, `dropwhile/2`, `duplicate/2`, `enumerate/1`, `enumerate/2`, `enumerate/3`, `filter/2`, `filtermap/2`, `flatlength/1`, `flatmap/2`, `flatten/1`, `flatten/2`, `foldl/3`, `foldr/3`, `foreach/2`, `join/2`, `keydelete/3`, `keyfind/3`, `keymap/3`, `keymember/3`, `keymerge/3`, `keyreplace/4`, `keysearch/3`, `keysort/2`, `keystore/4`, `keytake/3`, `last/1`, `map/2`, `mapfoldl/3`, `mapfoldr/3`, `max/1`, `member/2`, `merge/1`, `merge/2`, `merge/3`, `merge3/3`, `min/1`, `nth/2`, `nthtail/2`, `partition/2`, `prefix/2`, `reverse/1`, `reverse/2`, `rkeymerge/3`, `rmerge/2`, `rmerge/3`, `rmerge3/3`, `rukeymerge/3`, `rumerge/2`, `rumerge/3`, `rumerge3/3`, `search/2`, `seq/2`, `seq/3`, `sort/1`, `sort/2`, `split/2`, `splitwith/2`, `sublist/2`, `sublist/3`, `subtract/2`, `suffix/2`, `sum/1`, `takewhile/2`, `ukeymerge/3`, `ukeysort/2`, `umerge/1`, `umerge/2`, `umerge/3`, `umerge3/3`, `uniq/1`, `uniq/2`, `unzip/1`, `unzip3/1`, `usort/1`, `usort/2`, `zip/2`, `zip/3`, `zip3/3`, `zip3/4`, `zipwith/3`, `zipwith/4`, `zipwith3/4`, `zipwith3/5`.

## beam_maps

Functions retain key-first/map-last arguments, such as `find(Key,Map)` and `put(Key,Value,Map)`. `find/2` returns `{ok,Value}` or `error`. Invalid maps retain `{badmap,Term}`; strict missing-key operations retain `{badkey,Key}`. Iteration and callback algorithms retain upstream ordering and validation rules. Iterator representations remain implementation details.

Exports: `filter/2`, `filtermap/2`, `find/2`, `fold/3`, `foreach/2`, `from_keys/2`, `from_list/1`, `get/2`, `get/3`, `groups_from_list/2`, `groups_from_list/3`, `intersect/2`, `intersect_with/3`, `is_iterator_valid/1`, `is_key/2`, `iterator/1`, `iterator/2`, `keys/1`, `map/2`, `merge/2`, `merge_with/3`, `new/0`, `next/1`, `put/3`, `remove/2`, `size/1`, `take/2`, `to_list/1`, `update/3`, `update_with/3`, `update_with/4`, `values/1`, `with/2`, `without/2`.

## beam_string

Operations accept upstream Unicode chardata and operate on grapheme clusters where documented. Case conversion, lexemes, padding, trimming, searching and prefix-number parsing retain their input/result conventions. `to_integer/1` and `to_float/1` preserve the unconsumed remainder. The 32 documented obsolete Latin-1 API exports are omitted; their internal implementation helpers are retained when needed. Hidden numeric-prefix support exports retain their upstream roles.

Exports: `casefold/1`, `chomp/1`, `equal/2`, `equal/3`, `equal/4`, `find/2`, `find/3`, `is_empty/1`, `jaro_similarity/2`, `length/1`, `lexemes/2`, `list_to_float/1`, `list_to_integer/1`, `lowercase/1`, `next_codepoint/1`, `next_grapheme/1`, `nth_lexeme/3`, `pad/2`, `pad/3`, `pad/4`, `prefix/2`, `replace/3`, `replace/4`, `reverse/1`, `slice/2`, `slice/3`, `split/2`, `split/3`, `take/2`, `take/3`, `take/4`, `titlecase/1`, `to_float/1`, `to_graphemes/1`, `to_integer/1`, `trim/1`, `trim/2`, `trim/3`, `uppercase/1`.

## beam_unicode

Conversion accepts mixed/nested chardata and supports Latin-1, UTF-8, UTF-16 and UTF-32 with upstream encoding aliases and endianness. Invalid and incomplete input remain distinct: `{error,Prefix,Rest}` and `{incomplete,Prefix,Rest}`. Normalization, BOM conversion and character classification remain separate operations. Conversion can reuse binary storage; it promises neither detachment nor implicit normalization. Internal conversion exports retain their upstream roles.

Exports: `bin_is_7bit/1`, `bom_to_encoding/1`, `category/1`, `characters_to_binary/1`, `characters_to_binary/2`, `characters_to_binary/3`, `characters_to_binary_int/2`, `characters_to_list/1`, `characters_to_list/2`, `characters_to_list_int/2`, `characters_to_nfc_binary/1`, `characters_to_nfc_list/1`, `characters_to_nfd_binary/1`, `characters_to_nfd_list/1`, `characters_to_nfkc_binary/1`, `characters_to_nfkc_list/1`, `characters_to_nfkd_binary/1`, `characters_to_nfkd_list/1`, `encoding_to_bom/1`, `is_id_continue/1`, `is_id_start/1`, `is_whitespace/1`.

## beam_records

This is the OTP 29 native-record API. It creates and updates native records and inspects module/name/field metadata and definitions. Fields retain declaration order. Preserve the documented `badrecord`, `badfield`, `badmap` and `badarg` distinctions. Dynamic creation can construct records inconsistent with a loaded definition; these low-level operations retain their upstream tooling/debugging role.

Exports: `create/4`, `get/2`, `get_definition/2`, `get_field_names/1`, `get_module/1`, `get_name/1`, `is_exported/1`, `update/4`.

## beam_ets

ETS stores tuple objects in VM-owned tables. `new/2` creates a `set`,
`ordered_set`, `bag` or `duplicate_bag`, with upstream options for access,
key position, named tables, heirs and concurrency. Arguments, results,
validation and atomicity retain the OTP 29.0 contracts. Public errors identify
`beam_ets`.

Tables belong to processes rather than to the reachability of their identifiers.
Owner exit destroys a table or transfers it to its eligible heir; `give_away/3`
transfers ownership explicitly. A stale identifier does not identify a newly
created table with the same name. Hash tables use exact key equality;
`ordered_set` retains numeric comparison equality and defined traversal order.

`lookup/2`, `lookup_element/3,4` and `member/2` inspect objects. `insert/2`,
`insert_new/2`, `take/2`, deletion, counter updates and element updates preserve
their individual atomic scopes. Match/select operations support whole results,
bounded chunks and opaque continuation resumption. Fixation protects traversal
and reclamation; it does not prevent writers or create a snapshot.

`all/0`, `foldl/3`, `foldr/3`, `tab2list/1`, `init_table/2`, `match_delete/2`,
`delete_all_objects/1`, `select_delete/2`, `match_spec_run/2`,
`repair_continuation/2` and `test_ms/2` are ordinary Erlang helpers over the
selected native contracts. `info/1,2` reports diagnostics rather than exact
per-process ownership or a memory quota. Compiled match specifications and
continuations remain opaque, VM-local implementation values.

`fun2ms/1` is a compiler/shell-assisted pseudo-function. Compile literal calls
using the installed match-spec header, for example:

```erlang
-module(queries).
-include("ms_transform.hrl").
-export([adult_names_match_spec/0]).

adult_names_match_spec() ->
    beam_ets:fun2ms(fun({Name, Age}) when Age >= 18 -> Name end).
```

```sh
"$BEAM_PREFIX/bin/beam" compile \
  -I "$BEAM_PREFIX/lib/beam/tools/compiler/include" -- queries.erl
```

The transform replaces the literal call with match-spec construction. The shell
accepts interpreted literal funs and captured bindings through its separately
supplied match-spec tool. Calling the runtime function with an ordinary compiled
or external fun retains the upstream `badarg` exit; non-funs fail with
`function_clause`. Rejecting those values requires no evaluator/tooling code
in a base-only VM.

The facade supplies 63 source exports and no file-persistence, DETS-transfer,
QLC-adapter or interactive table-browser functions. The hidden
`internal_request_all/0`, `internal_delete_all/2`, `internal_select_delete/2`
and `match_spec_run_r/3` retain their implementation-support roles.
Acquiring the facade starts no services or processes and adds no QLC, DETS or
filesystem dependency. OTP independently retains its canonical `ets` API.

## beam_math

Scalar functions preserve the OTP 29.0 numeric contracts. Trigonometric functions
use radians; hyperbolic functions, logarithms, exponentiation, square root and
error functions return machine floats. Integer arguments can lose precision
when converted to floats; invalid numeric domains and nonfinite results retain
their native errors. No cross-platform bit identity or arbitrary-precision
transcendental result is promised. `atan2(Y, X)` and `pow(Base, Exponent)` retain
their argument order. `fmod/2` is floating remainder. `floor/1` and `ceil/1`
return floats, independently of integer-result language rounding.

Exports: `acos/1`, `acosh/1`, `asin/1`, `asinh/1`, `atan/1`, `atan2/2`, `atanh/1`,
`ceil/1`, `cos/1`, `cosh/1`, `erf/1`, `erfc/1`, `exp/1`, `floor/1`, `fmod/2`,
`log/1`, `log10/1`, `log2/1`, `pi/0`, `pow/2`, `sin/1`, `sinh/1`, `sqrt/1`,
`tan/1`, `tanh/1`, `tau/0`.

The 24 mathematical operations require explicit native bindings. `pi/0` and
`tau/0` are ordinary Erlang constants supplied by the module's bytecode.

## beam_ddll

This is the linked-in driver interface. `load/2`, `unload/1` and `reload/2`
retain their upstream reference-counting, pending-operation and open-port
behavior. The `load_driver/2`, `unload_driver/1` and `reload_driver/2` forms
retain the separate `kill_ports` policy; these pairs are not aliases.
`try_load/3` and `try_unload/2` expose lower-level operation options and pending
results. `monitor/2` and `demonitor/1` retain driver `UP`/`DOWN` notifications.
`loaded_drivers/0` and `info/0,1,2` inspect the selected driver's state.

Exports: `demonitor/1`, `format_error/1`, `format_error_int/1`, `info/0`, `info/1`,
`info/2`, `load/2`, `load_driver/2`, `loaded_drivers/0`, `monitor/2`, `reload/2`,
`reload_driver/2`, `start/0`, `stop/0`, `try_load/3`, `try_unload/2`, `unload/1`,
`unload_driver/1`.

Seven operations require native bindings. `format_error/1` uses the hidden
native helper `format_error_int/1`; the other wrappers retain their ordinary
Erlang implementations. Compatibility `start/0` returns
`{error,{already_started,undefined}}` and `stop/0` returns `ok`; neither starts
or stops a loader service. Driver loading still requires explicit
[ordinary native admission](../native-extensions/loading.md#ordinary-native-admission).
Supplying this module grants no driver permission and does not load NIFs or
arbitrary shared libraries.

## beam_debugger

This is experimental low-level debugger support. `supported/0` reports actual
debugger enablement; selecting bindings or supplying bytecode does not enable
it. Other operations preserve `undef` while debugging is disabled and their
existing runtime/configuration support conditions.

Exports: `breakpoint/3`, `breakpoints/1`, `breakpoints/3`, `instrumentations/0`,
`peek_stack_frame_slot/4`, `peek_xreg/3`, `register/1`, `stack_frames/2`,
`supported/0`, `toggle_instrumentations/1`, `unregister/2`, `whereis/0`,
`xregs_count/1`.

All thirteen functions require native bindings. `register/1`, `unregister/2`
and `whereis/0` manage one debugger process. Registration returns a session
token used in `{debugger_event, Session, Event}` messages. A breakpoint event
contains the upstream resume fun. No debugger process is created automatically.
`instrumentations/0` and `toggle_instrumentations/1` govern subsequently loaded
code; loaded modules keep their instrumentation. `breakpoint/3` sets or clears
line breakpoints and `breakpoints/1,3` inspects them.

Stack-frame and X-register inspection preserves suspended-process requirements,
`running`/`undefined` outcomes and bounded copying with `{too_large, Size}`
results. Exported session, event, frame, register-value and instrumentation types
retain their OTP 29.0 contracts. The module provides no debugger application or
stability guarantee for these experimental protocols.

## beam_format

`integer(Integer)` returns a decimal binary.
`term(Value, #{depth => Depth, bytes => Bytes})` returns bounded diagnostic text.
Both limits are required nonnegative integers, and no additional option keys
are accepted. This representation is for diagnostics, not serialization.

The renderer observes its limits while traversing the value. Depth zero does
not enter the value. Truncation uses `...`, counted within the byte limit; smaller
budgets use `.` or `..`. It does not render an unbounded value and then slice
the result, discover code metadata, or write to an output sink.
Container openers consume budget, and map pairs are visited incrementally.
Oversized scalars reserve space for the marker before copying leading bytes.
Floats use short format if it fits, otherwise only the marker; the renderer
does not request fixed-point formatting.

## beam_time

The host selects this ordinary base-library module together with its 15
native clock/timer bindings. Native calls dispatch directly to shared VM
implementations; conversion and deadline helpers remain ordinary Erlang.
Modern compositions expose these operations here. Erlang/OTP independently
selects their original `erlang` identities.

| Functions | Contract |
| --- | --- |
| `monotonic_time/0,1` | VM-local monotonic time; `/0` uses native units. |
| `system_time/0,1` | POSIX system time; `/0` uses native units. |
| `time_offset/0,1` | Offset between the monotonic and system clocks. |
| `timestamp/0` | System time as `{MegaSeconds, Seconds, Microseconds}`; not a uniqueness guarantee. |
| `convert_time_unit/3` | Integer conversion rounded toward negative infinity. |
| `start_timer/3,4` | Send `{timeout, Ref, Message}` on expiry. |
| `send_after/3,4` | Send `Message` directly on expiry. |
| `read_timer/1,2`, `cancel_timer/1,2` | Observe/cancel a timer with synchronous or asynchronous results. |

`time_unit()` accepts positive integer units per second, `second`, `millisecond`,
`microsecond`, `nanosecond`, `native` and `perf_counter`. Existing legacy aliases
`seconds`, `milli_seconds`, `micro_seconds` and `nano_seconds` are accepted.
Native/performance frequencies belong to the current VM lifetime. Conversion
uses arbitrary-size integer arithmetic and rejects invalid values with `badarg`.

Timer creation takes `(Time, Destination, Message[, Options])`. Relative time
is a nonnegative millisecond interval; `{abs,true}` selects an absolute monotonic
millisecond deadline. Destinations are local PIDs or locally registered names.
PID lifetime and name resolution retain the OTP 29 behavior; references and
remaining milliseconds retain their original result contracts.

Read/cancel accept `{async,Boolean}`, defaulting to `false`. Cancellation also
accepts `{info,Boolean}`, defaulting to `true`. Synchronous information is
remaining milliseconds or `false`; disabling cancellation information returns
`ok`. Asynchronous operations return `ok` and, when information is enabled,
send `{read_timer,Ref,Result}` or
`{cancel_timer,Ref,Result}`. These message tags are protocol data and keep their
names. Expiry, delivery, cancellation and receiver death can race; cancellation
does not remove an already delivered message. Invalid arguments/options retain
OTP 29 validation order and errors. Timer messages are not execution guarantees.

The library exports `time_unit/0` and `timestamp/0` types. There is no modern
`now/0` or calendar API in this unit.

`deadline_after(Milliseconds)` returns a VM-local absolute monotonic deadline
for a nonnegative interval. `deadline_after(infinity)` returns `infinity`.
Treat integer deadline values as opaque; they have no wall-clock meaning and
cannot be reused across VM lifetimes. Time queries observe VM clocks rather
than performing a pure transformation of their arguments.

`remaining(Deadline)` returns whole milliseconds clamped to zero, or `infinity`.
`expired(Deadline)` returns a boolean; infinity never expires. Remaining time
rounds downward, so `remaining(Deadline) =:= 0` does not by itself prove that
`expired(Deadline)` is true.

```erlang
Deadline = beam_time:deadline_after(1000),
false = beam_time:expired(infinity),
infinity = beam_time:remaining(infinity).
```

A deadline's enforcement belongs to the operation receiving it. See the
[file](../components/file/README.md#errors-and-deadlines),
[network](../components/network/README.md#errors-and-deadlines) and
[host-service](services.md) references for their different waiting and
cancellation guarantees.

The implementation sources are under [src/beam/library](../../src/beam/library/).

## beam_atomics

`new(Size, Options)` creates a positive-size array with one-based indices. Options
are a list of `{signed, Boolean}` tuples; the default is signed and the last
repeated option wins. Options are checked before size. `get(Ref, Index)`,
`put(Ref, Index, Value)`, `add(Ref, Index, Delta)`, `add_get(Ref, Index, Delta)`,
`exchange(Ref, Index, Value)` and `compare_exchange(Ref, Index, Expected, Desired)`
use the shared sequentially consistent 64-bit atomic kernels. Addition wraps.
`compare_exchange` returns `ok` on replacement, otherwise the actual value.
`exchange` returns the previous value. `info(Ref)` returns `size`, `memory`, `min`
and `max`. These eight operations require explicit native bindings.

`sub(Ref, Index, Delta)` and `sub_get(Ref, Index, Delta)` remain ordinary Erlang
and call the module's bound addition operation. Negation precedes handle/index
validation; nonnumeric decrements raise `badarith`. Errors retain public arguments.

## beam_counters

`new(Size, Options)` accepts exactly `[]`, `[atomics]` or `[write_concurrency]`;
options are checked before positive size. The default uses atomics. `get`, `put`,
`add` and `info` have the same signatures as `beam_atomics` and require their own
native bindings, without requiring public atomics bindings. Atomic `info` includes
`min` and `max`; write-concurrency `info` contains `size` and `memory`. Values wrap
as signed 64-bit integers. `sub/3` is ordinary Erlang calling bound `add/3`, with
the same negation and error-argument behavior as atomic subtraction.

## beam_persistent_term

`put(Key, Value)` stores a VM-wide persistent value and returns `ok`.
`put_new(Key, Value)` returns `ok` for a missing key or an identical existing
value; a different existing value raises `badarg`. `get(Key)` raises `badarg` for
a missing key, while `get(Key, Default)` returns the supplied default. `get()`
returns all `{Key, Value}` pairs. `erase(Key)` returns whether a value was removed;
`info()` returns `count` and `memory`. All seven operations require explicit
native bindings. Enumeration order is unspecified. Updates can cause global
literal reclamation work. Values and resource handles use the VM-owned store
and are reclaimed during VM termination. Shared native contracts choose one
public facade per VM.

These three modules are experimental ordinary base APIs. Loading their bytecode
does not bind native implementations; unbound native stubs raise `undef`.
