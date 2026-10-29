# Migrating optional host interfaces

These changes affect experimental SDK consumers. Rebuild against one selected,
matching SDK; there are no binary forwarding shims for moved functions.

## Returning hosts

Include `<beam/boot.h>` explicitly and link `BEAM::Host` in addition to the chosen
runtime. `<beam/beam.h>` includes VM declarations only. Returning preparation,
execution, discard and outcome release are no longer exported by either VM
image.

```cmake
find_package(BEAM CONFIG REQUIRED COMPONENTS Runtime Host)
target_link_libraries(my_host PRIVATE BEAM::Host BEAM::Runtime)
```

`BEAM::Host` is a static archive without a forced runtime linkage choice.
Runtime-only clients need neither it nor its headers and authoring helper.

## Native owners and individual bindings

When acquisition produces native pointers, supply `beam_native_owner` records
with the composition. Preparation retains them before storing native pointers;
VMs retain their preparations. The owner releases native code only after the
last preparation reference and all VM resource/registry reclamation. An
executable can instead provide resident code under the documented borrowed
lifetime contract.

For explicit NIF selections, `beam_nif_binding` records distinguish normalized
resident accessors from platform initializer signatures. Invoke
`beam_vm_bind_nif(vm, module, error)` after start. Successful repeated binding is
idempotent; initialization failure stops the VM. Use resident registrations and
ordinary Erlang `load_nif/2` when actual Erlang load information is required.

See [passive libraries](../native-extensions/libraries.md) for containers and
the installed authoring helper.

## Staged process-owned entry

`beam_vm_start` now also accepts process-owned VMs and suspends their initial
root. Use synchronous calls for setup and bind declared native selections before
calling `beam_vm_enter`. Entry rejects unfinished setup/bindings, pending work
and stopped state. Process policy and ordinary process-lifetime native
permissions become effective at entry commit. The initialized-to-entry
convenience path remains available.

## Failure reporting

Replace `BEAM_TERMINAL_COMPONENT_FAILURE` with `BEAM_TERMINAL_FAILURE` and
`beam_vm_report.component` with `beam_vm_report.origin`. Origin is a copied
diagnostic identity, independent of any fixed facility inventory.

Native monitors can call `beam_native_request_failure` without waiting for a
host callback. Native cleanup calls `beam_native_report_cleanup`; its first
diagnostic remains separate from the first terminal cause. These functions
require the active VM's native lifetime and do not support detached threads
after termination.

## Removed experimental composition policy

Remove `beam_component_kind`, `beam_component_spec`, component state queries,
`beam_service`/`BEAM_SERVICE_*`, `beam_vm_activate` and aggregate activation queries.
These interfaces have no compatibility shims. Acquire passive libraries through
`beam_library_open` or `beam_library_from_static`, then merge their inputs with
ordinary generic composition or `beam_host_prepare`.

Replace path-based `beam_nif_spec` and `beam_linked_nif_spec` with acquired or
linked `beam_nif_binding` records and explicit `beam_vm_bind_nif` calls.
Replace composition activation inventories with Host deferred modules and
explicit startup actions. Replace returning options' component/service/NIF fields
with `host`; rename `beam_return_module.service` to `deferred`.
Use `BEAM_OUTCOME_FAILURE` and `beam_outcome.origin` for returning failures.
Select `BEAM_NATIVE_MANAGED_PORTS` and an exact helper path for managed programs.

File, network and zlib now export both shared and static SDK targets:
`BEAM::File`, `BEAM::Network`, `BEAM::Zlib`, and their `Static` variants.
Static library targets leave runtime linkage to the final host.
