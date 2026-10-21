# Runtime concepts

## Erlang code and its host

An Erlang source module compiles into BEAM bytecode. A VM executes the modules
supplied to it. A host chooses the entry point, supplies code and selects native
capabilities. The `beam` command is one host; an application using the C SDK is
another.

The runtime does not acquire arbitrary dependencies after an undefined call.
Installing a file, supplying its bytecode and activating a native capability
are distinct operations. Explicit composition makes the runtime's inputs visible
to its host. It is not a security sandbox for untrusted Erlang or native code.

## Code and capabilities

| Layer | What supplies it |
| --- | --- |
| Core | Nine modules embedded in `libbeam`, with the VM's core processes and mechanisms. |
| Modern base library | Seven ordinary modules explicitly supplied from the installed base inventory. |
| Boot and service support | Ordinary modules selected by the host; supplying them alone enables no service. |
| Native components | Separately selected file, network and zlib libraries with their own bytecode and native implementations. |
| Program and host code | Modules, callbacks and other explicit inputs supplied by a command or embedding application. |

The [standard-library catalog](stdlib/README.md) names the public modules and
their availability. The [component manuals](components/README.md) describe
native requirements. In particular, installing a component shared object does
not make its functions usable in every VM. The host prepares a composition and
the VM performs activation at the selected execution boundary.

The ordinary source, shell and evaluation commands select a convenient service
set. Installed project execution uses a smaller profile. Minimal C hosts choose
their own services. Check the [CLI profiles](cli/README.md#execution-profiles)
before assuming a source-file example has the same dependencies as a packaged
program.

## VM lifetimes

A C host initializes one system and runs at most one VM at a time. After a VM
is stopped and terminated, the same system can create a fresh VM with a different
composition. A stopped handle cannot restart. Concurrent VMs and reinitializing
a terminated system are unsupported.

Bare embedding lets C load code and make calls. Returning boot runs an entry and
tears down its VM before returning a copied outcome to C. Process-owned execution
is also available for hosts whose runtime controls process lifetime. The
[embedding guide](c-api/guide.md) explains the choices and cleanup obligations.

For `beam run file.erl`, compilation and execution use separate sequential VMs.
Compiler processes and VM-local state do not carry into the program. When the
program entry returns, its remaining processes do not keep the command alive;
server programs must keep the entry active until their work and shutdown finish.

## BEAM/Erlang and Erlang/OTP

The independent Erlang/OTP product consumes the installed BEAM/Erlang SDK and
uses its VM. OTP supplies its applications, additional preloads, boot policy,
native services and commands. Modern BEAM/Erlang exposes its own library and
hosting interfaces. Its `beam` command has no runtime-family selector.

The modern interfaces remain experimental. The shared VM and SDK must continue
to support the selected OTP compatibility target, with OTP adaptations where
needed. This relationship does not make OTP applications part of the modern
standard library or establish full OTP compatibility for every current workflow.
