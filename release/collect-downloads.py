"""Flatten candidate payloads/reports; reject any same-name conflicting bytes."""
from pathlib import Path
import shutil
import sys
import release
source, dest = map(Path, sys.argv[1:])
dest.mkdir(parents=True, exist_ok=True)
for path in sorted(source.rglob('*')):
    if not path.is_file() or path.suffix not in ('.json', '.gz', '.zip'):
        continue
    if not (path.name.startswith(('beam-erlang-', 'candidate-', 'environment-', 'linux-', 'windows-', 'macos-', 'android-', 'ios-'))):
        continue
    target = dest/path.name
    if target.exists() and release.digest(target) != release.digest(path):
        raise ValueError('conflicting candidate files: '+path.name)
    shutil.copy2(path,target)
