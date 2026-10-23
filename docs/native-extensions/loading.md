# Loading and lifetime

The native loading design is provisional and under review. This page describes
the current implementation so consumers can use the selected SDK. It does not
freeze explicit activation as the final design or decide how `on_load` should
work in future releases. The [NIF](nif.md) and [driver](drivers.md) references
describe native operations and callbacks; this page owns their current loading
and lifetime policy.

## Choose the host route

| Route | Current use |
| --- | --- |
| Explicit external NIF binding | A host supplies wrapper bytecode and an absolute library path in composition, then activates the binding. |
| Already-linked NIF or driver | A product supplies matching-build descriptors and resident initializer functions through composition. |
| Ordinary native admission | A process-owned product enables selected NIF, driver, port or distribution mechanisms and supplies its own loading services. |

These are host choices. The modern shell does not acquire OTP's code server,
`erl_ddll` or application services to satisfy a native loading request. The
separate OTP product owns its loading services and startup policy.

## Explicit external NIF bindings

`beam_nif_spec` supplies the module name and an absolute UTF-8 path to the
library. The wrapper module must be among the ordinary modules supplied to
composition. Preparation copies the inputs, rejects duplicates and conflicts,
and checks the exported SDK build descriptor and initializer. Descriptor
validation is separate from package provenance. Build the library with the
installed `beam_target_nif` helper.

Preparation can map trusted shared libraries, whose platform constructors may
run. It neither starts a VM nor invokes the NIF load callback. The host assigns
the prepared composition to its VM, starts it and uses `beam_vm_activate` to
activate selected native bindings. NIF-only activation requires no modern
service-support modules. Compositions selecting modern services or deferred
activation modules have the support requirements listed in the
[composition reference](../c-api/reference.md#composition).

The installed [external-NIF host](../../tests/installed/runtime/nifs/main.c)
and [CMake recipe](../../tests/installed/runtime/nifs/CMakeLists.txt) are the
runnable sources for this route. Their failure cases also show rejected build
identities, malformed native entries and cleanup after partial activation.

## Load information and module callbacks

For an explicit external binding, activation currently passes `undefined` as
`load_info` and does not execute the wrapper's `on_load` function. Ordinary and
preload initialization currently reject modules carrying `on_load` without
executing that callback. A separate reviewed activation inventory can supply
deferred modules, including `on_load` modules, for execution after native
facilities become ready.

These distinctions describe the current routes; they are not a decision to
permanently reject `on_load`-based loading. In a product using the ordinary
`erlang:load_nif/2` path, the second argument supplies `load_info`. That product
owns the wrapper loading and callback policy.

## Already-linked native code

`beam_builtin_nif` and `beam_builtin_driver` supply a name, matching `build_id`,
typed initializer and taint flag. The initializer returns an `ErlNifEntry` or
`ErlDrvEntry` table. Native initializers and the implementations they select
must remain resident through VM termination.

Preparation copies descriptor metadata and strings and validates identities
and conflicts without calling the initializer. VM initialization incorporates
the selection into its ordinary static registries; NIF load callbacks still
run at ordinary load time. Native-entry validation, taint reporting and the
ordinary permanent-driver/static-NIF rules still apply. Registry state is
reclaimed at termination, allowing
a later fresh VM to select different implementations. Selection is per VM;
there is no general global registration or hot replacement operation in the
composition API.

The [built-in fixture](../../tests/installed/runtime/nifs/builtins.c) shows the
public initializer types and repeated VM lifetimes. Driver selection also
requires driver admission.

## Ordinary native admission

`native_admission` independently enables `BEAM_NATIVE_NIFS`,
`BEAM_NATIVE_DRIVERS`, `BEAM_NATIVE_PORTS` and `BEAM_NATIVE_DISTRIBUTION`.
The current implementation restricts these permissions to process-owned VMs.
They select no modern base code, components or loading services by themselves.
An ordinary native port composition can provide a program helper without
selecting modern services; external-program execution still needs that helper.

The VM rejects an ordinary native mapping before its initializer runs if its
dependency closure resolves `beam_build_id` to another runtime image, including
a second copy of the same build. Extensions must resolve symbols from their
host runtime rather than link another runtime into the process.

## Cleanup and upgrades

An explicit external NIF binding currently lasts until VM termination.
Unloading, rebinding and native hot replacement are unsupported for this route.
Resource destruction and successful unload callbacks precede release of the
native mapping. A failed activation stops the partially activated VM; terminate
it before retrying with a fresh VM.

The NIF reference describes resource takeover and upgrade callbacks supported
by the underlying VM interface. Those callback definitions do not grant a
host an upgrade route. Ordinary process-owned products own their module and
driver loading policy; preserve the distinction when using that reference.

Native allocations, threads and resources must have owners that can complete
cleanup. Repeated-lifetime acceptance observes real resources as well as VM
state. Keep callback code resident while its resources can still call it.

See the [C lifecycle reference](../c-api/reference.md#system-and-vm-lifecycle)
for stopping and terminating the VM, and [SDK deployment](../c-api/deployment.md)
for packaging the selected native artifacts.
