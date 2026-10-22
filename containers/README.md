# Development images

The Debian 13 and Alpine 3.24 recipes consume complete, verified SDK archives.
Each distribution/flavor produces AMD64 and ARM64 images and one tested OCI index.
The [base lock](bases.json) and [target catalog](../ci/targets.json) record exact
base digests. `images.py` checks SDK digests, source execution and an installed SDK
consumer, then transports the OCI archives and recorded package inventory.

Recipe PRs may use CI SDK archives; those images are explicitly ineligible for
publication. Weekly/manual candidates consume qualified published SDKs. Image
publication verifies the reviewed manifest, workflow origin, attestation and all
OCI digests, then promotes existing bytes with `skopeo --preserve-digests`.
Immutable tags include product version, distribution, flavor and an image revision
derived from the OCI index digest. Base/package changes therefore receive a new
image revision without changing the SDK's product identity. Approved channel
aliases may move; immutable tags may not. Rollback selects a retained digest.

These are development images containing the SDK, compiler tools and headers.
Container smoke tests do not replace the runtime's platform acceptance. Actual
Docker execution is recorded separately from catalog and syntax validation.
