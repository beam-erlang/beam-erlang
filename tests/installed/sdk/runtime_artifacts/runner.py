"""Installed artifact selection, provenance and isolated native deployment."""
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys

from support.process import check, isolated_environment, run


from support.scratch import scratch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    for name in ('test-dir', 'source-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    args.source_dir = args.source_dir.resolve()
    root = args.test_dir.resolve()
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    with scratch(root):
        env = isolated_environment(root)
        sdk = root / 'sdk'
        stage_installation(args, sdk)
        manifest = json.loads((sdk / 'lib/beam/runtime-artifacts.json').read_text())
        flavor = manifest['flavor']

        def configure(name, *, status=0, diagnostic=None, **variables):
            build = root / name
            command = [args.cmake, '-S', args.source_dir, '-B', build,
                       f'-DCMAKE_PREFIX_PATH={sdk}', f'-DEXPECTED_FLAVOR={flavor}',
                       f'-DEXPECTED_VERSION={expected_version()}']
            command.extend(f'-D{key}={value}' for key, value in variables.items())
            result = run(command, cwd=root, env=env, timeout=30, status=status, text=True)
            if diagnostic:
                check(diagnostic in result.stdout + result.stderr,
                      f'missing diagnostic {diagnostic}: {result.stdout}{result.stderr}')
            return build

        for phase in ('fresh', 'relocated'):
            if phase == 'relocated':
                destination = root / 'relocated SDK 日本'
                sdk.rename(destination)
                sdk = destination
            build = configure(phase)
            run([args.cmake, '--build', build], cwd=root, env=env, timeout=60)
            selection = (build / 'selection.txt').read_text().splitlines()
            check(selection[0] == manifest['build-id'], 'SDK build identity changed')
            check(selection[1] == flavor, 'SDK flavor changed')
            check(Path(selection[2]) == sdk / selection[3], 'artifact lookup escaped selected SDK')
            deployed = root / f'{phase}-deployment'
            run([args.cmake, '--install', build, '--prefix', deployed],
                cwd=root, env=env, timeout=30)
            selected = {'runtime', 'helper.child_setup'}
            expected = {manifest['artifacts'][key]['path'] for key in selected}
            headers = {'beam_native.h', 'erl_nif.h', 'erl_driver.h', 'erl_drv_nif.h',
                       'erl_nif_api_funcs.h', 'erl_int_sizes_config.h'}
            expected |= {f'usr/include/{name}' for name in headers}
            for name in headers:
                expected_hash = manifest['artifacts']['header.' + name[:-2]]['sha256']
                check(hashlib.sha256((deployed / 'usr/include' / name).read_bytes()).hexdigest() == expected_hash,
                      f'deployed native header differs: {name}')
            core = {key: record for key, record in manifest['artifacts'].items()
                    if record['kind'] == 'core-module'}
            check(len(core) == 9, 'incorrect core inventory size')
            for key, record in core.items():
                relative = 'core/ebin/' + Path(record['path']).name
                expected.add(relative)
                check(hashlib.sha256((deployed / relative).read_bytes()).hexdigest() == record['sha256'],
                      f'deployed core module differs: {key}')
            actual = {str(path.relative_to(deployed)) for path in deployed.rglob('*') if path.is_file()}
            check(actual == expected, f'unexpected deployed artifacts: {actual ^ expected}')
            for key in selected:
                record = manifest['artifacts'][key]
                path = deployed / record['path']
                check(hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256'],
                      f'deployment changed {key}')
                if record['kind'] == 'executable-helper':
                    check(os.access(path, os.X_OK), f'helper is not executable: {key}')
            relocated = root / f'{phase}-relocated-product 日本'
            deployed.rename(relocated)
            unavailable = root / 'sdk-unavailable'
            sdk.rename(unavailable)
            try:
                # Load exactly the deployed native closure in a fresh process with
                # its original SDK path absent. No VM or application is started.
                libraries = [str(relocated / manifest['artifacts'][key]['path'])
                             for key in ('runtime',)]
                clean_env = {key: value for key, value in env.items()
                             if key not in ('LD_LIBRARY_PATH', 'LD_PRELOAD')}
                run([sys.executable, '-c',
                     'import ctypes, sys; [ctypes.CDLL(path) for path in sys.argv[1:]]',
                     *libraries], cwd=root, env=clean_env, timeout=15)
            finally:
                unavailable.rename(sdk)

        configure('wrong-version', status=1, diagnostic='VERSION mismatch',
                  EXPECTED_VERSION=expected_version() + '.wrong')
        configure('wrong-flavor', status=1, diagnostic='FLAVOR mismatch',
                  EXPECTED_FLAVOR='jit' if flavor == 'emu' else 'emu')
        configure('wrong-identity', status=1, diagnostic='BUILD_ID mismatch',
                  EXPECTED_BUILD_ID='0' * 64)
        configure('unknown-artifact', status=1, diagnostic='artifact is unavailable',
                  QUERY_ARTIFACT='nif.unknown')
        configure('wrong-kind', status=1, diagnostic='kind mismatch', QUERY_KIND='driver')
        # Exercise artifact validation after CMake target import. Removing libbeam
        # itself would instead fail the imported-target existence check first.
        socket = sdk / manifest['artifacts']['helper.child_setup']['path']
        saved = socket.read_bytes()
        saved_mode = socket.stat().st_mode
        socket.unlink()
        try:
            configure('missing-artifact', status=1, diagnostic='artifact is missing',
                      QUERY_ARTIFACT='helper.child_setup', QUERY_KIND='executable-helper')
        finally:
            socket.write_bytes(saved)
            socket.chmod(saved_mode)
        socket.write_bytes(saved + b'changed')
        try:
            configure('changed-artifact', status=1, diagnostic='does not match its build inventory')
        finally:
            socket.write_bytes(saved)
        configured = configure('changed-after-configuration')
        socket.write_bytes(saved + b'changed')
        try:
            result = run([args.cmake, '--install', configured,
                          '--prefix', root / 'invalid-deployment'],
                         cwd=root, env=env, timeout=30, status=1, text=True)
            check('artifact changed after configuration' in result.stdout + result.stderr,
                  'installation accepted a changed SDK')
        finally:
            socket.write_bytes(saved)
        header = sdk / manifest['artifacts']['header.erl_driver']['path']
        saved_header = header.read_bytes()
        header_configured = configure('header-install-drift')
        header.write_bytes(saved_header + b'/* changed */')
        try:
            configure('changed-header', status=1, diagnostic='does not match its build inventory')
            result = run([args.cmake, '--install', header_configured,
                          '--prefix', root / 'invalid-header-deployment'],
                         cwd=root, env=env, timeout=30, status=1, text=True)
            check('native header changed after configuration' in result.stdout + result.stderr,
                  'header deployment accepted a changed SDK')
        finally:
            header.write_bytes(saved_header)
        core_module = sdk / manifest['artifacts']['core.erlang']['path']
        saved_core = core_module.read_bytes()
        core_configured = configure('core-install-drift')
        core_module.write_bytes(saved_core + b'changed')
        try:
            configure('changed-core', status=1, diagnostic='does not match its build inventory')
            result = run([args.cmake, '--install', core_configured,
                          '--prefix', root / 'invalid-core-deployment'],
                         cwd=root, env=env, timeout=30, status=1, text=True)
            check('core module changed after configuration' in result.stdout + result.stderr,
                  'core deployment accepted a changed SDK')
        finally:
            core_module.write_bytes(saved_core)
        metadata = sdk / 'lib/beam/sdk.toml'
        saved_metadata = metadata.read_bytes()
        metadata.write_bytes(saved_metadata + b'\n# changed\n')
        try:
            configure('changed-sdk-metadata', status=1,
                      diagnostic='metadata does not match its runtime artifact inventory')
        finally:
            metadata.write_bytes(saved_metadata)
        configure('mixed-linkage', STATIC_PROBE='ON', MIXED_LINKAGE='ON',
                  status=1, diagnostic='BEAM_RUNTIME_LINKAGE')
        for key in ('runtime.static', 'link.static', 'link.static.configuration', 'link.static.exports'):
            path = sdk / manifest['artifacts'][key]['path']
            original = path.read_bytes()
            path.write_bytes(original + b'\n# changed\n')
            try:
                configure('changed-' + key, STATIC_PROBE='ON', status=1,
                          diagnostic='does not match its build inventory')
            finally:
                path.write_bytes(original)
        # Static discovery and core inspection acquisition need no shared VM/CLI.
        (sdk / 'lib/libbeam.so').unlink()
        shutil.rmtree(sdk / 'bin')
        static = configure('static-only', STATIC_PROBE='ON')
        run([args.cmake, '--build', static], cwd=root, env=env, timeout=60)
        unavailable = root / 'static-sdk-unavailable'
        sdk.rename(unavailable)
        try:
            result = run(['./static-only/static_probe', static / 'sdk_driver_probe.so'], cwd=root, env=env, timeout=15, text=True)
            check(result.stdout.strip() == manifest['build-id'] and not result.stderr,
                  'static host identity or image discovery failed')
        finally:
            unavailable.rename(sdk)
        print(f'Runtime artifact SDK checks passed: {flavor}')


if __name__ == '__main__':
    main()
