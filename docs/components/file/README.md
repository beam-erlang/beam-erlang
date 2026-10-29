# libbeam-file.so

The file library adds local file and directory operations through
`beam_file`. It contains the complete module inventory `beam_file`,
`prim_buffer` and `prim_file`, together with their native implementations.
`prim_buffer` and `prim_file` are implementation support; use `beam_file` for the
modern interface documented here.

## Select the capability

Acquire `libbeam-file.so` or link `BEAM::FileStatic`, then supply its inputs to
composition. After VM start, explicitly call `beam_file:start/0`; it loads buffer
and file NIFs, creates the real janitor and passes its PID as load information.
The facade uses the ordinary base library, including `beam_time`. See the
[library lifecycle](../README.md#selection-and-startup).

Handles belong to their opening Erlang process. Owner death and VM termination
retain responsibility for closing files. Passing a handle does not transfer
ownership; another process receives `not_owner`. Handle fields are descriptive
and cannot grant access to the underlying resource.

## First read

With the file capability active, an application can read an absolute path:

```erlang
Deadline = beam_time:deadline_after(1000),
case beam_file:read_file(<<"/srv/example/message.txt">>, Deadline) of
    {ok, Bytes} -> Bytes;
    {error, #{kind := not_found}} -> <<>>;
    {error, Error} -> beam:error({read_failed, Error})
end.
```

`read_file/2` opens and closes its own handle, including on failure. Use
`open/3`, `read/3` and `close/2` when the application needs to process a large
file in chunks.

## beam_file reference

Paths are absolute Unix UTF-8 binaries beginning with `/`, without NUL bytes.
Relative paths and invalid UTF-8 return `invalid_data`. Paths are not normalized;
this interface supplies no filesystem sandbox. Lossless non-UTF-8 paths and
Windows paths are outside the current interface.

`Deadline` is `infinity` or an absolute VM-local monotonic deadline created by
`beam_time:deadline_after/1`. Invalid argument types and unknown option keys
raise `badarg`. Operational failures return `{error, Error}`. See the shared
[validation and result conventions](../../stdlib/services.md#validation-and-results)
for error precedence.

| Function | Successful result and behavior |
| --- | --- |
| `open(Path, Deadline, #{mode => read})` | `{ok, Handle}` for reading. |
| `open(Path, Deadline, #{mode => write})` | `{ok, Handle}`; creates or truncates the file. `mode` is the only option. |
| `read(Handle, Maximum, Deadline)` | `{ok, Bytes}` with up to `Maximum` bytes, or `eof`; `Maximum` is nonnegative. |
| `write(Handle, Bytes, Deadline)` | `ok` after accepting the whole binary; failures report write progress. |
| `close(Handle, Deadline)` | `ok`; idempotent for the owner and ignores deadline expiry. |
| `read_file(Path, Deadline)` | `{ok, Bytes}` for the whole file, including an empty file. |
| `write_file(Path, Bytes, Deadline)` | `ok`; creates/truncates, writes the binary and closes. |
| `info(Path, Deadline)` | `{ok, #{size => Size, type => Type}}`; does not follow the final symlink. `Type` is `regular`, `directory`, `symlink`, `device` or `other`. |
| `list_directory(Path, Deadline)` | `{ok, Names}`; UTF-8 binary entry names in unspecified order. |
| `make_directory(Path, Deadline)` | `ok`; does not create missing parent directories. |
| `remove_file(Path, Deadline)` | `ok`; unlinks the path. |
| `remove_directory(Path, Deadline)` | `ok`; the directory must be empty. |
| `rename(Source, Destination, Deadline)` | `ok`; native Unix rename behavior, including replacement and cross-filesystem errors. |

A positive-length read returns nonempty bytes, EOF or an error; a successful
read may return fewer bytes than requested. Zero-length reads
return `{ok, <<>>}` on an available, open, owned handle without native I/O;
they do not signal EOF. Empty writes similarly return `ok`. These empty
operations still check availability, ownership and open state, but ignore
deadline expiry. Other operations on a closed handle report `closed`.

Directory enumeration fails as a whole if any name cannot be represented as
UTF-8. That error includes `kind => invalid_data` and
`reason => unrepresentable_name`; entries are never silently omitted.

## Errors and deadlines

Every error map contains `kind` and `service => file`. Native errors retain
`reason` and `native => #{domain => posix, code => Errno}`. Kinds are
`unavailable`, `timeout`, `not_owner`, `closed`, `not_found`,
`permission_denied`, `invalid_data`, `busy` and `io`. Branch on these fields
rather than diagnostic text.

| Native error | `kind` |
| --- | --- |
| `enoent` | `not_found` |
| `eacces`, `eperm` | `permission_denied` |
| `einval` | `invalid_data` |
| `ebadf` | `closed` |
| `ealready` | `not_owner` |
| `etimedout` | `timeout` |
| `ebusy`, `eexist` | `busy` |
| Other errors, including `enomem` | `io`, retaining the native code |

File cancellation is cooperative. Work checks the deadline before native
dirty-I/O execution, syscalls and retries, and waits for an active syscall to settle
before returning. A complete single-syscall result wins over later expiry.
Multi-step work can time out after partial effects. There is no hard bound on
return time and no rollback of creation, truncation, accepted writes or metadata
changes. An open error after native admission conservatively reports
`effects => possible`; `write_file/3` failures after opening do so too.
Timeout does not leave background work using caller-owned buffers. There is no
pending-operation handle or separate cancellation API.

Every write failure includes `bytes_written` and `progress_unknown`. File writes
report the confirmed accepted byte count across native chunks with known
progress. Admission and ownership failures report zero confirmed bytes.
Acceptance does
not promise durable storage. Retry only an unaccepted suffix when the handle
remains usable; repeating `write_file/3` opens and truncates again.

Close validates its deadline but performs cleanup even after expiry. A reported
close error is not permission to retry an OS descriptor number: on Linux that
descriptor is consumed even when close reports an error. The handle records
logical closure and retains any remaining reclamation obligation. Whole-file
helpers report a cleanup failure or attach it as `cleanup` without replacing a
primary operation error.

## Scope and implementation sources

The modern facade currently has no append mode, seeking, ownership transfer,
recursive directory creation or atomic-write helper. It does not promise the
complete Erlang/OTP `file` API. File and socket handles share some conventions;
their capabilities remain separately selected.

The current implementation is defined by
[beam_file.erl](../../../src/beam/file/beam_file.erl) and the
[file library recipe](../../../src/beam/file/CMakeLists.txt).
