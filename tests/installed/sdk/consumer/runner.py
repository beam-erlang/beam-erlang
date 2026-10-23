#!/usr/bin/env python3
"""Public C/C++ consumers and the complete installed SDK layout."""
from functools import partial
from pathlib import Path
from support.scratch import scratch
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil

from installed.sdk.layout import check_installation
from installed.sdk.consumer.bytecode import check_bytecode
from support.bytecode import TEST_SUPPORT_DIR
from support.process import check, isolated_environment, run


def exact(result, expected):
    check(result.stdout == expected and not result.stderr,
          f"unexpected consumer output: {result.stdout!r} {result.stderr!r}")


def check_runtime_only(execute, cmake, source, prefix, root, phase):
    runtime = root / f'{phase}-runtime-sdk'
    for directory in ('include', 'lib/cmake/BEAM'):
        shutil.copytree(prefix / directory, runtime / directory)
    for filename in ('lib/libbeam.so', 'lib/beam/sdk.toml', 'lib/beam/runtime-artifacts.json'):
        destination = runtime / filename
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(prefix / filename, destination)
    client = root / f'{phase}-runtime-client'
    execute([cmake, '-S', source / 'runtime_only', '-B', client,
             f'-DCMAKE_PREFIX_PATH={runtime}'])
    execute([cmake, '--build', client])
    exact(execute([client / 'runtime_sdk_only']), 'Runtime SDK only\n')
    rejected = execute([cmake, '-S', source / 'runtime_only',
                        '-B', root / f'{phase}-missing-cli',
                        f'-DCMAKE_PREFIX_PATH={runtime}', '-DREQUIRE_CLI=ON'], status=None)
    check(rejected.returncode != 0 and 'BEAM CLI component is not installed' in rejected.stderr,
          'required CLI component did not reject the runtime-only installation')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--without-package', action='store_true')
    for name in ('test-dir', 'source-dir', 'doc-example-source'):
        parser.add_argument('--' + name, type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    root = args.test_dir.resolve()
    source = args.source_dir.resolve()
    shutil.rmtree(root, ignore_errors=True)
    work = root / 'unrelated'
    work.mkdir(parents=True)
    with scratch(root):
        env = isolated_environment(work)
        execute = partial(run, cwd=work, env=env, timeout=30, text=True)
        prefix = root / 'stage'
        stage_installation(args, prefix)
        for phase in ('fresh', 'relocated'):
            if phase == 'relocated':
                destination = root / 'relocated préfix 日本'
                prefix.rename(destination)
                prefix = destination
            check_installation(prefix, bundled=not args.without_package)
            check_runtime_only(execute, args.cmake, source, prefix, root, phase)
            coexistence = root / f'{phase}-native-coexistence'
            execute([args.cmake, '-S', source / 'native_coexistence', '-B', coexistence,
                     f'-DCMAKE_PREFIX_PATH={prefix}', f'-DEXPECTED_VERSION={expected_version()}'])
            execute([args.cmake, '--build', coexistence])
            exact(execute([coexistence / 'native_coexistence']), 'Native coexistence OK\n')
            check(not (prefix / 'bin/beam_bootstrap').exists(), 'private compiler host installed')
            check(not (prefix / 'bin/beam_seed_compiler').exists(), 'private seed compiler installed')
            exact(execute([prefix / 'bin/beam', 'eval',
                           'io:format("Installed CLI~n").']), 'Installed CLI\n')
            returning = root / f'{phase}-returning-client'
            execute([args.cmake, '-S', source / 'returning', '-B', returning,
                     f'-DCMAKE_PREFIX_PATH={prefix}', f'-DEXPECTED_VERSION={expected_version()}', f'-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}'])
            execute([args.cmake, '--build', returning])
            for language in ('c', 'cxx'):
                exact(execute([returning / f'returning_{language}_host', prefix,
                               returning / 'sdk_returning.beam']), 'Installed returning SDK\n')
            if phase == 'fresh':
                check_bytecode(execute, args.cmake, source / 'returning', prefix, returning, root)
        sample = root / 'first-call'
        execute([args.cmake, '-S', args.doc_example_source, '-B', sample,
                 f'-DCMAKE_PREFIX_PATH={prefix}', f'-DEXPECTED_VERSION={expected_version()}'])
        execute([args.cmake, '--build', sample])
        exact(execute([sample / 'first-call']), '42\n')


if __name__ == '__main__':
    main()
