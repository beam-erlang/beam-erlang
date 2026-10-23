# Base-library reference

These seven ordinary modules require no boot context or selected OS service.
A C host supplies their verified bytecode explicitly; command profiles acquire
them as part of their own composition. The [module catalog](README.md) describes
that boundary. The facades use native Erlang values and existing runtime
identities; they do not replace those identities or introduce new guard BIFs.

## beam

| Function | Result and behavior |
| --- | --- |
| `self()` | Calling process PID. |
| `spawn(Fun)` | PID of a new process running a zero-argument function. |
| `spawn_monitor(Fun)` | `{Pid, Ref}` with atomic spawn-and-monitor semantics. |
| `send(Pid, Message)` | `ok` after asynchronous send; no delivery acknowledgement. |
| `monitor(Pid)` | Reference for ordinary `{'DOWN', Ref, process, Pid, Reason}` notifications. |
| `demonitor(Ref)` | `ok`; removes the monitor and flushes a matching pending notification. |
| `link(Pid)` / `unlink(Pid)` | `ok` with ordinary Erlang link semantics. |
| `exit(Reason)` | Exits the caller. |
| `exit(Pid, Reason)` | `ok` after sending an exit signal; does not acknowledge termination. |
| `error(Reason)` | Raises an Erlang error. |
| `raise(Class, Reason, Stack)` | Raises the supplied `error`, `exit` or `throw` exception. |

Spawning a process does not establish application readiness. Use an explicit
message/acknowledgement protocol when the caller depends on completed startup.
Likewise, observe a monitor notification when termination must be confirmed.

## beam_list

`map(List, Fun)` maps a proper list with a one-argument callback.
`fold(List, Initial, Fun)` folds left, calling `Fun(Element, Accumulator)`.
Callback exceptions propagate unchanged.

```erlang
[2, 4, 6] = beam_list:map([1, 2, 3], fun(N) -> N * 2 end),
6 = beam_list:fold([1, 2, 3], 0, fun(N, Sum) -> Sum + N end).
```

## beam_map

`find(Map, Key)` returns `{ok, Value}` or `not_found`.
`put(Map, Key, Value)` returns an updated native map. These operations add no
map traversal-order guarantee.

```erlang
Settings = beam_map:put(#{}, timeout, 1000),
{ok, 1000} = beam_map:find(Settings, timeout).
```

## beam_bytes

`size(Bytes)` returns a binary's byte count.
`slice(Bytes, Offset, Count)` returns a binary slice. Offsets are zero-based;
offset and count must be nonnegative integers, and the complete range must fit
inside the input. Invalid ranges raise `badarg`.

## beam_text

`from_utf8(Bytes)` validates a binary, returning `{ok, Bytes}` or
`{error, #{kind => invalid_data}}`. A non-binary input raises `badarg`.
`concat(Texts)` concatenates a list of valid UTF-8 binaries; invalid text is a
precondition violation and raises `badarg`. Neither operation normalizes text
or converts charlists.

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
