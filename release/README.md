# Preview release operations

The adopted [release strategy](../../dev-documents/distribution-release-strategy.md)
uses the integrated upstream ERTS version plus an Erlangsters distribution
revision and an explicit preview designation. The
[sync process](../../dev-documents/Upstream-Sync-Process.md) defines upstream-aligned
development and product maintenance branches. Public compatibility guarantees
require explicit stabilization. Current preview interfaces are experimental;
native component descriptors remain private matching-build contracts.

`BEAM_VERSION` contains `17.0-r1-preview.1`. The full identifier uses
`<ERTS>-r<revision>[-preview.<number>]`, with revision and preview numbers from
1 through 9007199254740991, without leading zeroes. That bound preserves exact
numeric JSON values in JavaScript consumers. ERTS retains its upstream numeric
components as strings without this field bound. CMake discovery uses
only the first three ERTS components, supplying a zero patch when absent;
the current numeric project version is `17.0.0`. Exact SDK checks continue to
use the full string and build identity. Stable syntax such as `17.0-r1` is
accepted by the parser, but the preview pipeline rejects stable publication.

The unpublished release JSON schema 1 records `version`, `erts_version`,
`distribution_revision`, `channel` (`preview` or `stable`) and `preview_number`
(positive integer or null). Candidate, aggregate and site records validate these
fields against the full identifier. SDK metadata keeps its existing schema and
full identity. Ordering compares numeric ERTS components, distribution revision
and preview number, with stable after previews of the same base and revision.
The [release strategy](../../dev-documents/distribution-release-strategy.md#product-versions-and-compatibility)
owns the complete identifier rules.

The modern root `OTP_VERSION` and `otp_versions.table` are removed. Runtime ERTS
reporting is derived from `BEAM_VERSION`; existing OTP reporting remains explicit
generator compatibility metadata until separately changed. OTP's own version
files are unchanged. Earlier `0.1.0-preview.1` evidence and artifacts retain their
actual identities. New-identity qualification remains separate; version changes
do not establish source/binary compatibility or public release readiness.

## Inputs and readiness

`inputs.json` owns exact companion commits. The package repository is public.
Current pins identify committed checkpoints, not uncommitted adaptation work.
Its `pending` list and null fixture records deliberately prevent qualification:
review and commit the package acquired-payload mode and OTP SDK adaptation, then
update the pins. Do not automatically commit unrelated working changes.

Each `qualification_assets` entry must contain an HTTPS `url`, lowercase SHA-256
`sha256`, positive byte `size`, and human-readable producer/version `identity`.
Reference archives contain one complete original OTP installation root per
flavor; provenance is the matching external installation-inventory JSON from the
artifact reproducer's sequential build, pinned to upstream
`550d7b7898706c7822362c42e7b93120c1d1f29a`. Never fabricate reference provenance.
`rebar3` is the reviewed 3.27.0 executable archive with Relx 4.10.0. `postgres`,
`ldap` and `ftp` are single-root fixture tarballs using the layouts expected by
OTP's existing helpers (including PostgreSQL 18/psqlODBC and `usr/sbin/vsftpd`).
Missing or mismatched inputs fail before OTP qualification. No CMake network
acquisition or external Erlang compiler fallback is permitted.

`policy.json` intentionally disables promotion, retains the mandatory OTP
compatibility/strict-coverage blocker, and leaves release/recovery owners empty.
Only separately reviewed compatibility evidence may remove that blocker. Configure
both owner lists and the protected `preview-release` GitHub environment with
required reviewers, self-review prevention and protected-branch-only deployment
before enabling publication. The publisher checks this configuration through the
GitHub API and runs only on master.

Publishing from a product maintenance branch is not implemented; branch-aware
qualification, protection and publication rules must be adapted and verified
before enabling it.

Release owners must publish and verify a confidential security-reporting route
before the first preview; an unavailable repository API response does not
establish that private vulnerability reporting is enabled.
Protect master and workflow/policy changes with review. Do not infer that a
candidate's existence makes it eligible.
The selected BEAM repository is currently private; package and OTP source
repositories are public. Repository visibility has not been changed by this
implementation. Public release hosting requires an explicit review of that
visibility or a separately designed public release location. The publisher
checks public visibility. GitHub [limits private-repository attestations to
Enterprise Cloud](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations);
do not assume the current private repository can run the attestation stage.

## Build and qualification flow

Ordinary Linux CI has read-only repository permissions and pinned package
acquisition. `preview-candidate.yml` is manually dispatched for reviewed source
commits. Separate emu/JIT jobs use Ubuntu 24.04 hosted runners, optimized Release
with debug information enabled before SDK identity generation, and no stripping.
They run complete BEAM CTest, install once, and archive the complete prefix.
Source archives use `git archive` of exact committed BEAM/package trees. Dirty
source trees are rejected by the release packer. Playground is never an input.

Independent jobs download and digest-check the SDK and test-support archives.
All existing installed fixtures run in acquired-prefix mode, which copies the
extracted SDK into disposable roots without installing from the producer build.
Private generated headers, expected bytecode and the fault-injection component
travel in a separate support archive, bound to the candidate. They are not
public SDK assets or an alternate runtime. Package acceptance runs all four
owning suites using the exact installed package payload. Normal build/install
acceptance and private build-time tests remain enabled.

OTP jobs explicitly acquire pinned sources and fixtures, configure using the
candidate record's SDK version/build ID/flavor, build with mandatory seed
reproduction and run the entire configured CTest selection. Preflight requires
an unprivileged account, working bubblewrap namespaces and private loopback.
Preparation is restricted to disposable GitHub-hosted runners: it removes unused
Android/.NET bundles and checks for at least 25 GiB of scratch space after
dependency installation. Full OTP suites retain separate installations and logs;
GitHub's [standard runner allocation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
alone is insufficient for this retained diagnostic footprint. Run `preflight.py`
directly for local namespace checks, not the hosted preparation script.
Registration checks reject absent mandatory tests. Reports distinguish passed,
failed, skipped, deferred and untested coverage; only owning wx deferrals and the
existing BEAM thread-name platform skip are recognized. No OTP mandatory failure
or strict-coverage skip is waived. Diagnostics upload on failure. No OTP binaries
are published: diagnostic uploads select reports, execution/Common Test logs and
crash dumps, excluding the disposable OTP installation trees.

Candidate JSON binds source commits, target/flavor, SDK identity, archive sizes
and SHA-256 digests, generated acceptance inputs and actual build/dependency
inventory. The aggregate manifest includes report content and report digests.
Missing reports cannot become eligible. Native binary reproducibility and a
minimum kernel version are not claimed.

## Protected promotion and recovery

Dispatch `preview-publish.yml` on master with the candidate run ID and reviewed
`candidate-manifest.json` SHA-256. It requires successful build/acquired/OTP jobs
for both flavors, trusted repository/manual workflow/master origin, matching
source revision and reviewed workflow bytes, valid attestations, complete inputs,
passing report content and configured owners. It promotes existing bytes without
rebuilding. Both payloads and final manifest are attested. Offline bundles and
trusted roots accompany the release; see the [user verification guide](../docs-next/getting-started/preview.md).

Publication checks all existing assets before mutation and rejects conflicting
bytes. A changed payload requires a new preview version. Partial publication
remains a draft until downloaded assets verify. Matching assets can be retried;
never use `--clobber`, replace tags or erase withdrawal records. Retries verify
and retain the existing attested offline kit and its exact subject inventory,
so refreshing trust material cannot silently change an existing asset.

The publisher emits `site-release.json` only after hosted bytes verify. The site
owner runs `scripts/stage-release.py` with the published version and verified
manifest digest; it independently verifies the publication attestation and
hosted archives. Review and deploy version record, index and preview pointer in
one normal site change. No automatic site deployment or release ownership has
been configured by adding these workflows. Missing JSON routes must return an
error, never the SPA. Withdraw by retaining the version record with `status:
withdrawn` and a reason and moving the preview pointer to null or a retained
qualified version in the same deployment. Rollback never rewrites payloads.

Run `python3 -m unittest discover -s release -p 'test_*.py'` for transport,
registration and publication refusal tests. Full native qualification requires
the owning build and installed suites, not only these infrastructure checks.
