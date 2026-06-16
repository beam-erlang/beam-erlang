#!/usr/bin/env python3
"""Independent installed-SDK application deployment acceptance."""

from pathlib import Path
import argparse
import shutil

from support.bytecode import TEST_SUPPORT_DIR, RETURNING_ASSETS
from support.process import run as capture, isolated_environment, check


def run(command, cwd, env, success=True):
    result = capture(command, cwd=cwd, env=env, timeout=90, status=None, text=True)
    check(success == (result.returncode == 0),
          f"{command}: status={result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout, result.stderr


def main():
    parser = argparse.ArgumentParser()
    for name in ('cmake', 'build-dir', 'test-dir', 'source-dir'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    root = Path(args.test_dir).resolve()
    if root.exists(): shutil.rmtree(root)
    root.mkdir(parents=True)
    work = root / 'unrelated'; work.mkdir()
    env = isolated_environment(work)
    sdk = root / 'sdk'
    run([args.cmake, '--install', args.build_dir, '--prefix', sdk], work, env)
    assert 'BEAM::Distribution' not in (sdk / 'lib/cmake/BEAM/BEAMTargets.cmake').read_text()
    source = root / 'source'
    shutil.copytree(Path(args.source_dir), source)
    for case in ('bare', 'boot', 'file', 'network', 'zlib', 'program'):
        build = root / ('build-' + case)
        profile = 'bare' if case == 'bare' else 'beam-returning'
        command = [args.cmake, '-S', source, '-B', build, f'-DCMAKE_PREFIX_PATH={sdk}', f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}", f'-DPROFILE={profile}']
        if case in ('file', 'network', 'zlib', 'program'): command.append(f'-DCOMPONENTS={case}')
        run(command, work, env)
        run([args.cmake, '--build', build, '--parallel', '2'], work, env)
        stage = root / ('stage-' + case)
        run([args.cmake, '--install', build, '--prefix', stage], work, env)
        bundle = root / ('relocated 日本 é ' + case); stage.rename(bundle)
        expected = {'bin/deployment_host', 'lib/libbeam.so'}
        if case != 'bare': expected |= set(RETURNING_ASSETS)
        if case in ('file', 'network'): expected.add(f'lib/libbeam-{case}.so')
        if case == 'zlib': expected |= {'lib/libbeam-zlib.so'}
        if case == 'program': expected.add('libexec/beam/erl_child_setup')
        actual = {p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file()}
        assert actual == expected, (case, actual ^ expected)
        host = bundle / 'bin/deployment_host'
        dynamic, _ = run(['readelf', '-d', host], work, env)
        assert '$ORIGIN/../lib' in dynamic and str(sdk) not in dynamic and str(build) not in dynamic
        for generated in (build / 'beam-deployment').rglob('*'):
            if generated.is_file(): assert str(sdk) not in generated.read_text()
        test_env = dict(env, DEPLOY_EXPECT_ROOT=str(bundle))
        hidden = root / 'unavailable-sdk'; sdk.rename(hidden)
        try:
            out, err = run([host, case], work, test_env)
            entries = 0 if case == 'bare' else 2
            assert out == 'DEPLOY_ENTERED\n' * entries + 'DEPLOY_OK\n' and not err, (out, err)
            missing = []
            if case != 'bare': missing += list(RETURNING_ASSETS)
            if case in ('file', 'network'): missing.append(f'lib/libbeam-{case}.so')
            if case == 'zlib': missing += ['lib/libbeam-zlib.so']
            for name in missing:
                asset = bundle / name; held = asset.with_name(asset.name + '.held'); asset.rename(held)
                try:
                    out, err = run([host, case], work, test_env, success=False)
                    assert 'DEPLOY_OK' not in out and 'DEPLOY_ENTERED' not in out and err
                finally: held.rename(asset)
        finally: hidden.rename(sdk)
        print(f'deployment {case} ok', flush=True)
    for options in (['-DPROFILE=unknown'], ['-DPROFILE=bare', '-DCOMPONENTS=file'],
                    ['-DPROFILE=beam-returning', '-DCOMPONENTS=unknown'],
                    ['-DPROFILE=otp-boot', '-DOTP_APPLICATIONS=unknown']):
        invalid = root / 'invalid'
        if invalid.exists(): shutil.rmtree(invalid)
        run([args.cmake, '-S', source, '-B', invalid, f'-DCMAKE_PREFIX_PATH={sdk}', f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}", *options], work, env, success=False)
    # Bare SDK clients do not require an optional native component to exist.
    asset = sdk / 'lib/libbeam-network.so'; held = asset.with_suffix('.held'); asset.rename(held)
    try:
        run([args.cmake, '-S', source, '-B', root / 'bare-without-network', f'-DCMAKE_PREFIX_PATH={sdk}', f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}", '-DPROFILE=bare'], work, env)
        run([args.cmake, '-S', source, '-B', root / 'missing-network', f'-DCMAKE_PREFIX_PATH={sdk}', f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}", '-DPROFILE=beam-returning', '-DCOMPONENTS=network'], work, env, success=False)
    finally: held.rename(asset)


if __name__ == '__main__': main()
