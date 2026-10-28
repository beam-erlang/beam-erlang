# Upstream synchronization

Synchronize against an explicitly selected released upstream tag and commit,
preserving product ownership and adaptations. BEAM owns the shared VM and
modern distribution; OTP owns its product through the installed public SDK.
The [architecture](../docs/architecture.md) defines that boundary and
[release policy](README.md) defines versions, interface evolution and support.
Source integration does not establish compatibility or authorize publication.

## Branches and source selection

BEAM develops on `master`; maintenance branches use `maint-<ERTS-major>`, such as
`maint-17`. OTP's corresponding branches use its product major, such as
`maint-29`. These names do not select an SDK: OTP records the exact selected SDK
version, build and flavor. Upstream branch names retain upstream meanings.

Create a product maintenance branch from a reviewed, committed product checkpoint
before development moves to a new supported generation. Inspect dirty work,
local/remote divergence and existing refs first. Preserve unique history until
assessed; do not reset, clean, auto-commit or force-push as a precondition. A
floating `maint` alias is unnecessary. Registration in [policy.json](policy.json),
branch contents and qualification independently determine support/readiness.
Retained branches and downloads alone promise no support.

The initial source baseline is OTP 29.0 / ERTS 17.0 at
`550d7b7898706c7822362c42e7b93120c1d1f29a`. `UPSTREAM_COMMIT` contains only the
40-character SHA and a newline. Inspect it together with import history and
`Synced-to:` trailers: a live marker on a syncing branch is processed-through
provenance, not a claim of upstream source equality. A tracker tip may be later
than the selected pin and establishes neither processed source nor compatibility.

Review later releases on the selected line, including compiler, library, build
and test changes even when ERTS does not change. Multiple releases can form one
bounded review range. A new upstream release is a review candidate, not an
automatic merge or version bump. Other upstream maintenance generations remain
separate: assess each correction for applicability and integrate it once per
supported product branch. Urgent out-of-range fixes retain separate provenance;
do not advance the processed-through marker past unreviewed commits.

Use exact committed-tree exports (`git archive` or equivalent) for references
and imports. Preserve licenses, modes and notices. Record a clean base import
separately from removals, build adaptations and product changes. A reference
tracker may be fetched/advanced from upstream, never edited for product work.
No product build depends on its checkout or an alternate upstream runtime.

## Current source mapping

Retained upstream members preserve their relative paths beneath `src/`, subject
to explicit product ownership and exceptions:

| Upstream origin | BEAM owner/path |
| --- | --- |
| `erts/emulator/`, `erts/include/`, `erts/lib_src/`, `erts/preloaded/`, `erts/example/` | Corresponding retained members under `src/erts/`. |
| `lib/compiler/src/genop.tab` | `src/lib/compiler/src/genop.tab`. |
| `erts/preloaded/{src,ebin}/{atomics,counters,persistent_term}.*` | Corresponding members under `src/erts/preloaded/`. |
| `erts/emulator/test/` | Retained suites, helpers and data under `tests/vm/emulator/`, preserving suffixes. |
| `lib/common_test/test_server/ts_install_cth.erl` | `tests/vm/support/ts_install_cth.erl`; [provenance](../tests/vm/common_test/upstream.json). |
| Retained shared VM/thread-library tests | `tests/vm/`; use each file's import history and preserved notices. |
| Modern compiler/support and component adaptations | `src/beam/`; inspect per-file ancestry, attested inputs and source notices. |
| OTP application/startup/native implementations | OTP product, not an implied BEAM source destination. |

Directory mappings describe retained members, not restoration masks. Inspect
the actual target branch and file history before applying a change. Deleted
legacy build recipes can carry requirements for owned CMake implementations;
their absence does not make upstream behavioral changes irrelevant. Never apply
a blanket OTP removal mask or an automatic “always ours” merge strategy.

## Review and propagation

1. Inspect the target branch, uncommitted changes, source markers, support scope
   and desired upstream range. Preserve unrelated work and select reference
   commits explicitly.
2. Process upstream commits individually. For every path, resolve its current
   owner, retained adaptation, relocation or reviewed removal. Shared VM fixes
   can require changes to both products; directory ancestry alone is insufficient.
3. Apply relevant changes to the owning products, preserving applicable removals
   and the installed SDK boundary. Review generated inputs, debug metadata,
   source-attested seeds, native interfaces and source notices.
4. Validate affected supported interpreter/JIT builds and installed, relocated
   and isolated behavior. Preserve upstream assertions, baseline failures and
   explicit skips. Use the OTP [compatibility contract](../../erlang-otp/docs/compatibility.md)
   for its gates and stop rules. A clean merge is not runtime acceptance.
5. Record each reviewed upstream commit once per target branch, including its
   disposition. Advance the marker only through a fully reviewed range. Record
   selected out-of-range fixes separately and recognize them in later ranges.
   Omitted/irrelevant changes remain distinguishable from implemented ones.

Preserve the upstream subject, body and author when applying its change. Append
`Synced-to: <40-character SHA>`; use `Adjustment:` for meaningful adaptations or
omitted paths. Provenance describes source disposition without promising binary
equivalence. Commit and push only when authorized.

Coordinate changed SDK capabilities/identity, OTP preloads/native artifacts,
bootstrap order, compiler metadata, commands, resources and installed layout.
Assess correctness/security fixes for both products deliberately; aligned lines
do not make synchronization automatic. Preserve OTP's selected behavior through
suitable mechanisms or adaptation. Modern OTP-oriented reporting is not itself
a permanent modern public API. Stable interfaces follow the release manual's
compatibility/deprecation policy; preview interfaces remain experimental.

## New generations and audit

Select a tagged upstream commit for a new major generation, preserve product
history and reassess the entire tree against current ownership. Never replace
a maintained product branch with an upstream checkout or apply an old generation's
deletion set blindly. Update source mapping, version policy and compatibility
target with their owning acceptance. Retain prior product lines for their declared
support period; adopting a new generation neither extends nor retires that period.

The daily/manual `upstream-audit.yml` compares recorded provenance with upstream
release lines. It never merges, updates markers or creates product branches.
Nightly qualification dispatches registered lines and reports missing branches
as failures. Neither automation changes the selected source target by itself.
