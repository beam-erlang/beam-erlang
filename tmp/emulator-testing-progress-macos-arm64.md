# Emulator testing progress: macOS arm64

[Progress index](emulator-testing-progress.md) ·
[Shared scope and evidence rules](emulator-testing-findings.md) ·
[Test commands](../README.md)

Updated 2026-11-14. Target native Apple Silicon, macOS 15 or newer.

Interpreter Release discovery is complete: **115 Passed, 5 N/A, 1 Failed
(`hash_SUITE`), 0 Not run** across 121 suite rows/120 execution units. Totals:
2094 passed cases, 0 failed cases, 74 audited user skips and 14 rejected
automatic skips, all in hash. All 120 cleanup records are empty.

Hash remains deferred for review. No ordered `emulator.full` run has completed,
so no configuration is fully qualified.

## Current configuration status

| Engine | Build | Runtime checks | Selected suites | Full preparation | Discovery | Ordered full campaign |
| --- | --- | --- | --- | --- | --- | --- |
| `emu` | Release | Passed: 2 checks, including install/stage/relocation | Passed: 5 suites, 24 cases, 0 skips | Fresh: 1306 files, 121-entry catalog | 115 Passed, 5 N/A, 1 Failed, 0 Not run | Not run; hash deferred |
| `jit` | Release | Current qualification pending | Pending | Fixtures stale for current harness | Not validated at current inputs | Not run |
| `emu` | Debug | Current qualification pending | Pending | Fixtures stale for current harness | Not validated at current inputs | Not run |
| `jit` | Debug | Current qualification pending | Pending | Fixtures stale for current harness | Not validated at current inputs | Not run |

## Run identity and baseline

Build: `../tmp/beam-suite-qualify-macos-arm64-emu-20261114`, with both optional
emulator integrations enabled. The
[baseline identity](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/baseline-identity.json)
records the exact candidate, bootstrap, support and toolchain inputs.

| Identity | Recorded value |
| --- | --- |
| Source at execution | `203c7402c2df08a1f78f2f65a5bf5adffbc47620` plus the recorded harness changes |
| Upstream pin | `550d7b7898706c7822362c42e7b93120c1d1f29a` |
| Frozen input fingerprint | `2c6a703787b3ff7d11cb68e67e0bfdc6298fdab64390cde0c1510471e8edad69` |
| Candidate SHA-256 | `0c640564eb8e4536e445d08b36c70c4757304c92664a957fed7fadb799896cd3` |
| Child helper SHA-256 | `1972e5162315ef3c508452faba844cefcb5233d9f1c174016889d1ce962d68ba` |
| Toolchain | Apple Clang 21.0.0; CMake 4.4.3; Ninja 1.13.2; deployment target 15.0 |
| SDK used by Ninja | `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.2.sdk` |
| SDKSettings SHA-256 | `82165b15ee21419d4cffaba8895797391b908ac9499ba70f561f3e1bd16758d6` |
| Installed public BEAM SDK | Not used; private candidate/bootstrap/support closure |

No runtime/harness/policy input changed or rebuilt after the freeze. The
[source provenance audit](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/source-provenance.json)
verified 1490 retained files against the pinned upstream Git blobs: 0 changed;
11 project-owned paths have no upstream object.

| Baseline gate | Result and evidence |
| --- | --- |
| Default build and public-header compile probes | [Passed](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/build-final.log) |
| Full preparation | [1306 private outputs; 121-suite catalog](../../tmp/beam-full-317298bba769-prepare-20261113T205855Z-0c2fd4/prepared-receipt.json) |
| Runtime and selected-suite CTests | [All 7 passed in 13.64s](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/baseline-gates.log): runtime 2, selected 5/24 cases, zero failures/skips |
| Harness regressions | 47 Python checks passed |

The [independent final audit](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/campaign-audit-final.json)
found all 120 units/121 rows consistent with no errors. It checks receipts,
events, source hashes and recorded policy membership/selectors; it does not
rerun tests or replace runtime resource-policy evaluation.

## Audited N/A suites

Each has 0 passed/failed/automatic-skipped cases; N/A is distinct from Passed.

| Suite | User skips | Reason and result |
| --- | --- | --- |
| `estone_SUITE` | 1 | [Agreed pure-benchmark scope](../../tmp/beam-full-317298bba769-focused-run-20261113T212148Z-c814ea/result.json) |
| `hash_property_test_SUITE` | 3 | [Optional property tool absent](../../tmp/beam-full-317298bba769-focused-run-20261113T215751Z-650aff/result.json) |
| `jit_SUITE` | 6 | [Interpreter/non-JIT guard and profiler scope](../../tmp/beam-full-317298bba769-focused-run-20261113T215840Z-c9afdc/result.json) |
| `lcnt_SUITE` | 5 | [Lock counting disabled](../../tmp/beam-full-317298bba769-focused-run-20261113T215849Z-059bfb/result.json) |
| `lttng_SUITE` | 9 | [Dynamic tracing disabled](../../tmp/beam-full-317298bba769-focused-run-20261113T215907Z-ece025/result.json) |

## Upstream review required

`hash_SUITE` is the sole failure. Its
[result](../../tmp/beam-full-317298bba769-focused-run-20261113T212311Z-a7b8be/result.json)
records 12 passed cases, 0 failed cases, 15 audited user skips (14 pure benchmarks
and 1 resource guard), and 14 automatic skips from
`{failed,{hash_SUITE,init_per_testcase,bad_return}}`.
[Cleanup](../../tmp/beam-full-317298bba769-focused-run-20261113T212311Z-a7b8be/cleanup.json)
left 0 owned processes.

The [review bundle](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/blocker-review.json)
establishes the contract mismatch: original `hash_SUITE` inserts a bare atom
`phash2_benchmark_tests` into Config; original Common Test `test_server`
rejects non-tuple Config entries before those 14 testcase bodies execute.
Both sources match pinned upstream, and their staged sources/compiled modules
match the preparation receipt.

| Current source | SHA-256 |
| --- | --- |
| `hash_SUITE.erl` | `0a2f6788da86e4618f26f3ea8fef4f01a962d9b54cf0f06a6014a44f9e441b15` |
| `test_server.erl` | `e12d6345f1de7eccc71d79c41844d1a16aad639a2f0347caba299b3f989ffca5` |

The [unapplied two-line proposal](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/blocker-proposal.diff)
uses a tuple marker and matching proplist reader, preserving one-iteration
semantics and all expected-hash assertions. If approved, the corresponding
project-owned source fingerprint also needs updating. No upstream edit,
assertion change or skip-policy relaxation is authorized or applied.

## Remaining qualification

- Review the deferred hash failure; obtain the user's agreement before any
  upstream edit. Use the [focused workflow](emulator-testing-findings.md#focused-diagnosis-and-complete-campaigns)
  for case/group diagnosis and complete-suite verification.
- Refresh affected receipts after any approved change and revalidate affected
  results at final fingerprints. Run the mandatory ordered `emulator.full`
  campaign once every applicable suite is green and N/A is audited.
- Prepare fresh fixtures and complete runtime, selected-suite and corpus
  qualification for JIT Release and both Debug configurations.

## Interpreter Release suite ledger

The [machine-readable ledger](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/campaign.json)
contains the fixed plan, exact commands, timings and per-unit evidence. The
whole a/z pair ran once in one VM, reporting both rows (4 and 9 passed cases).
All other units ran individually. Failed suites remained red/deferred while
discovery continued. The inactive driver's generated region is retained below.

<!-- suite-qualification-ledger:start -->
Live ledger: [campaign.json](../../tmp/beam-suite-qualify-macos-arm64-emu-20261114/campaign.json).
Current inputs: `2c6a703787b3ff7d11cb68e67e0bfdc6298fdab64390cde0c1510471e8edad69`. State: **processed_with_failures**.
Running unit: `None`.

| Suite | Status | Passed | Failed | User skips | Auto skips | Inputs | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `a_SUITE` | Passed | 4 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210213Z-afcc26/result.json) |
| `after_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210228Z-32033d/result.json) |
| `alloc_SUITE` | Passed | 13 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210356Z-cc87c9/result.json) |
| `async_ports_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210444Z-42f0ca/result.json) |
| `atomics_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210502Z-3c1a18/result.json) |
| `beam_SUITE` | Passed | 11 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210511Z-9ce192/result.json) |
| `beam_literals_SUITE` | Passed | 14 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210525Z-5e6a9d/result.json) |
| `bif_SUITE` | Passed | 34 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210534Z-f60f3f/result.json) |
| `big_SUITE` | Passed | 21 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210615Z-cda4d8/result.json) |
| `binary_SUITE` | Passed | 42 | 0 | 12 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210625Z-402c64/result.json) |
| `bs_bincomp_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210655Z-2164e0/result.json) |
| `bs_bincomp_no_opt_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210704Z-eeb4b5/result.json) |
| `bs_bincomp_stripped_types_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210712Z-d71ba8/result.json) |
| `bs_bit_binaries_SUITE` | Passed | 11 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210721Z-14eefa/result.json) |
| `bs_bit_binaries_no_opt_SUITE` | Passed | 11 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210731Z-23bdf7/result.json) |
| `bs_construct_SUITE` | Passed | 30 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210740Z-6a4ae2/result.json) |
| `bs_construct_no_opt_SUITE` | Passed | 30 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210749Z-2dfd54/result.json) |
| `bs_construct_stripped_types_SUITE` | Passed | 30 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210759Z-4318d5/result.json) |
| `bs_match_bin_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210808Z-776e1f/result.json) |
| `bs_match_bin_no_opt_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210818Z-658105/result.json) |
| `bs_match_bin_stripped_types_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210827Z-38e43d/result.json) |
| `bs_match_int_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210836Z-719d3b/result.json) |
| `bs_match_int_no_opt_SUITE` | Passed | 9 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210846Z-df6e6b/result.json) |
| `bs_match_int_stripped_types_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210856Z-ee19e3/result.json) |
| `bs_match_misc_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210905Z-9a761f/result.json) |
| `bs_match_misc_no_opt_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210915Z-3c0eac/result.json) |
| `bs_match_misc_stripped_types_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210925Z-d8b01a/result.json) |
| `bs_match_tail_SUITE` | Passed | 4 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210934Z-4fdb70/result.json) |
| `bs_match_tail_no_opt_SUITE` | Passed | 4 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210943Z-e2fff2/result.json) |
| `bs_match_tail_stripped_types_SUITE` | Passed | 4 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210952Z-48f868/result.json) |
| `bs_utf_SUITE` | Passed | 8 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211001Z-12e042/result.json) |
| `bs_utf_no_opt_SUITE` | Passed | 8 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211016Z-611414/result.json) |
| `bs_utf_stripped_types_SUITE` | Passed | 8 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211035Z-279214/result.json) |
| `busy_port_SUITE` | Passed | 14 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211049Z-d62296/result.json) |
| `call_trace_SUITE` | Passed | 28 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211114Z-13ae02/result.json) |
| `code_SUITE` | Passed | 27 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211138Z-4d8a88/result.json) |
| `code_parallel_load_SUITE` | Passed | 2 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211232Z-f54415/result.json) |
| `counters_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211245Z-c96b40/result.json) |
| `crypto_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211254Z-b81686/result.json) |
| `ddll_SUITE` | Passed | 26 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211304Z-8db0bc/result.json) |
| `decode_packet_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211323Z-4cf8d2/result.json) |
| `dirty_bif_SUITE` | Passed | 13 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211333Z-0302e9/result.json) |
| `dirty_nif_SUITE` | Passed | 35 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211405Z-b72b4c/result.json) |
| `distribution_SUITE` | Passed | 51 | 0 | 5 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211506Z-4b5dad/result.json) |
| `driver_SUITE` | Passed | 70 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211636Z-0cc671/result.json) |
| `dump_SUITE` | Passed | 4 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211922Z-b4c309/result.json) |
| `efile_SUITE` | Passed | 1 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T211952Z-4e073a/result.json) |
| `erl_debugger_SUITE` | Passed | 26 | 0 | 2 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212015Z-24aaba/result.json) |
| `erl_drv_thread_SUITE` | Passed | 3 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212026Z-84b494/result.json) |
| `erl_link_SUITE` | Passed | 16 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212044Z-1ced4d/result.json) |
| `erts_debug_SUITE` | Passed | 8 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212138Z-36b70f/result.json) |
| `estone_SUITE` | Not applicable | 0 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212148Z-c814ea/result.json) |
| `evil_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212158Z-50bf95/result.json) |
| `exception_SUITE` | Passed | 16 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212207Z-ef78b0/result.json) |
| `float_SUITE` | Passed | 15 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212217Z-5bd784/result.json) |
| `fun_SUITE` | Passed | 18 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212228Z-a3e9e9/result.json) |
| `gc_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212239Z-e80e43/result.json) |
| `guard_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212250Z-026bcf/result.json) |
| `guard_no_opt_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212301Z-05be76/result.json) |
| `hash_SUITE` | Failed/Blocked | 12 | 0 | 15 | 14 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T212311Z-a7b8be/result.json) |
| `hash_property_test_SUITE` | Not applicable | 0 | 0 | 3 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215751Z-650aff/result.json) |
| `hello_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215801Z-77fafd/result.json) |
| `hibernate_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215813Z-470da6/result.json) |
| `iovec_SUITE` | Passed | 13 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215831Z-c3f010/result.json) |
| `jit_SUITE` | Not applicable | 0 | 0 | 6 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215840Z-c9afdc/result.json) |
| `lcnt_SUITE` | Not applicable | 0 | 0 | 5 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215849Z-059bfb/result.json) |
| `list_bif_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215858Z-28e922/result.json) |
| `lttng_SUITE` | Not applicable | 0 | 0 | 9 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215907Z-ece025/result.json) |
| `map_SUITE` | Passed | 63 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215916Z-ac8eef/result.json) |
| `map_no_opt_SUITE` | Passed | 63 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215934Z-b55b63/result.json) |
| `match_spec_SUITE` | Passed | 46 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T215949Z-afff0a/result.json) |
| `message_queue_data_SUITE` | Passed | 5 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220001Z-f58ecb/result.json) |
| `module_info_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220016Z-16ac39/result.json) |
| `monitor_SUITE` | Passed | 25 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220025Z-3f9f9a/result.json) |
| `mtx_SUITE` | Passed | 22 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220102Z-e4c3e7/result.json) |
| `multi_load_SUITE` | Passed | 3 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220604Z-254727/result.json) |
| `native_record_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220616Z-b34195/result.json) |
| `nested_SUITE` | Passed | 4 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220625Z-74165a/result.json) |
| `nif_SUITE` | Passed | 85 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220635Z-acc673/result.json) |
| `node_container_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220734Z-b47f7e/result.json) |
| `nofrag_SUITE` | Passed | 5 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220800Z-b555ff/result.json) |
| `num_bif_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220810Z-9ee120/result.json) |
| `op_SUITE` | Passed | 11 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220824Z-244a6c/result.json) |
| `os_signal_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220858Z-1d5fa4/result.json) |
| `persistent_term_SUITE` | Passed | 22 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T220935Z-4908f0/result.json) |
| `port_SUITE` | Passed | 73 | 0 | 1 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T221028Z-1fa1fe/result.json) |
| `port_bif_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T221420Z-593d3c/result.json) |
| `port_trace_SUITE` | Passed | 42 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T221442Z-4211ba/result.json) |
| `prim_eval_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T221453Z-bbb3fb/result.json) |
| `process_SUITE` | Passed | 101 | 0 | 2 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T221504Z-c5d7f5/result.json) |
| `process_max_heap_size_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222113Z-2f1e0e/result.json) |
| `pseudoknot_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222124Z-da9fd7/result.json) |
| `receive_SUITE` | Passed | 25 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222133Z-a7fa01/result.json) |
| `ref_SUITE` | Passed | 5 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222202Z-ead97a/result.json) |
| `register_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222241Z-e2d7dc/result.json) |
| `save_calls_SUITE` | Passed | 2 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222251Z-a68b3c/result.json) |
| `scheduler_SUITE` | Passed | 23 | 0 | 2 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222300Z-17196f/result.json) |
| `send_term_SUITE` | Passed | 1 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222505Z-706b94/result.json) |
| `sensitive_SUITE` | Passed | 12 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222515Z-59815e/result.json) |
| `signal_SUITE` | Passed | 38 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222526Z-5a049d/result.json) |
| `small_SUITE` | Passed | 13 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222647Z-97da4b/result.json) |
| `smoke_test_SUITE` | Passed | 3 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222703Z-1586a3/result.json) |
| `statistics_SUITE` | Passed | 18 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222714Z-e74b31/result.json) |
| `system_info_SUITE` | Passed | 12 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222757Z-e119a9/result.json) |
| `system_profile_SUITE` | Passed | 6 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222807Z-e46ab7/result.json) |
| `time_SUITE` | Passed | 18 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222835Z-26b8fe/result.json) |
| `timer_bif_SUITE` | Passed | 23 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T222936Z-6798a2/result.json) |
| `trace_SUITE` | Passed | 76 | 0 | 2 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223055Z-128651/result.json) |
| `trace_bif_SUITE` | Passed | 14 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223159Z-a8befe/result.json) |
| `trace_call_count_SUITE` | Passed | 10 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223210Z-f8904d/result.json) |
| `trace_call_memory_SUITE` | Passed | 24 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223219Z-acc347/result.json) |
| `trace_call_time_SUITE` | Passed | 34 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223231Z-d91034/result.json) |
| `trace_local_SUITE` | Passed | 50 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223302Z-44514f/result.json) |
| `trace_meta_SUITE` | Passed | 16 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223439Z-0342cb/result.json) |
| `trace_nif_SUITE` | Passed | 12 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223452Z-2772ab/result.json) |
| `trace_port_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223505Z-550868/result.json) |
| `trace_session_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223518Z-148528/result.json) |
| `tracer_SUITE` | Passed | 42 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223531Z-5b38e2/result.json) |
| `tuple_SUITE` | Passed | 20 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223542Z-9bdb0c/result.json) |
| `unique_SUITE` | Passed | 2 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T223553Z-ecdbd5/result.json) |
| `z_SUITE` | Passed | 9 | 0 | 0 | 0 | `2c6a703787b3` | [record](../../tmp/beam-full-317298bba769-focused-run-20261113T210213Z-afcc26/result.json) |

<!-- suite-qualification-ledger:end -->
