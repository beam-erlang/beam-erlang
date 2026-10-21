# libbeam-zlib.so

The zlib component supplies the `zlib` Erlang module and its native compression
implementation. It supports zlib-wrapped, raw DEFLATE and gzip data. It operates
on in-memory bytes and can be selected without file or network access.

## Select the capability

Select `BEAM_COMPONENT_ZLIB` with the exact path to
`<prefix>/lib/libbeam-zlib.so`. Its corresponding service bit is
`BEAM_SERVICE_ZLIB`. Supply the ordinary services inventory and follow the
[component lifecycle](../README.md#selection-and-activation). A returning host
also supplies its complete base and boot inventories.

VM activation binds the native implementation before application calls. Do not
call the module's internal `on_load` exports as an activation API. Omitting the
component leaves the `zlib` module absent. The VM's own compressed-bytecode and
external-term support remains a separate internal use of compression.

## Compress a value

With the component active:

```erlang
Compressed = zlib:gzip(<<"hello, BEAM/Erlang">>),
<<"hello, BEAM/Erlang">> = zlib:gunzip(Compressed).
```

Whole-input helpers accept `iodata()` and return a binary. They manage their
own stream lifetime, including closing on failure.

| Function pair | Format |
| --- | --- |
| `compress(Data)` / `uncompress(Data)` | zlib headers and checksum |
| `zip(Data)` / `unzip(Data)` | Raw DEFLATE, without zlib headers or checksum |
| `gzip(Data)` / `gunzip(Data)` | gzip headers and checksum |

`zip/1` does not create a ZIP archive. Archive structure and file acquisition
are separate concerns.

## Stream reference

`open/0` returns an opaque stream reference controlled by the calling process.
Initialize a compression or decompression session before processing data.
Finish/end that session, then close the stream:

```erlang
Z = zlib:open(),
try
    ok = zlib:deflateInit(Z),
    First = zlib:deflate(Z, <<"hello, ">>),
    Last = zlib:deflate(Z, <<"BEAM/Erlang">>, finish),
    ok = zlib:deflateEnd(Z),
    erlang:iolist_to_binary([First, Last])
after
    zlib:close(Z)
end.
```

| Function | Purpose and result |
| --- | --- |
| `open()` | New stream reference. |
| `close(Z)` | Close the stream; returns `ok`. Closing again raises `not_initialized`. |
| `set_controlling_process(Z, Pid)` | Transfer control to a local process; returns `ok`. Only the current controller can do this. |
| `deflateInit(Z)` / `deflateInit(Z, Level)` | Start compression; returns `ok`. Default level is `default`. |
| `deflateInit(Z, Level, deflated, WindowBits, MemLevel, Strategy)` | Configure a compression session; returns `ok`. |
| `deflate(Z, Data)` / `deflate(Z, Data, Flush)` | Compress iodata and return an iolist. Default flush is `none`; use `finish` for the final input. |
| `deflateEnd(Z)` | End the session; returns `ok`. An unfinished stream can raise `data_error`. |
| `deflateReset(Z)` | Reset compression while keeping the session settings; returns `ok`. |
| `deflateParams(Z, Level, Strategy)` | Update compression settings; returns `ok`. Buffered output is retained for a subsequent deflate call. |
| `deflateSetDictionary(Z, Dictionary)` | Set a dictionary immediately after initialization/reset and before input; returns its Adler checksum. |
| `inflateInit(Z)` / `inflateInit(Z, WindowBits)` | Start decompression; returns `ok`. Default window bits are 15. |
| `inflate(Z, Data)` / `inflate(Z, Data, Options)` | Decompress iodata; normally returns an iolist. See dictionary handling below. |
| `safeInflate(Z, Data)` | Produce a bounded chunk of output: `{continue, Output}`, `{finished, Output}` or `{need_dictionary, Adler, Output}`. |
| `inflateEnd(Z)` | End decompression; returns `ok`. Missing end-of-stream can raise `data_error`. |
| `inflateReset(Z)` | Reset decompression with the existing settings; returns `ok`. |
| `inflateSetDictionary(Z, Dictionary)` | Supply a requested dictionary; returns `ok`. Resume with empty input. |
| `inflateGetDictionary(Z)` | Return the current dictionary as a binary. |

Compression levels are `none`, `default`, `best_speed`, `best_compression` or
integers 0–9. Strategies are `default`, `filtered`, `huffman_only` or `rle`;
memory levels are 1–9. Flush modes are `none`, `sync`, `full` and `finish`.
Window bits select the window and wrapper: ordinary zlib uses 8–15, negative
values select raw DEFLATE, and gzip commonly uses 31. The retained source
[module documentation](../../../src/beam/zlib/zlib.erl) gives the detailed
parameter rules. The exported `inflateInit/3` end-of-stream-policy helper is
internal support, not part of this candidate public guide.

`inflate/3` accepts `[{exception_on_need_dict, false}]` to return
`{need_dictionary, Adler, Output}` instead of raising. Select the matching
dictionary, call `inflateSetDictionary/2`, and resume with empty input so the
queued input is not submitted twice.

For large or untrusted input, process `safeInflate/2` output incrementally and
enforce an application limit on total decompressed bytes. Drain `continue`
results using empty input. `finished` means queued input has been processed;
it does not by itself prove that the complete compressed stream was supplied.
`inflateEnd/1` checks stream completion. Chunking does not impose a total
decompression-size limit automatically.

## Errors and lifetime

This module retains its exception-based interface; it does not use the modern
file/socket error maps or deadline arguments. Errors include `badarg`,
`not_initialized`, `not_on_controlling_process`, `data_error`, `stream_error`
and dictionary requests. Wrapper functions may translate a native error; in
particular, `uncompress/1` reports caught failures as `badarg`.

Using a stream from another process requires an explicit control transfer.
Closing releases its active compression state; resource finalization releases
the remaining native allocation. VM termination finalizes retained resources
before releasing the component mapping. Use explicit close for prompt cleanup
during a running VM.

The implementation and embedded module inventory are defined by
[zlib.erl](../../../src/beam/zlib/zlib.erl),
[zlib_nif.c](../../../src/beam/zlib/zlib_nif.c) and the
[zlib component recipe](../../../src/beam/zlib/CMakeLists.txt).
