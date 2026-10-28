# Release operations

`release/` is product release tooling: pinned input acquisition, archives,
provenance, qualification aggregation, publication, verification and recovery.
Runner installation lives in [ci/](../ci/README.md), behavioral tests in
[tests/](../tests/README.md), and image recipes in [containers/](../containers/README.md).

## Product versions and compatibility

BEAM follows the upstream ERTS version of its integrated runtime baseline, with
an Erlangsters distribution revision and optional preview designation.
`BEAM_VERSION` owns the exact identity, currently `17.0-r1-preview.1`:
`<ERTS>-r<revision>[-preview.<number>]`.

- ERTS has at least two dot-separated nonnegative decimal components without
  leading zeroes except `0`. Preserve upstream bases with more than three components.
- Distribution revisions and preview numbers are decimal integers from 1 through
  9007199254740991, without leading zeroes. Revisions begin at 1 for a new ERTS base.
- Compare ERTS components numerically, then revision, then preview number. Stable
  sorts after previews of the same base/revision; ordering establishes no source
  ancestry or API/ABI guarantee.
- Numeric CMake discovery projects the first three ERTS components, supplying zero
  for an absent patch. Exact SDK/native checks retain the full identity and build ID.

Adopt a new ERTS base only after integrating its upstream runtime baseline.
Changed delivered source or payload on the same base advances the distribution
revision, including compiler, library, API, CLI, component, package-tool or
packaging changes. A selected upstream patch alone does not establish adoption
of an entire later ERTS release. [Upstream synchronization](upstream-sync.md)
owns source selection, processed-through provenance and branch propagation.

The release record binds the identity to `erts_version`, `distribution_revision`,
`channel` and `preview_number`; values must agree. Build IDs and artifact digests
remain separate from human-facing product versions. Changed bytes receive a new
immutable identity; previously published payloads never change. Source provenance
is distinct from product numbering. Modern generated OTP reporting is explicit
compatibility metadata, not another BEAM version or a permanent modern API.

OTP owns its upstream application/runtime reporting and independent distribution
revision. Its exact selected SDK version, build and flavor remain explicit, with
provenance outside its faithful installation tree. A changed statically linked VM
changes that delivered OTP product without forcing simultaneous publication of
both products. Shared changes still require suitable OTP adaptation and acceptance.

Public modern interfaces remain experimental. Exact SDK selection is not a stable
ABI guarantee; consumers must rebuild/requalify against changed previews. Explicit
stabilization must separately declare Erlang behavior, C source compatibility,
C binary ABI and command guarantees, supported by retained consumer acceptance.
Stable contracts persist through their maintained release line. Planned breaks
belong at a declared generation transition after announced deprecation, migration
instructions and an earlier published migration period. Narrow correctness/security
exceptions require impact review and guidance. An independent API break needs
explicit API/ABI versioning and acceptance; a distribution revision or invented
ERTS version cannot communicate that contract by itself.

File/network/zlib components retain matching-build linkage with `libbeam`.
Private native descriptors do not establish provider ABI, independent binary
interchangeability or hot reload. The bundled package tool has its own source
identity/version but retains exact matching-build inventory and selected-payload
qualification. Independent self-upgrade compatibility requires its own boundary.

## Source and support policy

[policy.json](policy.json) registers source branches, ERTS generations, permitted
channels and mandatory target IDs. `master` permits previews; `maint-17` permits
previews and stable ERTS 17 releases. Register support explicitly and create a
maintenance branch from a reviewed product checkpoint. Do not reuse inherited
upstream `maint` or `maint-29` tips as product branches. Daily upstream audit is
read-only. An ERTS bump is a sync signal, not the sole reason to integrate fixes:
review compiler, library and build changes affecting the selected product too.

Preview and stable share one pipeline. A preview artifact cannot lose its suffix
at publication. Stable requires new source identity and newly qualified bytes.
Branch existence, retained downloads and ERTS numbering confer no LTS or stable
C API guarantee. Native descriptors remain private matching-build contracts.

[inputs.json](inputs.json) owns exact companion commits and qualification fixture
URLs, SHA-256 digests, byte sizes and producer identities. Its pending entries and
missing fixtures deliberately prevent qualification. Source pins cannot represent
uncommitted adaptations. Reference OTP installations remain tied to upstream
`550d7b7898706c7822362c42e7b93120c1d1f29a`; never fabricate provenance.
CMake performs no downloads and no external Erlang fallback. Qualification uses
independent product-owned fixtures and explicit pinned inputs.

## Maintenance and preview scope

The first public binary milestone is a developer preview of the language, CLI,
components and embedding SDK. OTP publication is scheduled independently; this
never waives applicable OTP adaptation or mandatory acceptance for shared changes.
Maintain the latest preview on a best-effort basis. No stable cadence, backport
window, LTS duration or response-time promise is established. Declaring a supported
line requires owners, scope and end conditions plus its own backport, bootstrap,
installed-SDK and product qualification work. Do not create older generations
retroactively merely because upstream maintains them.

Select upstream release tags explicitly and qualify each supported product line.
A fix affecting several generations needs deliberate adaptation on each line;
combining all upstream maintenance branches is not synchronization. A branch name,
retained archive or ERTS number never establishes source synchronization or support.

Preview eligibility requires mandatory gates on both flavors, installed/relocated/
isolated behavior, affected package workflows and applicable OTP checks on actual
reacquired archive bytes. A preview label never turns a failing required gate into
a pass. Preserve failures, named deferrals and untested coverage in their actual
classifications; any eligibility conflict requires a specific maintainer decision
under the OTP [compatibility contract](../../erlang-otp/docs/compatibility.md).
Publish issue and confidential security-reporting routes before the first download.

## Candidate production and qualification

Manually dispatch `release-candidate.yml` on the exact reviewed branch commit.
The [catalog](../ci/targets.json) supplies 26 downloadable SDK targets and ten
additional qualification routes: two Linux OTP integration targets and eight
Android Windows/macOS producer routes. All Windows compiler
variants publish; Android's Linux route is the canonical download producer.

Each worker configures optimized Release with debug information, verifies its
source-attested compiler, builds, installs, finalizes signing and integrity
metadata, then packages the complete installation. Linux/macOS/mobile use
`.tar.gz`; Windows uses `.zip`, with compiler environment in the target identity.
iOS shared runtime/components are framework bundles. Native host tools are never
installed into cross-target SDKs. Mobile archives are ordinary SDK archives;
XCFramework aggregates preserve verified device/simulator slice identities and
are delivered alongside the complete SDK archives. Android runtime/component
AARs with Prefab likewise accompany complete SDKs; aggregates do not replace
the SDK archive. OS installers remain outside this format.

Archives include dependency/license records. Source archives use exact committed
BEAM/package trees; candidate packaging rejects dirty sources. Development CI
archives are explicitly ineligible and may use diagnostic configurations.
Candidate records bind the full version, source, target, SDK identity, payload
size/hash, support archive, input pins and actual toolchain/dependency inventory.

Independent jobs reacquire archives and verify hashes before consumer compilation
and runtime acceptance. Desktop fixtures use acquired-prefix mode, including
copied, relocated and isolated cases. Package acceptance uses the exact installed
payload. Integration additionally acquires pinned OTP source/fixtures, selects
only the installed SDK, builds its independently attested host and runs the whole
configured suite. `preflight.py` requires an unprivileged account, namespaces
and private loopback for that Linux integration executor. Hosted provisioning
requires enough disposable scratch space; it does not alter developer machines.

Both Linux integration targets additionally require a `beam-vm` report from the
[BEAM-owned emulator selection](../tests/README.md#installed-otp-host-for-emulator-suites).
`qualify-otp.py` accepts `--vm-report` alongside its existing `--report`, stages
the matching OTP installation and external provenance, and runs the standalone
Common Test project against the installed OTP/SDK pair before the broader OTP
checks. Their longer timeout cannot suppress the bounded VM batch. VM preparation
or execution failure still allows OTP execution, and both outcomes remain
independent. The VM
report binds the candidate, flavor, producer provenance, selected test inputs
and individual case results. All selected cases must pass without skips or
missing results. A VM pass does not waive an OTP failure. Missing VM evidence
blocks qualification.

Mobile producers run host inventory checks without executing target code.
Acquired mobile consumers use the selected NDK/Apple toolchain. Android candidates
also require an `android-app` report from `qualify-android.py`, pairing transported
SDKs of both ABIs and the same flavor/source. It verifies installed toolchain and
native input rejection, then builds the independent signed product application
offline with provisioned Gradle/JDK/Android dependencies. The report binds exact
SDK identities, signing certificate, observed ABI/API/page size, lifecycle,
negative resource variants and AAB split installation. API 35 requires both 4 KB
and 16 KB execution without compatibility fallback; API 36 and x86-64 API 26
execution are separate gates. Both ARM64 flavors require nondebuggable physical
Release execution without an attached debugger. Missing devices or emulator
infrastructure emit nonpassing evidence. Windows Android qualification remains
deferred; existing producer routes are preserved. iOS has its own independent
signed application and physical-device gates.
Reports identify the actual platform and exercised behavior; cross-build and
packaging success do not establish application execution.

The aggregate requires every mandatory target and bound report. Missing,
modified, skipped mandatory or unexecuted evidence cannot promote. Existing wx
deferrals and the BEAM thread-name platform skip remain explicit exceptions;
known OTP failures are not waived. Diagnostic archives preserve real failures.

## Publication and recovery

`policy.json` currently disables promotion, leaves owner lists unconfigured and
retains compatibility blockers. Before enabling it, supply reviewed input pins,
release/recovery owners, confidential security reporting and public hosting.
Configure the `release` environment with reviewers, self-review prevention and
protected-branch-only deployment; signing configuration is in the
[CI manual](../ci/README.md#infrastructure-and-credentials). Private-repository
attestation eligibility and repository visibility require an explicit operating
decision; this code does not change either.

Dispatch `release-publish.yml` with the successful candidate run ID and reviewed
manifest SHA-256. The publisher verifies source/branch/channel, candidate workflow
identity, attestations, complete qualification, protected environment and exact
asset bytes. It publishes existing artifacts without rebuilding. Tags are
publication outputs. Existing conflicting assets or tags cause rejection;
matching partial uploads can be retried. Downloads are verified after upload
before the draft becomes public. Offline verification material retains its exact
subject inventory on retry.

The publisher emits `site-release.json` with target/platform download records.
The website stages the verified version, retained index and channel pointer in
one reviewed deployment. The versioned index is also the future version-manager
handoff; it does not implement a manager. A rollback changes selection to a
retained qualified version. Withdrawal retains the identity and reason and removes
that version from channel selection; neither operation rewrites old payloads.

Run `python3 -m unittest discover -s release -p 'test_*.py'` for refusal, archive,
identity and publication retry contracts. These tooling tests do not establish
native platform or OTP compatibility.

Independent package-tool releases use their own version, source identity and gates.
Bundled distributions retain exact selected-payload qualification through
`qualify-package.py`, including after SDK reacquisition. Generic runner/launcher
bootstrap can produce SDKs without package sources; CI-only SDK snapshots are
separate prereleases and never bypass normal product signing, OTP qualification,
source-input or immutable-promotion gates. Snapshot publication is disabled in its
own policy; release promotion remains governed by this directory's existing policy.

## Real iOS application gate

`ios-app` is a mandatory candidate-bound qualification report for both iOS targets.
`release/qualify-ios.py` uses the independent product fixture in
[`tests/ios-app`](../tests/ios-app/README.md), installed host tools and the acquired
target SDK. It requires signed Release application execution, responsive UI,
static package NIFs, explicit components, repeated VMs, background recovery,
pre-signing missing/corrupt resources, and copied/isolated consumer inputs.
Device compilation and simulator spawning cannot satisfy the physical-device
report. Fixtures are product-owned and self-contained. Missing device/signing access leaves
qualification nonpassing. Public promotion remains separately controlled.
