# Release operations

`release/` is product release tooling: pinned input acquisition, archives,
provenance, qualification aggregation, publication, verification and recovery.
Runner installation lives in [ci/](../ci/README.md), behavioral tests in
[tests/](../tests/README.md), and image recipes in [containers/](../containers/README.md).

The adopted [release strategy](../../dev-documents/distribution-release-strategy.md)
and [upstream sync process](../../dev-documents/Upstream-Sync-Process.md) define
ERTS-aligned generations. `BEAM_VERSION` contains the exact product identity,
currently `17.0-r1-preview.1`: `<ERTS>-r<revision>[-preview.<number>]`.
Revision/preview numbers are positive integers no greater than 9007199254740991.
Numeric CMake discovery projects the first three ERTS components; exact SDK and
native component checks retain the full identity and build ID. The modern root
`OTP_VERSION` and `otp_versions.table` are removed. Generated OTP reporting remains
explicit compatibility metadata pending a separate interface change.

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
CMake performs no downloads and no external Erlang fallback. Playground is never
an input, published asset or qualification substitute.

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
AAR/Prefab, XCFramework aggregation and OS installers remain outside this format.

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

Mobile producers run host inventory checks without executing target code.
Acquired mobile consumers use the selected NDK/Apple toolchain and an emulator or
simulator. Device SDK compilation and packaging are checked; physical execution
remains untested. Disabled Android ARM64 infrastructure emits unexecuted evidence.
Current portable fixture coverage and platform validation limits are listed in
[architecture evidence](../../dev-documents/workflow-cmake-architecture.md).

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
