#!/usr/bin/env bash
# GitHub-hosted Ubuntu 24.04 qualification prerequisites.
set -euo pipefail
. /etc/os-release
[[ "$ID" = ubuntu && "$VERSION_ID" = 24.04 && $(uname -m) = x86_64 ]]
[[ $(id -u) != 0 ]]
[[ "${GITHUB_ACTIONS:-}" = true && "${RUNNER_ENVIRONMENT:-}" = github-hosted ]]
# Complete OTP suites retain independent installations and diagnostics. Reclaim
# unrelated preinstalled toolchains only on the disposable hosted runner; keep
# its action runtime, Java and all selected build/test dependencies.
sudo rm -rf -- /usr/local/lib/android /usr/share/dotnet
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build perl pkg-config python3 \
  flex default-jdk libssl-dev zlib1g-dev libncurses-dev libpcre2-dev \
  unixodbc-dev libwxgtk3.2-dev libwxgtk-webview3.2-dev libglib2.0-dev \
  libgl-dev libglu1-mesa-dev bubblewrap iproute2 libpq5 libldap2 libsasl2-2
sudo apt-get clean
python3 - <<'PY'
import os, shutil
free = shutil.disk_usage(os.environ['RUNNER_TEMP']).free
print(f'Qualification scratch available: {free // (1024**3)} GiB')
if free < 25 * 1024**3:
    raise SystemExit('Full qualification requires at least 25 GiB free scratch space')
PY
# This is an ephemeral hosted runner. AppArmor's generic userns restriction
# otherwise prevents bubblewrap before any owning isolation test can execute.
sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0
python3 release/preflight.py
