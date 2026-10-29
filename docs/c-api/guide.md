# Embedding BEAM from C

Candidate user guide for the current experimental SDK.

BEAM/Erlang lets a C application run Erlang in its own process through
`libbeam.so`. The host chooses code and services, calls exported functions,
receives copied results and can terminate the VM while C continues running.
This guide describes the experimental Linux SDK; matching headers and libraries
are required, with no source or binary compatibility promise. Interpreter and
JIT expose the same C interface.

The [reference](reference.md) defines precise contracts, and
[SDK deployment](deployment.md) covers installed targets and application bundles.
Start with a bare VM below, then add a [composed lifecycle](#choose-a-composed-lifecycle)
when your application needs services.

## The execution model

The host is your C program. An Erlang process is a lightweight VM execution
context with its own state and mailbox, not a C thread or OS process. Modules
contain code shared by those processes; scheduler threads execute runnable work.

`beam_init` initializes the system on the main thread. For a dedicated host-controlled OS thread, initialize `beam_system_options`, select `BEAM_SYSTEM_CALLING_THREAD`, and use `beam_init_with_options` on that thread. Perform VM operations and cleanup serially on the selected owner thread. One VM handle may exist at a time. Terminating it permits a
fresh VM with different code, options and composition under the same system.
Concurrent VMs, cross-thread VM operations other than `beam_vm_wake`, and library unloading are unsupported.

A default VM loads nine core modules, then creates a private root and required
runtime helpers at explicit start. It does not start OTP applications, a code
server, compiler, automatic loader or I/O server. Load application dependencies
explicitly. Loading code and activating services are separate decisions.

## Build and run the first host

The self-contained [first-call project](../../tests/installed/sdk/consumer/first-call/)
contains an Erlang module, a complete C host with checked cleanup, and a CMake
recipe that embeds compiled module bytes. Its Erlang export is:

```erlang
%% SPDX-License-Identifier: Apache-2.0
-module(answer).
-export([value/0]).
value() -> 42.
```

`answer:value/0` names module `answer`, function `value`, arity zero. The host
chooses this export directly; embedding requires no `main` function in Erlang.

With a C99 compiler, CMake 3.25 or newer and an installed BEAM prefix, select the installed SDK explicitly:

```sh
export BEAM_PREFIX=/absolute/path/to/beam
cmake -S "$BEAM_PREFIX/share/doc/beam/examples/first-call" -B build/c-api-first-call \
    -DCMAKE_PREFIX_PATH="$BEAM_PREFIX"
cmake --build build/c-api-first-call
./build/c-api-first-call/first-call
```

The program prints `42` and exits successfully. Use `install/linux-jit` and a
separate consumer build directory for JIT. The project can also be copied
elsewhere; it needs no other repository sources. See the product
[build instructions](../../README.md#building) to create the SDK.

A normal host links the public runtime target:

```cmake
find_package(BEAM CONFIG REQUIRED)
add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE BEAM::Runtime)
```

The first-call recipe additionally invokes installed `BEAM::CLI` to compile
`answer.erl`, then converts the whole `.beam` file into a generated C array.
The executable reads no external Erlang modules and invokes no compiler at
runtime. Consumers requiring build-time compilation can explicitly request
`COMPONENTS CLI` in `find_package`; runtime-only discovery needs no CLI.

## Follow the call

Read the complete [main.c](../../tests/installed/sdk/consumer/first-call/main.c)
for error checks and cleanup. The important sequence is:

1. Initialize `beam_system *system = NULL`, `beam_vm *vm = NULL` and
   `beam_result result = {0}`. Call `beam_vm_options_init(&options)` before
   changing configuration.
2. Call `beam_init(&system, &error)`, then
   `beam_vm_init(system, &options, &vm, &error)`. Both return without VM workers
   or Erlang execution; VM initialization leaves `BEAM_VM_INITIALIZED`.
3. Pass the complete compiled module bytes to `beam_vm_load`. It copies them,
   does not acquire dependencies and executes no Erlang. Start explicitly with
   `beam_vm_start`; starting does not call an application function.
4. Call `beam_vm_call(vm, "answer", "value", arguments, &result)`. The owner
   thread waits for this ordinary Erlang entry process to complete.
5. Inspect API status, then the Erlang result kind. Release the result, stop
   and terminate the VM, then terminate the system after its last VM lifetime.

Arguments cross the C boundary as one copied, uncompressed External Term Format
(ETF) proper list, including the version byte. Each element is one argument.
For zero arguments the complete encoding is:

```c
const unsigned char no_arguments_data[] = {131, 106};
const beam_bytes arguments = {
    no_arguments_data, sizeof no_arguments_data
};
```

`131` is the version and `106` is the empty-list tag. An empty C buffer is not
a zero-argument encoding for `beam_vm_call`.

`BEAM_OK` reports a successful C operation, not necessarily an Erlang return:

| Result kind | Meaning |
| --- | --- |
| `BEAM_RETURN` | `value` holds one copied ETF value. |
| `BEAM_EXCEPTION` | `value` holds ETF `{Class, Reason, Stacktrace}`. |
| `BEAM_HALT` | The VM requested halt; read `halt_status`. |
| `BEAM_COMPLETE` | A host callback selected terminal completion; copied `value` and status describe it. |

The example expects `{131, 97, 42}`, the encoding of the small integer 42.
That check is a decoder for one shape, not a general term codec. Larger values
need appropriate ETF handling; consult the
[external term format reference](../reference/external-term-format.md).
The SDK currently supplies no general-purpose ETF convenience codec.

Release owned results with `beam_result_release`. Release the previous result
before reusing its structure: calls clear the output without freeing an earlier
allocation. Shallow copies do not duplicate ownership. Copied buffers survive
later calls, VM lifetimes and system termination while the library remains
loaded; encoded PIDs, references and funs do not preserve a terminated VM.

`beam_vm_stop` joins VM-owned activity but retains the handle and storage.
`beam_vm_terminate(&vm, ...)` stops if needed, reclaims the VM and clears the
handle. System termination rejects even a stopped VM until its handle is
terminated. A stopped VM cannot start again. Termination accepts a NULL handle
value for repeated cleanup, but its pointer argument must be non-NULL.

## Reuse a VM or start a fresh one

A live VM can serve many calls. An export can create a registered counter
process; later calls find it and exchange messages. Background Erlang processes
continue running while C is between calls. Calls have no timeout or asynchronous
cancellation API, and another C thread cannot stop a blocked owner-thread call.

A background halt can leave `BEAM_VM_HALT_REQUESTED` observable until an owner
operation completes joining. `beam_vm_get_state` only observes; it does not run
Erlang or finish cleanup. Ordinary integer halt returns control to a
host-controlled C host; crash-dump/abort and unsupported flush forms are rejected.

For fresh state, terminate VM A, then initialize VM B under the same system.
A's application code, atoms, registries, timers, callback state and halt status
do not become B's state. Keep one system lifetime for the host's embedding
activity; system reinitialization is unsupported.

Additional native controls are described in the [reference](reference.md):

| Operation | Use |
| --- | --- |
| `beam_query_host` | Observe CPU availability before initialization; it reserves no capacity or scheduler policy. |
| `beam_vm_get_module`, `beam_vm_list_modules` | Inspect code and obtain copied module-name snapshots. |
| `beam_vm_unload` | Remove unused code conservatively without killing processes; busy code remains intact. |
| `beam_vm_get_schedulers`, `beam_vm_set_schedulers_online` | Observe or change online participation within initialized capacity, without creating threads or pausing the VM. |
| `beam_vm_get_report`, `beam_vm_terminate_report` | Inspect a copied terminal cause and cleanup diagnostics, including the final report after reclamation. |

Erlang exports can return richer observations through ordinary calls, including
`system_info`, `statistics`, `memory` and selected `process_info`. Measurements
assembled separately are not an atomic snapshot; process disappearance and
scheduler cooperation remain relevant. VM-accounted memory is not host RSS.

## Supply host services and I/O

An Erlang service can request work from C through the optional `beam_host`
module. Embed its installed `BEAM_HOST_MODULE` bytes explicitly, then register
one handler with `beam_vm_set_host_handler(vm, handler, context)` before the
first call is admitted. The context remains host-owned until stop completes.

1. Erlang calls `beam_host:request(Request)` with any encodable term.
2. The handler receives copied ETF request bytes borrowed for that invocation.
3. It calls `beam_host_reply_copy(reply, response)` with one complete ETF term,
   then returns `BEAM_OK`. Reply bytes are copied immediately.
4. The Erlang bridge returns `{ok, Reply}` or a documented error such as
   `no_handler`, `no_reply`, `invalid_reply` or callback failure.

Callbacks run serially on the owner thread without VM locks. They must return
and cannot reenter VM operations or wait for another callback on that same
thread. Host-controlled synchronous calls pump callbacks while waiting. An
explicit `beam_vm_pump` also services requests between calls, including while no
call is active. Without either operation, requests remain queued. Stop cancels pending requests. Callback
side effects are not undone if a requester exits or the VM halts.

`beam_host_reply_complete` can select terminal completion instead of replying
to Erlang. The runtime applies it after callback return, stops the VM and
returns `BEAM_COMPLETE` if that completion wins arbitration. This is not
cross-thread cancellation or permission to stop the VM from inside a callback.

`io:format/2` needs formatting code and an I/O server selected as group leader.
Loading its module alone supplies neither. A host can explicitly load the
required modules, create a protocol adapter and forward supported output to C.
A small output adapter is not a full terminal service; unsupported operations
and missing formatting dependencies still need explicit handling. Modern
services and command profiles already define their own inventories and policy.

## Choose a composed lifecycle

The core VM, ordinary bytecode and native capabilities are separate inputs:

| Input | How the host supplies it | What it enables |
| --- | --- | --- |
| Nine core modules | Embedded in the selected runtime. | Runtime mechanisms and required helper processes. |
| Seven base modules | Verified `base` inventory through ordinary module inputs. | Modern collection, text, formatting and time functions. |
| Boot modules | Verified `boot` inventory through ordinary module inputs. | The returning application lifecycle. |
| Optional support | Select `startup`, `host`, `output` or `code` inventories through ordinary module inputs. | Explicit startup, host callbacks, output or code acquisition respectively. |
| File, network or zlib library | Explicit shared acquisition or a static descriptor. | Modules and resident native registrations; startup remains explicit. |

The nine core modules are `erlang`, `erts_internal`, `erts_code_purger`,
`erts_literal_area_collector`, `erts_dirty_process_signal_handler`,
`erts_trace_cleaner`, `atomics`, `counters` and `persistent_term`.
The base inventory is `beam`, `beam_list`, `beam_map`, `beam_bytes`, `beam_text`,
`beam_format` and `beam_time`. Boot contains `beam_return_boot` and `beam_boot`.
The single-module support inventories contain `beam_startup`, `beam_host`,
`beam_output` and `beam_code` respectively. The `services` inventory is a
convenience selection containing all four; it is not a VM requirement.
These lists describe current composition; the installed manifests
and producer-generated expectations bind the exact bytes to your SDK.

The [deployment guide](deployment.md#verify-ordinary-bytecode) explains verification.
Do not replace missing inputs by searching the source tree or loading an external
Erlang installation. A bare application that needs only its own code and core
operations does not need these ordinary inventories.

For persistent file/socket/zlib facilities, acquire passive libraries and prepare
generic composition from their inputs, then initialize/start and perform explicit
Erlang startup calls. Optional `beam_host_prepare`/`beam_host_start` combines those
steps with an explicit action list. For explicit NIF bindings, call
`beam_vm_bind_nif` after start. Managed programs select port admission and a helper
independently. Release caller preparations after VM initialization when no longer
needed; the VM retains native owners through cleanup. Library readiness and
monitoring are above the VM. See [the composition reference](reference.md#composition).

For one modern application lifetime, use `beam_return_prepare` followed by
`beam_return_run`. Include `<beam/boot.h>` and link the optional `BEAM::Host`
archive as well as the selected runtime. Supply required ordinary boot/support bytecode and a Host plan selecting libraries
and explicit startup actions, plus
entry bytes, ETF arguments and callbacks. The entry's completion ends the boot,
including its children; C receives a copied outcome after VM cleanup. Release
it with `beam_outcome_release` even when run reports a cleanup error; follow the
[initialization and reuse rules](reference.md#returning-beam-boot). Cancel an
unstarted recipe with `beam_return_discard`. Another recipe may run under the
same system.

An independent process-owned host instead supplies a root MFA, immutable
context and explicit native/process policy, then calls `beam_vm_enter`.
Callbacks run within entry on the owner thread. Once the root is admitted,
entry never returns: halt exits the host process. OTP uses this generic route
with product-owned acquisition, commands and startup; there is no SDK OTP boot
adapter or deployment profile, and no returning OTP teardown guarantee.

Use [deployment helpers](deployment.md#application-bundles) to package selected assets.
For errors, check `beam_status`; optional `beam_error` diagnostics belong only
to the operation that filled them. Internal native allocation/invariant failures
can still terminate the host. Host-controlled operation preserves host signals,
standard streams, terminal, environment and floating-point state; native-stack
JIT requires compatible custom synchronous-fault handlers using `SA_ONSTACK`.
An admitted host callback must finish before the owner thread can continue; a
service deadline cannot interrupt it or authorize callback reentry.

## Responsive owner-thread embedding

For a UI host, create one persistent OS thread and initialize a calling-thread
system there. Dispatch application commands to that thread. Do not use a queue
whose successive tasks may execute on different OS threads. The calling-thread
option accepts only host-controlled VMs; the legacy process-owned route retains
its main-thread contract.

`beam_vm_call_begin` copies the module/function names and ETF argument list and
admits one asynchronous call. Its slot remains occupied, including after the
outcome is ready, until `beam_vm_call_take` consumes it. Further admission and
conflicting module/scheduler operations return `BEAM_BUSY`.

Repeatedly call `beam_vm_pump(vm, timeout_ms, callback_budget, &pumped)` with a
properly initialized `beam_pump_result`. The owner waits against a monotonic
deadline and services at most the requested number of callbacks. It also serves
unsolicited host requests when there is no active call. The pump cannot preempt
native callbacks; they must return cooperatively. A ready call returns control
before another callback is admitted, preserving synchronous callback ordering.

`beam_vm_call_take` returns `BEAM_BUSY` while the result is unavailable. On
completion it transfers the copied result to the host and releases the slot.
Use `beam_result_release` as usual. If `pumped.stop_requested` is set, call
`beam_vm_stop` before retrieving the terminal result. Owner-thread stop is legal
with a pending call: it joins runtime workers, preserves any previously recorded
outcome, and otherwise completes the call with `BEAM_STOPPED`. Results remain
retrievable after stop and copied results survive VM destruction.

`beam_vm_wake` is the narrow thread-safe exception: it interrupts a pump wait
without performing a VM operation. The application must synchronize the wake
handle against VM destruction. The owner drains its own command queue and decides
whether to call, stop or restart. Wake is not cross-thread cancellation.

The synchronous `beam_vm_call` uses the same admission, pumping and result
machinery. Existing main-thread, returning and process-owned contracts remain.
For application packaging, see [iOS deployment](ios.md).
