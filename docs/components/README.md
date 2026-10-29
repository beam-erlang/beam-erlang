# Native libraries

BEAM/Erlang supplies three passive libraries containing Erlang modules and their
native implementations. Each has shared and static delivery:

| Library | CMake targets | Erlang interface |
| --- | --- | --- |
| [File](file/README.md) | `BEAM::File`, `BEAM::FileStatic` | `beam_file` |
| [Network](network/README.md) | `BEAM::Network`, `BEAM::NetworkStatic` | `beam_socket` |
| [Zlib](zlib/README.md) | `BEAM::Zlib`, `BEAM::ZlibStatic` | `zlib` |

## Selection and startup

Include `<beam/library.h>` and link the optional `BEAM::Host` archive alongside
one selected runtime. Open an exact shared path with `beam_library_open`, or
include `<beam/libraries/file.h>` and pass `beam_file_library()` to
`beam_library_from_static` for static file delivery. Network and zlib have the
corresponding headers/accessors. Static library targets leave runtime linkage to
the host. Modern shared libraries require a shared VM; static libraries work with
either VM form. The modern CLI uses shared delivery.

Libraries are experimental exact-SDK containers. They contain no startup actions
or dependency resolver. Opening one creates no VM and calls no NIF initializer;
platform constructors may run when trusted native code is mapped. The
[documented format and authoring helper](../native-extensions/libraries.md) also
support independently authored libraries.

For a persistent host:

1. Acquire libraries and any ordinary bytecode needed by the application.
2. Merge their module/native inputs and owners into generic composition, or use
   `beam_host_prepare` to merge them with explicitly requested startup actions.
3. Initialize and start the VM. Resident entry tables are validated at
   initialization; their load callbacks have not yet run.
4. Invoke `beam_file:start/0`, `beam_socket:start/0` or `zlib:on_load/0` as selected.
   `beam_host_start` can perform these explicit actions and complete any explicit
   NIF bindings before application entry. It requires supplied `beam_startup`
   bytecode for its startup controller.
5. Stop and terminate to reclaim the VM before creating another.

Returning hosts put the same plan in `beam_return_options.host`; the optional
helper performs startup before entry and teardown before returning a copied
outcome. There are no service masks, fixed library kinds or aggregate VM
activation operation. Output and code acquisition are separate host policies;
program execution selects managed-port admission and a helper independently.

## Code and resource lifetime

Composition copies module bytes and retains native owners. The initialized VM
retains its preparation, so callers can release their library and preparation
references early. Native code remains resident until native resources, unload
callbacks and registries have been reclaimed. Static executable storage supplies
a borrowed lifetime. Shared-image provenance comes from native admission;
module origins are diagnostic labels, not acquisition instructions.

Selecting one library does not acquire another. Undefined calls remain undefined.
Library startup creates real file/socket owners, whose loss closes availability
and reports a generic terminal failure. Zlib needs no sentinel owner process.
There is no hot native replacement. Failed startup stops the VM; inspect copied
VM diagnostics and terminate before retrying with a fresh VM.
