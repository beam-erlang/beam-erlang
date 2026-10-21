# Native components

BEAM/Erlang supplies three optional shared libraries that add Erlang modules and
their native implementations to a VM. Each library has its own guide and module
reference:

| Library | Capability | Erlang interface |
| --- | --- | --- |
| [libbeam-file.so](file/README.md) | Files and directories | `beam_file` |
| [libbeam-network.so](network/README.md) | Numeric-address TCP sockets | `beam_socket` |
| [libbeam-zlib.so](zlib/README.md) | In-memory compression and decompression | `zlib` |

The three components can be selected independently. Their presence in an
installation does not activate them. Selection applies to the VM, not to an
individual Erlang process, and is not a per-process security sandbox.

## Selection and activation

A C host links to `BEAM::Runtime` and supplies an exact component path in a
`beam_component_spec`. These libraries must come from the same SDK build as the
host runtime. Their descriptors are a private matching-build contract; they are
not a public interface for implementing additional component kinds. Static
runtime hosts cannot acquire these shared components.

| Component kind | Corresponding service bit | Typical installed path |
| --- | --- | --- |
| `BEAM_COMPONENT_FILE` | `BEAM_SERVICE_FILE` | `<prefix>/lib/libbeam-file.so` |
| `BEAM_COMPONENT_NETWORK` | `BEAM_SERVICE_SOCKET` | `<prefix>/lib/libbeam-network.so` |
| `BEAM_COMPONENT_ZLIB` | `BEAM_SERVICE_ZLIB` | `<prefix>/lib/libbeam-zlib.so` |

For a persistent host:

1. Acquire and verify ordinary bytecode inputs. Include the complete four-module
   services inventory from `lib/beam/services`; include the base library when
   using its operations or the `beam_file` and `beam_socket` facades.
2. Prepare a composition with `beam_composition_prepare`. A selected native
   component implies its corresponding service bit. Requesting that service bit
   without its component is invalid.
3. Initialize the VM with that composition, load any deferred ordinary modules,
   and start it.
4. Call `beam_vm_activate` before calling application exports. Activation starts
   the selected native facilities and required owners, and waits for readiness.
5. Stop and terminate the VM to reclaim its resources before creating another.

Returning hosts use `beam_return_prepare` and `beam_return_run`. They supply the
complete base, boot and services inventories, and their native component list
must exactly match the requested native service bits. The returning helper
performs activation before entry and teardown before returning its copied
outcome. See the [C API guide](../c-api/guide.md) and
[deployment guide](../c-api/deployment.md) for host and bundle construction.

## Code and resource lifetime

Preparation checks component identity, conflicts and module containers, copies
the inputs, and retains library mappings. It neither starts a VM nor activates
NIFs. Native library constructors can run during mapping, so components are
trusted native code. Full executable-code validation belongs to VM
initialization/loading.

Initialization retains its own reference to a successful preparation. Releasing
the host's preparation does not release code still needed by that VM. Module
origins identify the canonical component library path followed by `#<module>`.
An origin is provenance, not an instruction to acquire more code.

Omitting a component omits its whole module inventory. Calling an absent module
raises `undef`; the VM does not search an installed directory to rescue the
call. Loading an Erlang wrapper by itself does not grant a native capability.
There is no component hot replacement or rebinding during a VM lifetime.

`beam_vm_get_component` observes a selected component's state;
`beam_vm_get_activation` observes aggregate activation. Failed activation stops
the VM and retains diagnostics. Terminate that VM before retrying with a fresh
one. Required service-owner failure likewise closes availability and stops the
VM. Termination retains the mappings until native resources and code have been
reclaimed. Exact state and reporting contracts are in the
[C API reference](../c-api/reference.md).

Output and code acquisition are host-backed facilities documented with the
[standard-library service interfaces](../stdlib/services.md). They do not require
additional `libbeam-<name>.so` files. External program execution is another
explicit service with its own helper selection; it is not provided by the file
or network component.
