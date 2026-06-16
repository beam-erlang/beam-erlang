#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Aggregate two verified installed iOS SDKs without changing either prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile
import tomllib


def run(*args):
    subprocess.run(list(map(str, args)), check=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sdk(prefix, variant, cmake):
    prefix = prefix.resolve(strict=True)
    run(cmake, '-P', prefix / 'lib/cmake/BEAM/support/BEAMVerifySDK.cmake')
    facts = tomllib.loads((prefix / 'lib/beam/target.toml').read_text())
    if (facts['system'], facts['architecture'], facts['apple-variant'], facts['flavor']) != ('iOS', 'arm64', variant, 'emu'):
        raise ValueError('Expected an arm64 iOS interpreter ' + variant + ' SDK')
    inventory = json.loads((prefix / 'lib/beam/runtime-artifacts.json').read_text())
    frameworks = {}
    for name in ('runtime', 'component.file', 'component.network', 'component.zlib'):
        record = inventory['artifacts'][name]
        path = prefix / record['path']
        if not path.resolve().is_relative_to(prefix) or digest(path) != record['sha256']:
            raise ValueError('SDK artifact failed verification: ' + name)
        framework = path.parent
        if framework.suffix != '.framework':
            raise ValueError('Expected a dynamic framework: ' + str(path))
        frameworks[name] = framework
    return facts, frameworks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--device-sdk', type=Path, required=True)
    p.add_argument('--simulator-sdk', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cmake', default='cmake')
    a = p.parse_args()
    if not os.environ.get('DEVELOPER_DIR'):
        p.error('Select Xcode explicitly with DEVELOPER_DIR')
    device, df = sdk(a.device_sdk, 'device', a.cmake)
    simulator, sf = sdk(a.simulator_sdk, 'simulator', a.cmake)
    for key in ('compiler-generation', 'baseline'):
        if device[key] != simulator[key]:
            raise ValueError('SDK slices disagree on ' + key)
    output = a.output.absolute()
    if output.exists():
        p.error('Output already exists; choose a new aggregate destination')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.beam-xcframework-', dir=output.parent) as temporary:
        stage = Path(temporary)
        provenance = {'format': 1, 'slices': {'device': device, 'simulator': simulator}, 'frameworks': {}}
        for key, framework in df.items():
            name = framework.name.removesuffix('.framework')
            product = stage / (name + '.xcframework')
            run('xcodebuild', '-create-xcframework', '-framework', framework, '-framework', sf[key], '-output', product)
            info = plistlib.loads((product / 'Info.plist').read_bytes())
            if len(info['AvailableLibraries']) != 2:
                raise ValueError('Unexpected XCFramework slice inventory')
            slices = {}
            for library in info['AvailableLibraries']:
                variant = 'simulator' if library.get('SupportedPlatformVariant') == 'simulator' else 'device'
                if library['SupportedPlatform'] != 'ios' or library['SupportedArchitectures'] != ['arm64'] or variant in slices:
                    raise ValueError('Incorrect XCFramework slice platform')
                binary = product / library['LibraryIdentifier'] / library['LibraryPath'] / name
                source = (sf if variant == 'simulator' else df)[key] / name
                if digest(binary) != digest(source):
                    raise ValueError('XCFramework changed verified framework bytes')
                slices[variant] = {'identifier': library['LibraryIdentifier'], 'sdk-build-id': (simulator if variant == 'simulator' else device)['sdk-build-id'], 'sha256': digest(binary)}
            provenance['frameworks'][name] = slices
            if key == 'runtime':
                # A framework that copies its binary but cannot import its public
                # module is not a usable XCFramework. Compile only on the host.
                with tempfile.TemporaryDirectory(prefix='.module-check-', dir=stage) as check:
                    check = Path(check)
                    source = check / 'import.swift'
                    source.write_text('import BEAM\nlet version: UnsafePointer<CChar>? = beam_version()\n')
                    for variant, apple_sdk in (('device', 'iphoneos'), ('simulator', 'iphonesimulator')):
                        sysroot = subprocess.check_output(['xcrun', '--sdk', apple_sdk, '--show-sdk-path'], text=True).strip()
                        triple = 'arm64-apple-ios' + device['baseline'] + ('-simulator' if variant == 'simulator' else '')
                        run('xcrun', '--sdk', apple_sdk, 'swiftc', '-typecheck', '-target', triple,
                            '-sdk', sysroot, '-F', product / slices[variant]['identifier'],
                            '-module-cache-path', check / 'cache', source)
        (stage / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        # Publish a complete aggregate once, on the destination filesystem.
        os.rename(stage, output)
    print(output)


if __name__ == '__main__':
    main()
