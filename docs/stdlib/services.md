# Boot and host-service interfaces

These interfaces use explicitly supplied ordinary bytecode. Loading them does
not activate capabilities. Output, code acquisition and the host bridge can be
used by a persistent C host without returning boot. Their selection and
lifetime are described in the [C API guide](../c-api/guide.md).

## Validation and results

The modern file, socket, output and code interfaces perform explicit argument
type, option and deadline-type checks before checking service availability.
These invalid arguments raise `badarg`. For valid file/socket handles, result
precedence is availability, native ownership/open state, then deadline expiry;
malformed native handle references are argument errors. Invalid path contents
(including relative paths and invalid UTF-8) and invalid bytecode are
operational `invalid_data` results once their operation reaches validation.

Service failures return `{error, Error}`. Every error map has `kind` and
`service`; a native detail is represented as
`native => #{domain => Domain, code => Code}`. Unix failures use `posix`, socket
conditions use `socket`, host replies use `host`, and code validation uses
`loader`. Prefer `kind` or documented native codes over diagnostic text.
Individual component references own their error mappings and I/O guarantees.

These rules describe the modern facades; the retained
[`zlib` interface](../components/zlib/README.md) has its own exception contract.

## beam_boot

`services/0` returns `{ok, Services}`, the fixed selected service list for
returning boot. It does not activate anything or expose private service PIDs.
`finish(Status)` requests completion with an integer status in 0–2147483647.
It does not resume ordinary application execution after an accepted request.
The C host receives a copied outcome after VM cleanup and decides how to map
that outcome to its own process exit status.

A normal application-entry return already completes returning boot; it does
not need `finish/1`. Entry completion ends the whole VM even if child processes
remain, so a server entry must stay alive until its workload and acknowledged
shutdown are complete.

Without returning boot, both functions return
`{error, #{kind => unavailable, service => boot}}`. Invalid `finish/1` arguments
still raise `badarg`. The module comes from the complete two-module boot
inventory, alongside internal `beam_return_boot`.

## beam_output

`write(Destination, Bytes, Deadline)` writes a binary to a host-defined sink.
`Destination` is a nonempty atom or binary logical key; it is not inherently a
filesystem path or a standard stream. The host defines the mapping. `Deadline`
is `infinity` or a VM-local monotonic deadline.

The result is `ok` after sink acceptance, or `{error, Error}`. Error maps include
`kind`, `service => output`, `bytes_written`, `progress_unknown` and native host
details. Kinds are `unavailable`, `timeout`, `not_found`, `busy`, `io` or
`closed`. Before admission, progress is zero and known. The current host output
callback cannot report partial byte counts: callback failure reports zero
confirmed bytes with unknown progress. Losing the dispatcher after admission
also reports unknown progress. Do not retry blindly when progress is
unknown.

Deadline checking is admission-only. After a request is admitted, the caller
waits for the actual host result even if the deadline expires. An empty write
to a selected service returns `ok` without calling the sink or enforcing
expiry. Sink acceptance does not promise durable storage.

## beam_code

`fetch(Source, Module, Deadline)` requests bytes from a host-defined source.
`Source` is a nonempty atom or binary key; `Module` is an atom. The result is
`{ok, CopiedBytes}` or `{error, Error}`. Fetch requires the selected code
acquisition service, applies its deadline only before admission, and does not
publish code or fetch dependencies automatically.

`load(Module, Bytes, Origin)` explicitly publishes module bytecode already held
by the caller. `Origin` is a copied nonempty atom or binary provenance label,
not an acquisition instruction. This operation checks module identity and
rejects ordinary `on_load`; it returns `ok` or `{error, Error}`. Publication
does not require or activate the code acquisition service.

Acquisition errors use `unavailable`, `timeout`, `not_found`, `invalid_data` or
`io`; malformed host replies become `invalid_data`. Publication errors use
`invalid_data` or `busy`, retaining the loader reason. Errors identify
`service => code`, with native `host` or `loader` details where applicable.

## beam_host

`request(Request)` is the low-level Erlang-to-C host bridge. It returns
`{ok, Reply}` or `{error, Reason}` using the host's registered protocol. This is
an embedding mechanism rather than a predefined filesystem/network API.
Successful request and reply terms are copied across the boundary.
Callback reply storage can be released after the successful reply has been
copied, according to the C host reply ownership protocol.

Callbacks execute serially on the C owner thread while the host services a VM
call; they must not reenter the VM. Pending requests between calls wait for a
later call, and stop cancels them. An executing callback must return on its own;
there is no forced interruption or general hard callback deadline. See the
[C API reference](../c-api/reference.md) for registration, terminal replies and
ownership rules.

## Supply explicit support

Select the independent `host`, `output`, `code` and `startup` inventories as
needed. Each contains its corresponding `beam_` module; `services` remains an
aggregate convenience inventory. Output/code use `beam_host` and the ordinary
base library's `beam_time`. Explicitly invoke `beam_output:start/0` or
`beam_code:start/0` before using the selected facility. The optional Host
controller uses `beam_startup` to execute requested actions and reviewed deferred
module initialization. Generic VM composition requires none of these modules.
Supplying code alone starts nothing.

The implementation sources are [beam_boot](../../src/beam/boot/beam_boot.erl)
and the [services directory](../../src/beam/services/).
