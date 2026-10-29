# In-memory zlib C interface

Include `<beam/zlib.h>` and link `BEAM::Runtime` or `BEAM::RuntimeStatic` to use
its namespaced compression interface. The VM and native callers share one
vendored codec. No system or VM initialization, optional library, file access
or network access is required. Use headers from the selected SDK; this interface
is experimental and does not promise a stable binary ABI.

The optional `libbeam-zlib` library supplies the Erlang `zlib` module and its
NIF bridge. C callers do not need that library. OTP supplies its own Erlang
module and native registration, using the same public C codec.

## Streams and allocation

Zero-initialize a `beam_zlib_stream`, then initialize, process and end it:

```c
#include <beam/zlib.h>

int compress_bytes(unsigned char *output, unsigned *output_size,
                   unsigned char *input, unsigned input_size)
{
    beam_zlib_stream stream = {0};
    int result = beam_zlib_deflate_init(&stream, BEAM_ZLIB_DEFAULT_COMPRESSION);
    if (result != BEAM_ZLIB_OK) return result;
    stream.next_in = input;
    stream.avail_in = input_size;
    stream.next_out = output;
    stream.avail_out = *output_size;
    result = beam_zlib_deflate(&stream, BEAM_ZLIB_FINISH);
    *output_size -= stream.avail_out;
    int ended = beam_zlib_deflate_end(&stream);
    if (result == BEAM_ZLIB_STREAM_END) return ended;
    return result == BEAM_ZLIB_OK ? BEAM_ZLIB_BUF_ERROR : result;
}
```

For incremental processing, refill input or drain output according to the
remaining `avail_in` and `avail_out` values. `next_in` and `msg` retain their
upstream pointer types; treat the referenced input and message bytes as read-only.
End every initialized stream, including on application errors. The caller owns
C-created streams; VM termination does not close them.

Null `zalloc` and `zfree` select ordinary codec allocation. Set both callbacks
and `opaque` before initialization to use a custom allocator. The callbacks and
allocator state must remain valid until the stream is ended. Independent streams
can operate concurrently with thread-safe allocation; serialize access to each
individual mutable stream. NIFs retain their own allocation and resource-finalizer
policy, including cleanup before their VM lifetime ends.

`beam_zlib_gzip_header` supports in-memory gzip metadata. Keep the header and
its referenced buffers alive while the stream may access them. It provides no
file handle or implicit file operations.

## Operations and compatibility

Names use `beam_zlib_` followed by the upstream operation in snake case:
`deflateInit2` becomes `beam_zlib_deflate_init2`. Constants use `BEAM_ZLIB_`
in place of upstream `Z_`. Operation results and streaming behavior follow the
selected zlib implementation.

| Group | Available operations |
| --- | --- |
| Information | `version`, `compile_flags` |
| Compression streams | `deflate`, initialization, end, reset, copy, dictionaries, parameters, tuning, bounds, pending/used bits, priming and gzip headers |
| Decompression streams | `inflate`, initialization, end, reset/reset2, copy, dictionaries, synchronization, priming, marks and gzip headers |
| Whole inputs | `compress`, `compress2`, `compress_bound`, `uncompress`, `uncompress2`, including their documented `_z` variants |
| Checksums | `adler32`, `crc32`, `_z` variants, combination operations and their documented `64` variants |

The installed header declares the exact supported signatures. File-oriented
`gz*` operations, `inflateBack*` and upstream's explicitly undocumented operations
are excluded. In-memory gzip processing uses the stream interface with window
bits 31; raw DEFLATE uses negative window bits.

Initialization macros pass `BEAM_ZLIB_API_VERSION` and the stream size to checked
entry points. A mismatch returns `BEAM_ZLIB_VERSION_ERROR` without modifying the
stream. `beam_zlib_version()` reports the codec version independently.

Stream counters and ordinary bounds retain upstream `unsigned` and
`unsigned long` widths, including Windows' 32-bit `unsigned long`. Documented
`_z` operations use `size_t`. Checksum-combine lengths use signed `int64_t`,
independent of filesystem feature macros. Public names coexist with system
`<zlib.h>`; streams from the two interfaces belong to their respective providers
and must not be exchanged.
