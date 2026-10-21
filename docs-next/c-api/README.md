# C embedding SDK

The C SDK runs Erlang inside a native application through `libbeam`. The host
chooses modules and native capabilities, controls execution and owns copied
results. These are candidate user documents for the experimental Linux SDK;
use matching headers and libraries. Source and binary compatibility are not
promised yet.

| Document | Start here when you want to… |
| --- | --- |
| [Embedding guide](guide.md) | Build a first C host, understand ownership and choose a lifecycle. |
| [C API reference](reference.md) | Look up options, statuses, lifecycle rules, composition and callbacks. |
| [SDK and deployment guide](deployment.md) | Select installed CMake targets, verify inputs and package a native application. |

The [public header](../../src/beam.h) supplies complete declarations. The
[first-call project](../../tests/installed/sdk/consumer/first-call/) is the
runnable tutorial source and remains part of installed-SDK acceptance. The
documentation does not maintain a second copy of that project.

Choose a lifecycle according to who should regain control:

| Lifecycle | Host entry | Completion |
| --- | --- | --- |
| Persistent bare or composed VM | `beam_vm_start`, then `beam_vm_call` | Each call returns; child Erlang processes may remain live until stop. |
| One returning application | `beam_return_prepare`, then `beam_return_run` | The whole VM is reclaimed before the copied application outcome returns, subject to reported cleanup errors. |
| Process-owned host | `beam_vm_enter` | After root admission, execution owns the process and does not return. |

All lifetimes share one VM implementation. Operations are serial on the main
owner thread. Terminating one VM permits a fresh VM under the same system;
concurrent VMs and system reinitialization are unsupported.

The independent Erlang/OTP distribution uses the public SDK with its own startup
and composition. Its compatibility target is separate from the experimental
modern API; there is no runtime-family switch in this SDK.

[Return to the documentation index](../README.md).
