# Host-memory ETF codec

Include `<beam/beam_etf.h>` and link `BEAM::Runtime` or `BEAM::RuntimeStatic` to
encode and decode Erlang External Term Format (ETF) in C or C++. The codec uses
ordinary C allocation and has no system or VM initialization requirement.
Encoding can precede `beam_init`; buffers, copied binary output and decoded funs
remain usable after VM and system termination while the library remains loaded.
This API neither creates runtime terms nor grants distribution capabilities.

This is an experimental, exact-SDK interface. Its implementation is extracted
from the Erl_Interface codec in OTP 29.0; the upstream notices remain with the
sources. OTP retains its independent `ei.h` interface and compatibility behavior.
The VM's runtime-term serialization machinery is separate.

## Buffers, cursors and errors

Raw operations take a buffer and an `int *index` byte offset. Pass a null output
buffer to an encoder to measure the required size, then reset the cursor before
encoding into adequately sized storage. Growable operations take a
`BeamEtfBuffer` with `buff`, `buffsz` and `index`; initialize it with
`beam_etf_x_new` or `beam_etf_x_new_with_version` and release it with
`beam_etf_x_free`. `beam_etf_x_append` and `beam_etf_x_append_buf` copy bytes.
Version encoding is explicit except in `x_new_with_version`.

```c
BeamEtfBuffer encoded;
if (beam_etf_x_new_with_version(&encoded) != 0)
    return -1;
if (beam_etf_x_encode_tuple_header(&encoded, 2) != 0 ||
    beam_etf_x_encode_atom(&encoded, "ok") != 0 ||
    beam_etf_x_encode_long(&encoded, 42) != 0) {
    beam_etf_x_free(&encoded);
    return -1;
}
/* encoded.buff[0..encoded.index) contains {ok,42} with the ETF version. */
beam_etf_x_free(&encoded);
```

Most operations return `0` on success and `-1` on failure; comparisons return
negative, zero or positive. There is no error object or global initialization
API. This first interface preserves upstream cursor and allocation behavior;
errors do not promise unchanged output or transactional cursor updates. In
particular, growth assigns the result of `realloc` to `buff` and updates `buffsz`
before testing for failure. Preserve the previous allocation pointer separately
if recovery from failed growth is necessary. This behavior is retained pending a
separate allocation/error-contract redesign.

**This is not a bounded parser.** Decoders require complete readable ETF input,
including all nested terms, and raw encoders require sufficient output space.
The cursor is not a capacity. `get_type` and `skip_term` inspect or traverse data;
they do not establish complete validity. Do not use them to validate untrusted
or truncated input. Output capacity is explicit only for atom decoding.

## Operations and representations

All names below have the `beam_etf_` prefix. Encoders generally have an `x_`
variant for growable buffers; declarations in the
[public header](../../include/beam/beam_etf.h) give the exact C types.

| Group | Operations and contract |
| --- | --- |
| Scalars | `encode/decode_long`, `ulong`, `longlong`, `ulonglong`, `double`, `boolean`, `char`. Preserve C scalar types; `char` represents an integer byte, not a Unicode character. |
| Atoms | `encode_atom`, `encode_atom_len` and growable equivalents accept UTF-8. They reject overlong names. `decode_atom` writes UTF-8 and takes destination capacity including the trailing NUL; Latin-1 ETF atoms are converted. There is no modern encoding-selection parameter. |
| Byte data | String operations represent lists of bytes, including explicit lengths and embedded NULs. `decode_binary` copies bytes into caller storage. `decode_bitstring` returns a borrowed view into the input with bit offset and length; keep the input alive. `decode_iodata` measures or flattens iodata. |
| Containers | Tuple, map and list headers describe sequential children. Map arity counts pairs. A list header is followed by its elements **and its tail**; encode an empty list for a proper tail. |
| Identities | PID, port and reference operations use `BeamEtfPid`, `BeamEtfPort`, `BeamEtfRef`; `cmp_pids`, `cmp_ports`, `cmp_refs` compare serialized identities. No runtime entity or network connection is created. |
| Funs | `encode_fun`, `decode_fun`, `free_fun` use `BeamEtfFun`. Closure environments own copied bytes; exported function names can own an allocation or point into the structure's module array. Do not shallow-copy decoded funs. Release successful decoded values exactly once with `beam_etf_free_fun`. |
| Inspection | `get_type` leaves the cursor unchanged. `skip_term` advances over a term without constructing a runtime value. |

Legacy big-integer structures, GMP, formatting, printing, partial `ei_term`
decoding, trace tokens, compatibility modes, initialization and C-node/networking
operations are not part of this modern API.

## Incorporation into a standalone library

A distribution that must supply its own standalone compatibility archive can
request the separate `ETF` SDK component:

```cmake
find_package(BEAM CONFIG REQUIRED COMPONENTS ETF)
add_library(compatibility STATIC adapters.c)
beam_target_etf_objects(compatibility)
```

The helper incorporates the PIC members of `BEAM::ETFObjects` into the archive
and supplies include/system-library usage. The installed payload is built from
the same codec sources using `beam_etf_embed_*` names, so an archive can coexist
with either form of `libbeam`, including whole-archive static runtime linkage.
It contains no VM image, native runtime headers, threads, sockets or resolver
initialization. ETF component discovery works without a runtime binary;
runtime-only discovery does not require the object payload.

`<beam/sdk/etf.h>` declares the embedded namespace and five compatibility bridge
operations: explicit-encoding atom encoding/decoding, legacy big-integer
encoding/decoding and buffer growth with a caller-selected extra capacity.
`BeamEtfCompatBig` retains byte arity, sign and native unsigned-short digits.
The caller owns allocation and arithmetic. `beam_etf_compat_x_fix_buff` returns
nonzero on success and preserves upstream pointer/capacity/cursor updates on
failure. The bridge and embedded symbols are absent from the runtime's public
export surface.

These are incorporation interfaces, not stable cross-version ABIs. The artifact
catalog binds objects and link metadata to the selected SDK identity and hashes;
use the helper after relocation as well. Object paths contain the SDK identity,
and adapter compilation records it, so selecting a replacement SDK rebuilds the
archive even when installed inputs preserve older timestamps.
Objects and adapters must share a C
allocation domain. A generated header checks the actual Microsoft CRT selection
when compiling the incorporating archive, including static/dynamic and
debug/release differences. Select the same runtime configuration as the SDK.
Consumers of OTP's
installed `libei` archives need only OTP headers and the usual system link flags;
they need no BEAM headers, VM library or additional codec archive.
