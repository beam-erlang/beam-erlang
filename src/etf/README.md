# Standalone ETF implementation

The codec is extracted from `lib/erl_interface` in Erlang/OTP 29.0,
commit `550d7b7898706c7822362c42e7b93120c1d1f29a`. Encode/decode files retain
upstream filenames and notices; `cmp_nc.c`, `get_type.c` and `putget.h` come
from `src/misc`, and `../beam_etf.c` contains selected `ei_x_encode.c` bodies
and the UTF-8 atom facade. Public representations derive from `include/ei.h`.

`cmake/ETF.cmake` compiles the same sources twice. `namespace.h` maps public and
private helpers to the embedded namespace for SDK incorporation. `compat.c`
is included only in that payload. The normal runtime exports only the selected
modern entry points; its internal encoding-selection and legacy big-integer
bodies remain hidden. Ordinary C allocation is intentional: no source in this
directory may acquire a VM, socket or thread dependency.

Preserve upstream codec algorithms and review semantic corrections separately
from ownership moves. CMake selects the original `EI_64BIT` mode when `long` is
eight bytes. OTP's adapters, formatting, C-node support and legacy allocation/
arithmetic helpers remain OTP-owned. The host-memory codec does not replace
ERTS runtime-term serialization.
