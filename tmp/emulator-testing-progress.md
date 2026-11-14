# Emulator testing: progress index

Keep these working records tracked in this repository's `tmp/` until the job is
complete. The goal is practical confidence in the candidate VM through the
reasonably applicable retained emulator functional corpus.

The [shared findings and decisions](emulator-testing-findings.md) define scope,
exclusions, blocker handling, test commands and acceptance evidence. Each
platform tracker covers `emu`/`jit` × Release/Debug.

| Platform | Current state | Next work |
| --- | --- | --- |
| [Linux glibc amd64](emulator-testing-progress-linux-glibc-amd64.md) | Product presets and runtime/selected tests exist; no native qualification recorded | Port the full harness (F07), then qualify on native Linux |
| [macOS arm64](emulator-testing-progress-macos-arm64.md) | `emu` Release discovery: 121 suite rows, 115 passed, 5 N/A, 1 failed (`hash_SUITE`); no accepted complete campaign | Review the hash blocker, then complete qualification at final inputs |
| [Windows amd64](emulator-testing-progress-windows-amd64.md) | `emu` Release initial discovery complete: 108 Passed, 7 N/A, 6 Failed/deferred, 0 Not run; independent audit consistent; no accepted complete campaign | Repair exact guard accounting and Windows paths, rerun on final inputs, and review hash/MSVC smoke blockers (F08) |

No configuration has an accepted complete ordered campaign. Suite discovery
results do not replace that acceptance run. Linux AArch64 remains a product
target outside this three-platform qualification job.

Platform trackers retain native execution evidence and unresolved blockers.
Disposable builds, logs and receipts belong in workspace `../tmp/`.

The [Windows foundation accounting review](../../tmp/windows-full-qualification/foundation-accounting-review.md)
audits all 29 native/data recipes, Windows native exclusions, pinned OSMon
support, unchanged functional scope and F02 timezone handling. It records an
expected stale preparation after launcher repairs; final preparation and
qualification are still pending.
