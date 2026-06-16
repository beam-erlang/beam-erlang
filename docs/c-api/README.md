# C API and embedding

The C SDK runs Erlang inside a native application through `libbeam`. The host
chooses modules and native capabilities, controls execution and owns copied
results. These manuals describe the experimental SDK; examples use the Linux layout
unless a platform guide says otherwise. Use matching headers and libraries. Source and binary compatibility are not
promised yet.

Source compatibility (rebuilding an existing host unchanged) and binary
compatibility (running its existing binary with a newer runtime) are separate
contracts. Neither follows from the ERTS version in a release identity. See
[release operations](../../release/README.md) for the current preview boundary
and the adopted stabilization policy.

| Document | Start here when you want to… |
| --- | --- |
| [Embedding guide](guide.md) | Build a first C host, understand ownership and choose a lifecycle. |
| [C API reference](reference.md) | Look up options, statuses, lifecycle rules, composition and callbacks. |
| [iOS deployment](ios.md) | Build with an explicit target SDK and assemble a signed Apple application. |
| [SDK and deployment guide](deployment.md) | Select installed CMake targets, verify inputs and package a native application. |

The [public header](../../include/beam/beam.h) supplies complete declarations. The
[first-call project](../../tests/installed/sdk/consumer/first-call/) is the
runnable tutorial source and remains part of installed-SDK acceptance. The
documentation does not maintain a second copy of that project.

Include `<beam/beam.h>` for the complete embedding interface. Its self-contained
`<beam/runtime.h>` and `<beam/boot.h>` headers cover generic VM operation and the
modern returning application lifecycle. Optional SDK preparation uses
`<beam/sdk/bytecode.h>` and `<beam/sdk/inventories.h>`; native execution support
uses `<beam/native.h>`. None is included by the embedding umbrella. Native
extension authors keep `<erl_nif.h>` or `<erl_driver.h>` through their own targets.

Choose a lifecycle according to who should regain control:

| Lifecycle | Host entry | Completion |
| --- | --- | --- |
| Persistent bare or composed VM | `beam_vm_start`, then synchronous calls or async admission/pump/retrieval | Each call returns; child Erlang processes may remain live until stop. |
| One returning application | `beam_return_prepare`, then `beam_return_run` | The whole VM is reclaimed before the copied application outcome returns, subject to reported cleanup errors. |
| Process-owned host | `beam_vm_enter` | After root admission, execution owns the process and does not return. |

All lifetimes share one VM implementation. Operations are serial on the selected
owner OS thread; legacy `beam_init` selects the main thread. Terminating one VM permits a fresh VM under the same system;
concurrent VMs and system reinitialization are unsupported.

The independent Erlang/OTP distribution uses the public SDK with its own startup
and composition. Its compatibility target is separate from the experimental
modern API; there is no runtime-family switch in this SDK.

[Return to the documentation index](../README.md).
