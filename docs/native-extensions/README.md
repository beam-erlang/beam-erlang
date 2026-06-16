# Native extensions

Native extensions implement Erlang functions or port callbacks in C or C++.
They use the installed NIF and driver headers and execute inside the VM process.
The native API is different from the `beam/beam.h` embedding API: an embedding host
controls the VM, while an extension runs when that VM invokes it.

| Manual | Scope |
| --- | --- |
| [NIF API](nif.md) | Terms, environments, resources, native functions, scheduling and callbacks. |
| [Driver API](drivers.md) | Port output, queues, asynchronous work, synchronization and driver utilities. |
| [Driver entry](driver-entry.md) | The `ErlDrvEntry` structure and callback contracts. |
| [Loading and lifetime](loading.md) | Current host selection, activation, native admission and cleanup; this design remains under review. |

Use a selected installed SDK. `BEAM::NIF` and `BEAM::Driver` expose its public
headers; [SDK deployment](../c-api/deployment.md#native-headers-and-bindings)
explains the CMake helpers. Native code must use the running VM's symbols.
Source-level API descriptions and historical version notes do not establish a
stable binary interface for this experimental distribution.

The references retain the useful C API material from the upstream ERTS manuals,
with their copyright and license notices. Their descriptions include VM
mechanisms also used by the separate Erlang/OTP product. An API entry does not
make OTP loading services or every VM mechanism available to a modern host;
read the loading page for the current boundary.

## Read a working consumer

The [installed native acceptance fixture](../../tests/installed/runtime/nifs/)
owns a complete NIF wrapper, native implementation, CMake build and C host.
Its [main host](../../tests/installed/runtime/nifs/main.c) exercises external
bindings, and its [built-in host](../../tests/installed/runtime/nifs/builtins.c)
selects already-linked NIFs and a driver. The fixture also includes intentional
invalid inputs and failure cases. Use its successful path to understand the
integration, and its [runner](../../tests/installed/runtime/nifs/runner.py) to
see the lifecycle checks.

For the driver callback shape, the small
[driver probe](../../tests/installed/sdk/runtime_artifacts/driver_probe.c)
uses the public header to return data through a port. Its accompanying Erlang
loader uses OTP's `erl_ddll` service and is not a modern-shell tutorial.

Native code can corrupt or terminate the process. Keep scheduler callbacks
short, respect the documented thread and term-environment rules, and give
every native allocation and resource an explicit cleanup owner.

[Return to the documentation index](../README.md).
