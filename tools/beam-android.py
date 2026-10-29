#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify Android native closure, produce Prefab AARs and stage explicit assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import tempfile
import tomllib
import zipfile

SYSTEM = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so'}
MACHINES = {'arm64-v8a': 183, 'x86_64': 62}
COMPONENTS = ('runtime', 'file', 'network', 'zlib')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(root, value):
    p = PurePosixPath(value)
    if p.is_absolute() or not p.parts or any(x in ('', '.', '..') for x in value.split('/')) or '\\' in value:
        raise ValueError('Invalid relative inventory path: ' + value)
    path = root / value
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError('Inventory escapes its root: ' + value)
    return path


def elf_bytes(data, abi, name):
    """Read only public ELF64 program headers; works on stripped Android images."""
    if len(data) < 64 or data[:7] != b'\x7fELF\x02\x01\x01':
        raise ValueError('Expected little-endian ELF64: ' + name)
    h = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if h[1] != 3 or h[2] != MACHINES[abi] or h[9] != 56:
        raise ValueError('ELF kind or ABI mismatch: ' + name)
    loads, dynamic = [], []
    for i in range(h[10]):
        ph = struct.unpack_from('<IIQQQQQQ', data, h[5] + i * h[9])
        kind, flags, off, va, _, size, memsz, alignment = ph
        if off + size > len(data) or size > memsz:
            raise ValueError('Invalid ELF segment: ' + name)
        if kind == 1:
            if alignment < 16384 or alignment & (alignment - 1) or (va - off) % 16384:
                raise ValueError('ELF does not support 16 KB pages: ' + name)
            if flags & 3 == 3:
                raise ValueError('Writable executable ELF segment: ' + name)
            loads.append((va, off, size))
        if kind == 2:
            for pos in range(off, off + size, 16):
                tag, value = struct.unpack_from('<qQ', data, pos)
                if tag == 0:
                    break
                dynamic.append((tag, value))
    if not loads or not dynamic:
        raise ValueError('ELF lacks loadable/dynamic segments: ' + name)
    strings = [value for tag, value in dynamic if tag == 5]
    sizes = [value for tag, value in dynamic if tag == 10]
    if len(strings) != 1 or len(sizes) != 1:
        raise ValueError('Invalid ELF string table: ' + name)
    offsets = [off + strings[0] - va for va, off, size in loads if va <= strings[0] and strings[0] + sizes[0] <= va + size]
    if len(offsets) != 1:
        raise ValueError('Unmapped ELF string table: ' + name)
    table = data[offsets[0]:offsets[0] + sizes[0]]
    def string(offset):
        end = table.find(b'\0', offset)
        if offset >= len(table) or end < 0:
            raise ValueError('Invalid ELF string: ' + name)
        return table[offset:end].decode('utf-8')
    needed = [string(v) for t, v in dynamic if t == 1]
    sonames = [string(v) for t, v in dynamic if t == 14]
    paths = [string(v) for t, v in dynamic if t in (15, 29)]
    if len(sonames) != 1 or sonames[0] != name or any('/' in n or not n.endswith('.so') for n in needed):
        raise ValueError('ELF SONAME/dependency is not a unique library basename: ' + name)
    if any(p not in ('', '$ORIGIN', '${ORIGIN}') for p in paths):
        raise ValueError('Unsupported ELF search path: ' + name)
    return {'soname': sonames[0], 'needed': needed, 'load-alignment': 16384}


def closure(paths, abi):
    records = {}
    for path in paths:
        if path.name in SYSTEM:
            raise ValueError('Application must not supply an Android system library: ' + path.name)
        if path.name in records:
            raise ValueError('Conflicting native library name: ' + path.name)
        records[path.name] = elf_bytes(path.read_bytes(), abi, path.name)
        records[path.name]['sha256'] = digest(path)
    for name, record in records.items():
        missing = set(record['needed']) - SYSTEM - records.keys()
        if missing:
            raise ValueError('Unsupported native dependency closure for ' + name + ': ' + ', '.join(sorted(missing)))
    return records


def sdk(prefix, cmake):
    prefix = prefix.resolve(strict=True)
    subprocess.run([cmake, '-P', str(prefix / 'lib/cmake/BEAM/support/BEAMVerifySDK.cmake')], check=True)
    facts = tomllib.loads((prefix / 'lib/beam/target.toml').read_text())
    if (facts['system'] != 'Android' or facts['android-abi'] not in MACHINES or facts['baseline'] != '26'
            or facts['ndk-version'] != '28.2.13676358' or facts['cxx-runtime'] != 'c++_shared'):
        raise ValueError('Unsupported Android SDK target/toolchain')
    inventory = json.loads((prefix / 'lib/beam/runtime-artifacts.json').read_text())
    paths = {key: relative(prefix, record['path']) for key, record in inventory['artifacts'].items()}
    natives = [paths['runtime'], paths['runtime.cxx']] + [paths["library." + c + ".shared"] for c in COMPONENTS[1:]]
    closure(natives, facts['android-abi'])
    return prefix, facts, paths


def publish(output, operation):
    output = output.absolute()
    if output.exists():
        raise ValueError('Output already exists: ' + str(output))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.beam-android-', dir=output.parent) as temp:
        stage = Path(temp) / 'result'
        stage.mkdir()
        operation(stage)
        os.rename(stage, output)


def json_write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def aar(args):
    slices = [sdk(p, args.cmake) for p in args.sdk]
    if {s[1]['android-abi'] for s in slices} != set(MACHINES) or len(slices) != 2:
        raise ValueError('AAR delivery requires exactly one SDK for each ABI')
    for key in ('flavor', 'compiler-generation', 'baseline', 'ndk-version'):
        if len({s[1][key] for s in slices}) != 1:
            raise ValueError('AAR SDK slices disagree on ' + key)
    flavor = slices[0][1]['flavor']
    def assemble(stage):
        provenance = {'format': 1, 'flavor': flavor, 'slices': {s[1]['android-abi']: s[1] for s in slices}, 'aars': {}}
        for component in COMPONENTS:
            package = 'beam_' + component
            module = 'beam' if component == 'runtime' else 'beam-' + component
            payload = stage / package
            payload.mkdir()
            (payload / 'AndroidManifest.xml').write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.erlangsters.' + package + '"><uses-sdk android:minSdkVersion="26"/></manifest>\n')
            json_write(payload / 'prefab/prefab.json', {'schema_version': 2, 'name': package, 'dependencies': [] if component == 'runtime' else ['beam_runtime']})
            json_write(payload / 'prefab/modules' / module / 'module.json', {'export_libraries': [] if component == 'runtime' else ['//beam_runtime:beam']})
            for prefix, facts, paths in slices:
                abi = facts['android-abi']
                target = payload / 'prefab/modules' / module / 'libs' / ('android.' + abi)
                target.mkdir(parents=True)
                key = 'runtime' if component == 'runtime' else "library." + component + ".shared"
                shutil.copy2(paths[key], target / paths[key].name)
                json_write(target / 'abi.json', {'abi': abi, 'api': 26, 'ndk': 28, 'stl': 'c++_shared', 'static': False})
                # Generated SDK headers bind the exact ABI/flavor/build selection.
                if component == 'runtime':
                    shutil.copytree(prefix / 'include', target / 'include')
                else:
                    (target / 'include').mkdir()
                json_write(target / 'include/beam_android_provenance.json', {'target': facts, 'artifact': {'name': key, 'sha256': digest(paths[key])}})
                license_root = prefix / 'share/licenses'
                if not license_root.is_dir():
                    raise ValueError('SDK license inventory is missing')
                if not (payload / 'META-INF/licenses').exists():
                    shutil.copytree(license_root, payload / 'META-INF/licenses')
            archive = stage / (package.replace('_', '-') + '-' + flavor + '.aar')
            with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
                for path in sorted(payload.rglob('*')):
                    if path.is_file():
                        z.write(path, path.relative_to(payload).as_posix())
            provenance['aars'][component] = {'path': archive.name, 'sha256': digest(archive)}
            shutil.rmtree(payload)
        json_write(stage / 'provenance.json', provenance)
    publish(args.output, assemble)


def assets(args):
    prefix, facts, paths = sdk(args.sdk, args.cmake)
    export = (args.export / 'current').resolve(strict=True)
    meta = tomllib.loads((export / 'export.toml').read_text())
    if meta['format'] != 1 or meta['sdk'] != tomllib.loads((prefix / 'lib/beam/sdk.toml').read_text()):
        raise ValueError('Package export selects a different Android SDK')
    natives = [paths['runtime'], paths['runtime.cxx']] + [paths["library." + c + ".shared"] for c in args.component]
    copies = []
    for record in meta['packages'].values():
        root = relative(export, record['path'])
        for path, expected in record['files'].items():
            source = relative(root, path)
            if digest(source) != expected:
                raise ValueError('Package export integrity failure: ' + path)
        m = record['metadata']
        for native in m.get('native', {}).get('artifacts', {}).values():
            if native['kind'] == 'shared':
                natives.append(relative(root, native['path']))
        exported = m.get('exports', {})
        for path in ['ebin/' + n + '.beam' for n in m['modules']] + exported.get('resources', []) + exported.get('licenses', []):
            copies.append((relative(root, path), record['path'] + '/' + path))
    native_records = closure(natives, facts['android-abi'])
    def assemble(stage):
        target = stage / 'beam' / facts['android-abi']
        target.mkdir(parents=True)
        for source, dest in copies:
            out = relative(target, dest)
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, out)
        for inventory in args.inventory:
            path = prefix / 'lib/beam' / inventory
            shutil.copytree(path, target / 'lib/beam' / inventory)
        shutil.copy2(export / 'export.toml', target / 'export.toml')
        json_write(target / 'assembly.json', {'format': 1, 'target': facts, 'native': native_records})
    publish(args.output, assemble)


def apk_set(apks):
    """Validate the installed APK set, including base and ABI configuration splits."""
    records, artifacts = {}, []
    for apk in apks:
        with zipfile.ZipFile(apk) as z, apk.open('rb') as raw:
            seen = set()
            for entry in z.infolist():
                name = entry.filename
                if name in seen:
                    raise ValueError('Duplicate entry inside APK: ' + name)
                seen.add(name)
                if name.startswith('lib/') and name.endswith('.so'):
                    parts = name.split('/')
                    if len(parts) != 3 or parts[1] not in MACHINES:
                        raise ValueError('Unexpected APK native entry: ' + name)
                    if parts[2] in SYSTEM:
                        raise ValueError('APK must not supply an Android system library: ' + name)
                    if entry.compress_type != zipfile.ZIP_STORED:
                        raise ValueError('APK native library is compressed: ' + name)
                    raw.seek(entry.header_offset)
                    header = raw.read(30)
                    filename_size, extra_size = struct.unpack_from('<HH', header, 26)
                    if (entry.header_offset + 30 + filename_size + extra_size) % 16384:
                        raise ValueError('APK native library is not 16 KB aligned: ' + name)
                    by_abi = records.setdefault(parts[1], {})
                    if parts[2] in by_abi:
                        raise ValueError('Conflicting library across APK set: ' + name)
                    by_abi[parts[2]] = elf_bytes(z.read(entry), parts[1], parts[2])
                if (name.endswith(('.a', '.h', '.hpp', '.cmake', '.bpkg', '.bpkg.tar.gz')) or
                        '/cmake/' in name or name.split('/')[-1] in
                        ('beam', 'beam-pkg', 'beam_bootstrap', 'beam_seed_compiler', 'erl_child_setup')):
                    raise ValueError('Development input leaked into APK: ' + name)
        artifacts.append({'name': apk.name, 'sha256': digest(apk)})
    for abi, libraries in records.items():
        if 'libbeam.so' not in libraries or 'libc++_shared.so' not in libraries:
            raise ValueError('Incomplete application native closure for ' + abi)
        for name, facts in libraries.items():
            if set(facts['needed']) - SYSTEM - libraries.keys():
                raise ValueError('Missing APK native dependency: ' + name)
    if not records:
        raise ValueError('APK set contains no selected native runtime')
    return {'format': 1, 'native': records, 'artifacts': artifacts}


def verify_apk(args):
    print(json.dumps(apk_set(args.apk), indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('aar')
    p.add_argument('--sdk', type=Path, action='append', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cmake', default='cmake')
    p.set_defaults(action=aar)
    p = commands.add_parser('assets')
    for key in ('sdk', 'export', 'output'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--component', choices=COMPONENTS[1:], action='append', default=[])
    p.add_argument('--inventory', choices=('library', 'services', 'boot'), action='append', default=[])
    p.add_argument('--cmake', default='cmake')
    p.set_defaults(action=assets)
    p = commands.add_parser('verify-apk')
    p.add_argument('--apk', type=Path, action='append', required=True)
    p.set_defaults(action=verify_apk)
    args = parser.parse_args()
    args.action(args)


if __name__ == '__main__':
    main()
