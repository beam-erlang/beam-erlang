# Installing and maintaining a preview

No qualified public preview is currently available. These instructions describe
the current candidate pipeline. The adopted release model uses the integrated
upstream ERTS version plus an Erlangsters distribution revision and a preview
designation, currently `17.0-r1-preview.1`.

The filenames below illustrate that candidate; they do not name a qualified
public download. After a release appears on the
[download page](https://beam.erlangsters.org/download), use its verified manifest
and exact archive/root names. See [release operations](../../release/README.md)
for identifier rules and publication readiness.

## Select and verify

Select the archive matching OS, architecture, libc, compiler environment and
flavor. The catalog includes Debian 13, Alpine 3.24, Windows MSVC/MSYS2, macOS
Apple Silicon, Android and iOS. Every archive provides the same SDK interfaces
for its target; availability requires completed qualification. Android downloads
come from the Linux build route. iOS supports the interpreter and contains
framework bundles for device or simulator. Windows ZIP names distinguish MSVC,
UCRT64, CLANG64 and CLANGARM64. See the [build matrix](../../ci/README.md). The release manifest records the actual build kernel,
compiler, flags, system packages and required runtime libraries. It does not
promise portability to other distributions or native binary reproducibility.
Debug information is retained in optimized Release binaries.

Obtain `release-manifest.json`, the archive, and the offline verification bundle
from the same versioned GitHub release. Verify the manifest with GitHub CLI:

```sh
gh attestation verify release-manifest.json --repo beam-erlang/beam-erlang \
  --signer-workflow beam-erlang/beam-erlang/.github/workflows/release-publish.yml \
  --deny-self-hosted-runners
```

Compare the archive's SHA-256 and byte size with the verified manifest. Then
verify its producer attestation, whose expected workflow is
`beam-erlang/beam-erlang/.github/workflows/release-candidate.yml`:

```sh
gh attestation verify beam-erlang-17.0-r1-preview.1-linux-debian13-amd64-gcc-jit.tar.gz \
  --repo beam-erlang/beam-erlang \
  --signer-workflow beam-erlang/beam-erlang/.github/workflows/release-candidate.yml \
  --deny-self-hosted-runners
```

Before transfer to an offline machine, verify `offline-verification.tar.gz` with
the publication workflow identity above, using independently obtained trusted
roots. Unpack the verified kit; its subject inventory binds the intended payloads. Add
`--bundle offline-verification/sha256:<artifact-digest>.jsonl` and
`--custom-trusted-root offline-verification/trusted_root.jsonl` to the verification
command. Before going offline, independently obtain the expected repository ID
with `gh api repos/beam-erlang/beam-erlang --jq .id`. Compare it with each verified
certificate's `sourceRepositoryIdentifier` using `--format json`; do not trust a
repository name or a bundled ID file alone. Refresh trusted roots when importing
new signed material. Follow [GitHub's offline verification procedure](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/verify-attestations-offline).

## Install and first use

Extract a verified archive into a fresh user-owned directory. Do not overlay an
existing installation. The archive contains one installation root. Tar archives preserve executable
modes and symbolic links; extract Windows ZIPs with `Expand-Archive`. Mobile
SDKs are linked into applications using the matching target toolchain. The
example below uses Debian AMD64 JIT.

```sh
mkdir -p "$HOME/opt"
tar -xzf beam-erlang-17.0-r1-preview.1-linux-debian13-amd64-gcc-jit.tar.gz -C "$HOME/opt"
export BEAM_PREFIX="$HOME/opt/beam-erlang-17.0-r1-preview.1-linux-debian13-amd64-gcc-jit"
"$BEAM_PREFIX/bin/beam" version
"$BEAM_PREFIX/bin/beam" help
"$BEAM_PREFIX/bin/beam" eval 'io:format("Hello, BEAM!~n").'
"$BEAM_PREFIX/bin/beam" shell
```

Use an explicit executable path or prepend this installation's `bin` to `PATH`.
Several installations may coexist. Project `.beam` dependency environments do
not install or select a runtime. Package manifests and commands remain
experimental; the hosted registry is unavailable.

## Limits, reports and maintenance

This is a BEAM/Erlang preview, not an Erlang/OTP binary distribution. It does not
provide a stable native component ABI, a stable SDK promise or production/LTS
support. Native components and embedding consumers require the exact selected
SDK identity, including the preview suffix and matching build ID. Numeric CMake
version discovery alone is insufficient.

The current maintenance scope is the latest preview on a best-effort basis;
retaining an older download does not promise fixes for that release. Future
stable public interfaces will declare their source or binary compatibility and
deprecation guarantees explicitly. ERTS-aligned numbering and maintenance
branch names do not confer those guarantees on the current experimental SDK.

Report problems in the [runtime repository](https://github.com/beam-erlang/beam-erlang/issues),
including full version, flavor, manifest digest, OS/kernel, the command and a
small reproducer. Include diagnostics without credentials or private data.
Package policy issues belong in the
[package repository](https://github.com/beam-erlang/beam-erlang-package/issues).
A verified confidential security-reporting route must accompany the first public
preview; it has not yet been established in this documentation. Do not
post sensitive vulnerability details in a public issue.

Updates install into a new directory. Test your workload before switching the
explicit path or `PATH`. A published payload never changes under the same version;
a correction needs a new preview. Remove an unused prefix only after consumers
stop using it. Rollback selects a retained, qualified release. Withdrawn records
remain visible with their reason, and cannot be selected as the current preview.
