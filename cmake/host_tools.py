"""Attest reusable native bootstrap builds and their complete tool payload."""
import argparse
import hashlib
import json
import platform
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity(root):
    paths = [root / 'CMakeLists.txt', root / 'BEAM_VERSION']
    for folder in ('src', 'cmake', 'emulator', 'include', 'lib_src', 'lib/compiler'):
        paths.extend(p for p in (root / folder).rglob('*') if p.is_file()
                     and '__pycache__' not in p.parts and p.suffix != '.pyc')
    return hashlib.sha256(''.join(f'{p.relative_to(root)}:{sha(p)}\n'
                                 for p in sorted(set(paths))).encode()).hexdigest()


def record(source, build):
    paths = []
    for folder in ('bin', 'lib', 'compiler-seed'):
        paths.extend(p for p in (build / folder).rglob('*') if p.is_file())
    suffix = '.exe' if platform.system() == 'Windows' else ''
    for name in ('beam_bootstrap', 'beam_seed_compiler', 'yielding_c_fun_host'):
        if not (build / 'bin' / (name + suffix)).is_file():
            raise ValueError('missing native tool: ' + name)
    if not (build / 'compiler/verified.stamp').is_file():
        raise ValueError('compiler source/seed reproduction has not passed')
    cache = (build / 'CMakeCache.txt').read_text()
    if 'BEAM_HOST_TOOLS_ROOT:PATH=\n' not in cache:
        raise ValueError('host tools must be built locally, not re-exported')
    return {'schema': 1, 'source': source_identity(source), 'root': str(build),
            'system': platform.system(), 'machine': platform.machine().lower(),
            'configuration': sha(build / 'CMakeCache.txt'),
            'files': {str(p.relative_to(build)): sha(p) for p in sorted(paths)}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=['create', 'verify'])
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    a = p.parse_args()
    build = a.build.resolve()
    current = record(a.source.resolve(), build)
    manifest = build / 'host-tools.json'
    if a.command == 'create':
        manifest.write_text(json.dumps(current, indent=2, sort_keys=True) + '\n')
    elif json.loads(manifest.read_text()) != current:
        raise ValueError('native tools differ from recorded source, host, configuration or payload')

if __name__ == '__main__':
    main()
