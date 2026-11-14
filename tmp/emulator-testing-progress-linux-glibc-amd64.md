# Emulator testing progress: Linux glibc amd64

[Progress index](emulator-testing-progress.md) ·
[Shared scope, workflow and evidence rules](emulator-testing-findings.md)

Target: native Linux with glibc on amd64; product presets use `linux-x86_64`.
Linux AArch64 remains a product target outside this qualification job.

## Current state

Product presets and runtime/selected-suite integration exist. Full-corpus
preparation requires the Linux native-fixture and process-support port (F07).
No native Linux qualification is recorded, and no configuration is accepted.

| Engine | Build | Runtime checks | Selected suites | Full preparation | Focused / suite diagnostics | Complete ordered campaign |
| --- | --- | --- | --- | --- | --- | --- |
| `emu` | Release | Not run | Not run | Pending F07 | Not run | Not run |
| `jit` | Release | Not run | Not run | Pending F07 | Not run | Not run |
| `emu` | Debug | Not run | Not run | Pending F07 | Not run | Not run |
| `jit` | Debug | Not run | Not run | Pending F07 | Not run | Not run |

## Next steps

- Port private native fixture compilation/linkage and launcher/helper staging.
- Implement Linux process identity, listener ownership, private EPMD and safe
  cleanup; verify epoll and ordinary resource discovery on the native host.
- Validate runtime installation/staging/relocation and the independent selected
  five-suite, 24-case gate.
- Use the shared focused-run and suite-ledger workflow for all four rows, then
  perform complete ordered acceptance runs when their blockers are resolved.
  Record the compiler, glibc/sysroot, host and measured platform conditions.
