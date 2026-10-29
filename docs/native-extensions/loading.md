# Loading and lifetime

The native loading design is provisional and under review. This page describes
the current implementation so consumers can use the selected SDK. It does not
freeze the experimental interfaces or decide how `on_load` should
work in future releases. The [NIF](nif.md) and [driver](drivers.md) references
describe native operations and callbacks; this page owns their current loading
and lifetime policy.

## Choose the host route

| Route | Initializer and load timing |
| --- | --- |
| Resident registrations | Preparation copies descriptors; VM initialization obtains/validates entry tables; the owning Erlang module later invokes `erlang:load_nif/2` with actual load information. |
| Explicit native bindings | Host acquisition produces a `beam_nif_binding` and owner. `beam_vm_bind_nif` invokes the initializer after start, using `undefined` load information. |
| Ordinary native admission | A process-owned product supplies its own loading code and explicitly authorizes ordinary NIF, driver, port or distribution operations at entry. |

The modern shell does not acquire OTP startup or loading services implicitly.
The separate OTP product owns its loading policy. Passive [libraries](libraries.md)
are optional containers above the VM and normally supply resident registrations.

## Explicit native bindings

Use `beam_host_open_nif` from the optional Host target to acquire an external NIF,
or supply a linked initializer directly. Both produce generic `beam_nif_binding`
records for composition. The wrapper must be a supplied ordinary module; identities
and native registrations must not conflict. Acquisition checks the image and its
SDK stamp without invoking its NIF initializer. Mapping trusted code can run
platform constructors.

After starting the VM, call `beam_vm_bind_nif` for each declared binding. Successful
repeated binding is idempotent. Invalid arguments and missing module code leave
state unchanged. Once native initialization begins, a failure stops the VM and
preserves its diagnostics. Terminate that VM before retrying. No product support
inventory is required for explicit binding.

`beam_target_nif(target MODULE module)` generates static registration and explicit
binding accessors. Referencing the accessor retains its native implementation
through static linking. Shared and static runtime hosts support resident code;
a static VM rejects a modern shared library that depends on a runtime DSO.
Ordinary OTP dynamic NIFs remain supported under ordinary admission.

The [external-NIF fixture](../../tests/installed/runtime/nifs/main.c) and
[built-in fixture](../../tests/installed/runtime/nifs/builtins.c) exercise these
routes, including conflicts, delayed initialization and failed binding cleanup.

## Load information and module callbacks

Explicit binding passes `undefined` as `load_info` and does not execute a wrapper
`on_load`. Resident registrations preserve ordinary `erlang:load_nif/2` semantics:
file startup passes the real janitor PID, socket startup passes registry/configuration
information, and zlib loads directly without an artificial owner process.

Ordinary and preload initialization reject modules carrying `on_load` without
executing that callback. The optional Host layer accepts an explicit deferred
inventory and preserves reviewed initialization/rollback after requested startup.
This migration does not broaden ordinary module-loading or `on_load` behavior.

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
These permissions take effect at entry commit and select no library or startup code.
`BEAM_NATIVE_MANAGED_PORTS` separately permits returning-mode ports/programs,
with direct-child termination and reaping. It grants no dynamic NIF, driver or
distribution permission. Program execution requires an explicitly selected helper.

The VM rejects an ordinary native mapping before its initializer runs if its
dependency closure resolves `beam_build_id` to another runtime image, including
a second copy of the same build. Extensions must resolve symbols from their
host runtime rather than link another runtime into the process.

On Linux/musl, inspection happens before mapping: the runtime reads ELF program
headers, dynamic symbols and the transitive dependency closure in process. A
runtime image is identified by its exported identity symbol, including when it
has another filename. Only the actual shared runtime already used by the host
is permitted; a static host cannot acquire a runtime DSO. Unsupported or
unresolvable images fail before their constructors or native initializers run.
No external inspection program or child runtime is used.

The Alpine 3.24 implementation follows musl 1.2.6: resident providers, explicit
paths, process-start `LD_LIBRARY_PATH`, the requesting object's `$ORIGIN` search
paths and their ancestry, then `ld-musl-ARCH.path` or the loader's default
directories. Both ELF symbol-hash styles and objects without section headers
are supported. Unknown search substitutions or ambiguous metadata are rejected.

Native files and loader-search configuration must remain stable for the host
process lifetime. musl captures environment search paths at process startup and
caches its system search configuration. Changing those inputs after startup or
replacing libraries concurrently is outside this loading contract. Inspection
rejects observable changes during admission. Secure execution and invoking musl's
loader directly as a command are unsupported inspection contexts.

## Cleanup and upgrades

An explicit external NIF binding currently lasts until VM termination.
Unloading, rebinding and native hot replacement are unsupported for this route.
Resource destruction and successful unload callbacks precede release of the
native mapping. A failed binding stops the VM; terminate
it before retrying with a fresh VM.

musl retains successfully loaded libraries until process exit: releasing a
mapping does not reset its globals or rerun its constructors. Per-VM state and
resources must therefore be initialized and released through the native lifecycle
callbacks. This applies equally to sequential fresh VM lifetimes.

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
