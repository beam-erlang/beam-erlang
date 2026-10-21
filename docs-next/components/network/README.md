# libbeam-network.so

The network component adds numeric-address TCP sockets through `beam_socket`.
Its complete module inventory is `beam_socket`, `socket_registry`, `socket`,
`maps`, `lists` and `prim_socket`. The latter modules support the component;
their inclusion does not make the complete OTP networking or standard-library
surface part of the modern interface.

## Select the capability

Select `BEAM_COMPONENT_NETWORK` with the exact path to
`<prefix>/lib/libbeam-network.so`. Its service bit is `BEAM_SERVICE_SOCKET`.
Supply the ordinary services inventory and the base library, which includes
`beam_time`. Follow the [component lifecycle](../README.md#selection-and-activation)
to prepare, start and activate it. Networking is independent of file and zlib
selection.

Activation initializes native socket support and its registry before admitting
application calls. Each listener or connected socket belongs to one Erlang
process. Passing the handle in a message does not transfer ownership. Native
resources, polling registrations and pending I/O are reclaimed on owner death
or VM termination even if other processes retain the handle. Editing the
handle's descriptive owner field grants no access to a native socket. The
registry is managed by the VM's service lifecycle; an
application does not start it itself.

## Connect and send

This expression requires a server listening at the chosen address:

```erlang
Deadline = beam_time:deadline_after(1000),
case beam_socket:connect(#{addr => {127, 0, 0, 1}, port => 9000}, Deadline) of
    {ok, Socket} ->
        try beam_socket:send(Socket, <<"hello\n">>, Deadline)
        after beam_socket:close(Socket, infinity)
        end;
    {error, Error} -> {error, Error}
end.
```

Reuse a single absolute deadline across a sequence to bound the time available
for that sequence. A successful send reports backend acceptance; it does not
acknowledge application processing by the peer.

## beam_socket reference

An address is exactly `#{addr => {A, B, C, D}, port => Port}` with four integer
octets in 0–255 and a port in 0–65535. Additional keys are rejected. Listening
on port zero lets the OS select a port; use `sockname/1` to retrieve it.

`Deadline` is `infinity` or an absolute VM-local monotonic deadline from
`beam_time:deadline_after/1`. Wrong types and malformed handles raise `badarg`.
Operational failures return `{error, Error}`. Handles are opaque values. See the
shared [validation and result conventions](../../stdlib/services.md#validation-and-results)
for error precedence.

| Function | Successful result and behavior |
| --- | --- |
| `listen(Address, Deadline)` | `{ok, Listener}`; creates, binds and listens on a TCP socket. |
| `connect(Address, Deadline)` | `{ok, Socket}`; a failed attempt closes its internal socket. |
| `accept(Listener, Deadline)` | `{ok, Socket}` owned by the accepting caller. |
| `send(Socket, Bytes, Deadline)` | `ok` after accepting the whole binary; failures report progress. |
| `recv(Socket, Maximum, Deadline)` | `{ok, Bytes}` up to the nonnegative maximum, or `eof`. |
| `close(Socket, Deadline)` | `ok`; owner-idempotent and ignores deadline expiry. |
| `sockname(Socket)` | `{ok, #{addr => IPv4, port => Port}}`; no deadline parameter. |

TCP is a byte stream. A positive-length receive returns available nonempty
bytes, EOF or an error; it does not wait for exactly `Maximum` bytes. Callers
implement framing and retain partial headers/bodies between reads. Peer EOF is
distinct from locally closing the socket.

Zero-length receives return `{ok, <<>>}` and empty sends return `ok` on an
available, open, owned socket. They perform no I/O and ignore expiry, but still
check ownership and state. Non-owner use returns `not_owner`, including close
and empty operations. Other operations on a closed socket return `closed`.

## Errors and deadlines

Error maps contain `kind` and `service => socket`. Native details use
`native => #{domain => posix | socket, code => Code}`. Kinds are `unavailable`,
`timeout`, `not_owner`, `closed`, `permission_denied`, `invalid_data`, `busy`
and `io`.

| Native error | `kind` |
| --- | --- |
| `timeout` | `timeout` |
| `closed`, `ebadf` | `closed` |
| `eaddrinuse` | `busy` |
| `eacces`, `eperm` | `permission_denied` |
| `einval` | `invalid_data` |
| Other errors, including `econnrefused` | `io`, retaining the native code |

Native `timeout` and `closed` use the `socket` domain; errno codes use `posix`.

Every send error includes `bytes_written` and `progress_unknown`. The TCP
facade records accepted bytes across continuations and reports known progress.
Admission and ownership errors report zero confirmed bytes.
When the socket remains usable, retry only the unaccepted suffix. Retrying the
whole message can duplicate a prefix already accepted by the backend.

Accept, connect, send and receive cancel native waits on timeout and retire
matching notifications before returning. Continuations keep the original
deadline. Positive millisecond conversions round upward. Timeout does not undo
bytes already sent, and these deadlines do not establish a general hard bound
on return to a C host. Close validates the deadline but never skips cleanup
because it has expired.
Timeout leaves no background operation using caller-owned buffers or unmatched
late notification. There is no pending-operation handle or separate
cancellation API.

## Scope and implementation sources

The modern facade currently covers numeric IPv4 TCP. It does not expose DNS,
IPv6, UDP, TLS, distributed Erlang, socket-option control or ownership transfer.
The component ships no legacy inet drivers or `prim_net`. Its support module
exports are not a promise that every upstream `socket` operation is available
in a minimal modern composition.

The current implementation is defined by
[beam_socket.erl](../../../src/beam/network/beam_socket.erl) and the
[network component recipe](../../../src/beam/network/CMakeLists.txt).
