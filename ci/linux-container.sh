#!/usr/bin/env bash
set -euo pipefail
# This script runs inside the selected target distribution, not the runner OS.
if command -v apk >/dev/null; then
    apk add --no-cache build-base cmake ninja perl python3 git ncurses-dev linux-headers bash pkgconf pcre2-dev zlib-dev
else
    apt-get update
    apt-get install -y --no-install-recommends build-essential cmake ninja-build perl python3 git ca-certificates libncurses-dev pkg-config libpcre2-dev zlib1g-dev
fi
git config --global --add safe.directory /source
exec "$@"
