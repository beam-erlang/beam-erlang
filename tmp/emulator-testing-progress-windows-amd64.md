# Emulator testing progress: Windows amd64

[Progress index](emulator-testing-progress.md) ·
[Shared scope and evidence rules](emulator-testing-findings.md) ·
[Test commands](../README.md)

Updated 2026-11-13 18:15:31 UTC-08:00. Target native Windows amd64, MSVC x64.

Interpreter Release initial discovery is complete: **108 Passed, 7 N/A, 6 Failed/deferred, 0 Not run** across 121 suite rows/120 execution units.
Totals so far: 2041 passed cases, 16 failed cases, 82 audited user skips, 19 unaudited user skips and 14 automatic skips.
120 of 120 completed cleanup records leave zero owned processes. Not run includes the active unit until its complete result is recorded.

Initial discovery and its independent audit are complete. Project-owned path, guard-accounting, launcher and evidence-integrity repairs are applied. The relinked candidate passed fresh runtime/install checks and the independent 24-case gate. Launcher Job Object nesting and actual peer checks are being validated before final preparation. The table below preserves initial frozen results; hash and smoke remain failed and ordered acceptance is gated.

Initial frozen discovery completed with **108 Passed, 7 N/A, 6 Failed, 0 Not run**. [Independent audit: consistent, no errors](../../tmp/windows-full-qualification/discovery-drive-case-fixed-audit.json); all 120 cleanup records are empty. [Original complete suite table and identities](../../tmp/windows-full-qualification/tracker-initial-discovery-complete.md). Final-input results are recorded separately; historical passes are not upgraded.

## Current configuration status

| Engine | Build | Runtime checks | Selected suites | Full preparation | Discovery | Ordered full campaign |
| --- | --- | --- | --- | --- | --- | --- |
| `emu` | Release | Passed: 2 checks, including install/stage/relocation | Passed: 5 suites, 24 cases, 0 skips | Prepared at shown fingerprint: 1,322 files, 121-entry catalog | 108 Passed, 7 N/A, 6 Failed/deferred, 0 Not run | Not run; failures pending |
| `jit` | Release | Maintained output refreshed and passed | Earlier 24-case baseline; current qualification pending | Pending final qualification | Not validated at current inputs | Not run |
| `emu` | Debug | Maintained output refreshed and passed | Earlier 24-case baseline; current qualification pending | Pending final qualification | Not validated at current inputs | Not run |
| `jit` | Debug | Maintained output refreshed and passed | Earlier 24-case baseline; current qualification pending | Pending final qualification | Not validated at current inputs | Not run |

## Run identity and baseline

Build: `../tmp/windows-full-qualification/build-emu-release`, with both optional emulator integrations enabled. [Native baseline/input capture](../../tmp/windows-full-qualification/final-inputs-0545f77512645af2c27f10a89668ba1e78277d4f690c2554f5693bcffbe12abf.json) records actual source, bootstrap, support, toolchain, SDK, host and resource identities.

| Identity | Recorded value |
| --- | --- |
| Source at execution | `73fd079d66d4b0d23c336dce53aaa014581ee27f` plus recorded project-owned changes |
| Upstream pin | `550d7b7898706c7822362c42e7b93120c1d1f29a` |
| Frozen input fingerprint | `0545f77512645af2c27f10a89668ba1e78277d4f690c2554f5693bcffbe12abf` |
| Candidate SHA-256 | `ea072c52872c2e7ce3a5c1f789374d5daa5aeadedaa2a3b31b162be7256889d9` |
| Child helper | Native Windows; no `erl_child_setup` or Unix helper alias |
| Toolchain | MSVC 19.44.35215/toolset 14.44.35207; CMake 4.4.3; Ninja 1.13.1; Python 3.12.10 |
| Windows SDK | 10.0.26100.0; exact selected paths captured in native receipt |
| Installed public BEAM SDK | Not used; private candidate/bootstrap/support closure |

The initial inputs stayed frozen for all 120 units and their independent audit. Project-owned repairs were applied afterward; final-input revalidation is required for every stale suite. Retained upstream suite/support bytes remain unchanged; 14 additional imports match the pin byte-for-byte.

| Baseline gate | Result and evidence |
| --- | --- |
| Native fixtures | [122 verified artifacts](../../tmp/windows-full-qualification/native-artifacts-final.json): 99 DLL modules, 18 executables, EI and four headers; no runtime DLL dependency |
| Full preparation | [1,322 private files; 121-suite catalog](../../tmp/beam-full-775ed2fdfe10-prepare-20261114T003315Z-5b7f10/prepared-receipt.json) |
| Runtime and selected CTests | [All 7 passed](../../tmp/windows-full-qualification/baseline-final-summary.json): runtime 2, selected 5/24 cases, zero failures/skips |
| Harness regressions | Initial frozen harness: 40 Python and 14 native checks passed. Applied repairs: 75 full/focus/policy tests and 5 operational audit tests passed; native launcher/containment checks in progress. |
| Maintained outputs | [All 16 stages passed](../../tmp/windows-full-qualification/maintained-refresh/results-final.json): all four presets rebuilt, runtime checked and installed, including Unicode/staging/relocation/Debug PDB checks |

The [foundation accounting review](../../tmp/windows-full-qualification/foundation-accounting-review.md) covers all 29 recipes, exact Windows native omissions, pinned OSMon/event-log support, specifications and F02's Pacific-time treatment. It identifies its prepared snapshot explicitly; later launcher rebuilds require fresh preparation.

## Audited N/A suites

N/A is distinct from Passed. Each completed N/A target has no passed/failed/automatically skipped cases and complete exact accounting.

| Suite | User skips | Reason and result |
| --- | --- | --- |
| `efile_SUITE` | 2 | [Exact audited native Windows file/proc guards](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010315Z-92df6c/result.json) |
| `estone_SUITE` | 1 | [Agreed scoring-benchmark exclusion](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010523Z-fac5b8/result.json) |
| `hash_property_test_SUITE` | 3 | [Optional property-testing framework absent](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010942Z-b46cd4/result.json) |
| `jit_SUITE` | 6 | [Interpreter/non-JIT guards and profiler scope](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011101Z-a47800/result.json) |
| `lcnt_SUITE` | 5 | [Lock counting disabled](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011116Z-5840a6/result.json) |
| `lttng_SUITE` | 9 | [Dynamic tracing disabled](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011147Z-215750/result.json) |
| `os_signal_SUITE` | 0 | [Exact audited Win32 all/0 empty inventory](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012341Z-d0c526/result.json) |

## Deferred failures and review

These rows remain failed. Proposed project-owned repairs or guard audits do not qualify their current executions. No upstream edit or assertion/acceptance relaxation has been applied.

| Suite | Recorded outcome | Review and evidence |
| --- | --- | --- |
| `alloc_SUITE` | 11 passed, 0 failed, 2 user skips, 0 auto skips | Native mseg allocator skip needs its exact guard and fresh allocator observation. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003840Z-e43839/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003840Z-e43839/cleanup.json) |
| `dirty_nif_SUITE` | 22 passed, 13 failed, 0 user skips, 0 auto skips | Native CRT file paths exceed MAX_PATH; shorter Windows run roots are prepared for whole-suite revalidation. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005634Z-577bc9/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005634Z-577bc9/cleanup.json) |
| `driver_SUITE` | 42 passed, 0 failed, 28 user skips, 0 auto skips | Native Windows driver/allocator skips need exact source-backed guard accounting. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010103Z-d9408c/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010103Z-d9408c/cleanup.json) |
| `hash_SUITE` | 12 passed, 0 failed, 15 user skips, 14 auto skips | Known upstream Config-marker problem; automatic skips remain failures and upstream edits require approval. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010902Z-18a91d/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010902Z-18a91d/cleanup.json) |
| `port_SUITE` | 68 passed, 2 failed, 4 user skips, 0 auto skips | Long run paths exceed native chdir and quoted-spawn path limits; both files/directories exist. Compact Windows roots require complete-suite revalidation. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012704Z-c24da0/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012704Z-c24da0/cleanup.json) |
| `smoke_test_SUITE` | 2 passed, 1 failed, 0 user skips, 0 auto skips | Optimized MSVC emu uses switch dispatch; the retained jump_table assertion fails. No skip guard applies; any source/scope change requires approval. [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014814Z-5a0d2f/result.json) · [cleanup](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014814Z-5a0d2f/cleanup.json) |

## Upstream review required

`hash_SUITE` reproduces the known pinned upstream Config-marker mismatch. [Its native result](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010902Z-18a91d/result.json) records 12 passed cases, 15 audited user skips and 14 automatic skips before correctness bodies execute. This remains Failed, never N/A.

The retained suite inserts bare atom `phash2_benchmark_tests` into Config; pinned Common Test rejects that non-tuple entry in `init_per_testcase`. Canonical upstream source identities match the macOS review:

| Source | Canonical LF SHA-256 |
| --- | --- |
| `hash_SUITE.erl` | `0a2f6788da86e4618f26f3ea8fef4f01a962d9b54cf0f06a6014a44f9e441b15` |
| `test_server.erl` | `e12d6345f1de7eccc71d79c41844d1a16aad639a2f0347caba299b3f989ffca5` |

The tuple-marker/proplist-reader proposal remains unapplied. No upstream edit, assertion change or acceptance relaxation is authorized; complete ordered acceptance stays gated.

`smoke_test_SUITE` separately needs review: [the native result](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014814Z-5a0d2f/result.json) has two passed cases (`boot_combo`, `native_atomics`) and one failed `jump_table` assertion (`optimized_without_beam_jump_table`). The supported MSVC interpreter deliberately defines `NO_JUMP_TABLE` and uses switch dispatch. The retained assertion has no matching upstream Windows skip guard. This remains Failed; a narrowly scoped configuration exception or upstream test edit requires the user's agreement.

Source-backed triage and unapplied proposals: [deferred review](../../tmp/windows-full-qualification/deferred-failure-review.md). The known hash Config-marker problem remains unapproved for upstream modification; it is recorded here once its native suite result is processed.

## Remaining qualification

- Initial discovery and independent audit are complete; preserve all historical failure evidence.
- Validate applied project-owned repairs, prepare fresh receipts and rerun affected/stale complete suites on final inputs.
- Run one ordered `emulator.full` only after every applicable suite passes and every N/A is audited; qualify JIT Release and both Debug configurations afterward.

## Interpreter Release suite ledger

Each complete suite runs individually through its CTest entry. The whole a/z pair ran once in one VM and reports both rows. Exact commands, durations and cleanup receipts are retained with each unit in the machine-readable ledger.

<!-- suite-qualification-ledger:start -->
Live ledger: [discovery-drive-case-fixed-ledger.json](../../tmp/windows-full-qualification/discovery-drive-case-fixed-ledger.json).
Current inputs: `0545f77512645af2c27f10a89668ba1e78277d4f690c2554f5693bcffbe12abf`. State: **processed_with_failures**.
Running unit: `None`.

| Suite | Status | Passed | Failed | User skips | Auto skips | Inputs | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `a_SUITE` | Passed | 4 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003602Z-6d0663/result.json) |
| `after_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003707Z-46899b/result.json) |
| `alloc_SUITE` | Failed/deferred | 11 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003840Z-e43839/result.json) |
| `async_ports_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003935Z-68da37/result.json) |
| `atomics_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003956Z-53b761/result.json) |
| `beam_SUITE` | Passed | 11 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004010Z-152ab9/result.json) |
| `beam_literals_SUITE` | Passed | 14 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004036Z-28e04c/result.json) |
| `bif_SUITE` | Passed | 34 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004052Z-af7d88/result.json) |
| `big_SUITE` | Passed | 21 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004146Z-da832a/result.json) |
| `binary_SUITE` | Passed | 52 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004205Z-c0aa60/result.json) |
| `bs_bincomp_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004423Z-e025f3/result.json) |
| `bs_bincomp_no_opt_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004437Z-3e51f8/result.json) |
| `bs_bincomp_stripped_types_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004451Z-15153a/result.json) |
| `bs_bit_binaries_SUITE` | Passed | 11 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004505Z-66c7c9/result.json) |
| `bs_bit_binaries_no_opt_SUITE` | Passed | 11 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004519Z-837c2d/result.json) |
| `bs_construct_SUITE` | Passed | 30 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004534Z-3d8354/result.json) |
| `bs_construct_no_opt_SUITE` | Passed | 30 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004550Z-ff35f2/result.json) |
| `bs_construct_stripped_types_SUITE` | Passed | 30 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004605Z-c84904/result.json) |
| `bs_match_bin_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004620Z-21d1e4/result.json) |
| `bs_match_bin_no_opt_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004634Z-a318f6/result.json) |
| `bs_match_bin_stripped_types_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004649Z-d60ab1/result.json) |
| `bs_match_int_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004703Z-978c41/result.json) |
| `bs_match_int_no_opt_SUITE` | Passed | 9 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004718Z-8dddf8/result.json) |
| `bs_match_int_stripped_types_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004732Z-69bc15/result.json) |
| `bs_match_misc_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004747Z-8dd71e/result.json) |
| `bs_match_misc_no_opt_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004802Z-35d41e/result.json) |
| `bs_match_misc_stripped_types_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004818Z-13a484/result.json) |
| `bs_match_tail_SUITE` | Passed | 4 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004834Z-d01078/result.json) |
| `bs_match_tail_no_opt_SUITE` | Passed | 4 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004848Z-55aa45/result.json) |
| `bs_match_tail_stripped_types_SUITE` | Passed | 4 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004902Z-dc71ee/result.json) |
| `bs_utf_SUITE` | Passed | 8 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004916Z-d9b496/result.json) |
| `bs_utf_no_opt_SUITE` | Passed | 8 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T004950Z-57e49a/result.json) |
| `bs_utf_stripped_types_SUITE` | Passed | 8 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005031Z-751c0f/result.json) |
| `busy_port_SUITE` | Passed | 14 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005103Z-804802/result.json) |
| `call_trace_SUITE` | Passed | 28 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005132Z-65441c/result.json) |
| `code_SUITE` | Passed | 27 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005206Z-c652f8/result.json) |
| `code_parallel_load_SUITE` | Passed | 2 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005426Z-6cba45/result.json) |
| `counters_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005444Z-f207db/result.json) |
| `crypto_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005459Z-6d9633/result.json) |
| `ddll_SUITE` | Passed | 26 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005515Z-aded92/result.json) |
| `decode_packet_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005539Z-fac08a/result.json) |
| `dirty_bif_SUITE` | Passed | 13 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005554Z-cf0f5a/result.json) |
| `dirty_nif_SUITE` | Failed/deferred | 22 | 13 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005634Z-577bc9/result.json) |
| `distribution_SUITE` | Passed | 56 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T005753Z-3a48a6/result.json) |
| `driver_SUITE` | Failed/deferred | 42 | 0 | 28 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010103Z-d9408c/result.json) |
| `dump_SUITE` | Passed | 4 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010240Z-2930ac/result.json) |
| `efile_SUITE` | Not applicable | 0 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010315Z-92df6c/result.json) |
| `erl_debugger_SUITE` | Passed | 26 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010329Z-893747/result.json) |
| `erl_drv_thread_SUITE` | Passed | 3 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010345Z-2a820d/result.json) |
| `erl_link_SUITE` | Passed | 16 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010407Z-2e8e25/result.json) |
| `erts_debug_SUITE` | Passed | 8 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010507Z-e1c03e/result.json) |
| `estone_SUITE` | Not applicable | 0 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010523Z-fac5b8/result.json) |
| `evil_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010537Z-211d5b/result.json) |
| `exception_SUITE` | Passed | 16 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010550Z-b05a91/result.json) |
| `float_SUITE` | Passed | 15 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010607Z-ff5c11/result.json) |
| `fun_SUITE` | Passed | 18 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010625Z-b054f3/result.json) |
| `gc_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010749Z-86c627/result.json) |
| `guard_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010829Z-ddd516/result.json) |
| `guard_no_opt_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010846Z-c28473/result.json) |
| `hash_SUITE` | Failed/deferred | 12 | 0 | 15 | 14 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010902Z-18a91d/result.json) |
| `hash_property_test_SUITE` | Not applicable | 0 | 0 | 3 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010942Z-b46cd4/result.json) |
| `hello_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T010956Z-d767b0/result.json) |
| `hibernate_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011016Z-0dc5e7/result.json) |
| `iovec_SUITE` | Passed | 13 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011045Z-3cfe11/result.json) |
| `jit_SUITE` | Not applicable | 0 | 0 | 6 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011101Z-a47800/result.json) |
| `lcnt_SUITE` | Not applicable | 0 | 0 | 5 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011116Z-5840a6/result.json) |
| `list_bif_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011130Z-536f17/result.json) |
| `lttng_SUITE` | Not applicable | 0 | 0 | 9 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011147Z-215750/result.json) |
| `map_SUITE` | Passed | 63 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011201Z-39b01a/result.json) |
| `map_no_opt_SUITE` | Passed | 63 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011229Z-413e91/result.json) |
| `match_spec_SUITE` | Passed | 46 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011258Z-7287ff/result.json) |
| `message_queue_data_SUITE` | Passed | 5 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011318Z-b46bc1/result.json) |
| `module_info_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011339Z-169f33/result.json) |
| `monitor_SUITE` | Passed | 25 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011355Z-6bc574/result.json) |
| `mtx_SUITE` | Passed | 22 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011443Z-011522/result.json) |
| `multi_load_SUITE` | Passed | 3 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T011942Z-734c7f/result.json) |
| `native_record_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012001Z-a76d1e/result.json) |
| `nested_SUITE` | Passed | 4 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012016Z-4eaa63/result.json) |
| `nif_SUITE` | Passed | 79 | 0 | 7 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012032Z-5ec1f0/result.json) |
| `node_container_SUITE` | Passed | 19 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012148Z-91e9a9/result.json) |
| `nofrag_SUITE` | Passed | 5 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012224Z-f848b5/result.json) |
| `num_bif_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012239Z-0425bf/result.json) |
| `op_SUITE` | Passed | 11 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012257Z-c44a67/result.json) |
| `os_signal_SUITE` | Not applicable | 0 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012341Z-d0c526/result.json) |
| `persistent_term_SUITE` | Passed | 22 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012356Z-b8620e/result.json) |
| `port_SUITE` | Failed/deferred | 68 | 2 | 4 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012704Z-c24da0/result.json) |
| `port_bif_SUITE` | Passed | 8 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012845Z-e7fb8a/result.json) |
| `port_trace_SUITE` | Passed | 42 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012912Z-26625c/result.json) |
| `prim_eval_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012930Z-c89953/result.json) |
| `process_SUITE` | Passed | 102 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T012945Z-e88920/result.json) |
| `process_max_heap_size_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013650Z-632c97/result.json) |
| `pseudoknot_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013731Z-a88b31/result.json) |
| `receive_SUITE` | Passed | 25 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013747Z-9e371a/result.json) |
| `ref_SUITE` | Passed | 5 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013829Z-b061e7/result.json) |
| `register_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013914Z-1aceef/result.json) |
| `save_calls_SUITE` | Passed | 2 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013932Z-c0d854/result.json) |
| `scheduler_SUITE` | Passed | 24 | 0 | 1 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T013947Z-dfaf9e/result.json) |
| `send_term_SUITE` | Passed | 1 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014351Z-723b0e/result.json) |
| `sensitive_SUITE` | Passed | 12 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014406Z-ce456b/result.json) |
| `signal_SUITE` | Passed | 38 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014422Z-e909a6/result.json) |
| `small_SUITE` | Passed | 13 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014721Z-ed19c7/result.json) |
| `smoke_test_SUITE` | Failed/deferred | 2 | 1 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014814Z-5a0d2f/result.json) |
| `statistics_SUITE` | Passed | 18 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014838Z-181573/result.json) |
| `system_info_SUITE` | Passed | 12 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T014946Z-472ccc/result.json) |
| `system_profile_SUITE` | Passed | 6 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015008Z-f04aa4/result.json) |
| `time_SUITE` | Passed | 16 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015044Z-2229f7/result.json) |
| `timer_bif_SUITE` | Passed | 23 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015208Z-676af6/result.json) |
| `trace_SUITE` | Passed | 76 | 0 | 2 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015338Z-a5bb9a/result.json) |
| `trace_bif_SUITE` | Passed | 14 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015458Z-1862e8/result.json) |
| `trace_call_count_SUITE` | Passed | 10 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015515Z-b9eaec/result.json) |
| `trace_call_memory_SUITE` | Passed | 24 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015532Z-2def05/result.json) |
| `trace_call_time_SUITE` | Passed | 34 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015550Z-f05fcc/result.json) |
| `trace_local_SUITE` | Passed | 50 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015649Z-1bc648/result.json) |
| `trace_meta_SUITE` | Passed | 16 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015846Z-22e45c/result.json) |
| `trace_nif_SUITE` | Passed | 12 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015906Z-4523af/result.json) |
| `trace_port_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015924Z-957e5c/result.json) |
| `trace_session_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T015943Z-55a4f7/result.json) |
| `tracer_SUITE` | Passed | 42 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T020009Z-d80efa/result.json) |
| `tuple_SUITE` | Passed | 20 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T020038Z-fd47bc/result.json) |
| `unique_SUITE` | Passed | 2 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T020111Z-401c09/result.json) |
| `z_SUITE` | Passed | 9 | 0 | 0 | 0 | `0545f7751264` | [record](../../tmp/beam-full-775ed2fdfe10-focused-run-20261114T003602Z-6d0663/result.json) |

<!-- suite-qualification-ledger:end -->

## Porting and historical receipts

The earlier native foundations, initial long-path preflight, launcher false-pass diagnosis, approved timezone exception, CRLF guard identity contract and earlier baseline evidence are preserved in [the previous tracker snapshot](../../tmp/windows-full-qualification/tracker-before-macos-layout.md). All historical native execution receipts remain in their original directories. The initial preflight console-log path collision is disclosed in [its bookkeeping note](../../tmp/windows-full-qualification/preflight-console-log-note.json).

Windows preserves the host timezone. Only the two exact Stockholm-only case guards may accept native timezone skips with successful fresh `tzutil /g` evidence. Missing probes or ordinary fixtures remain failures.
