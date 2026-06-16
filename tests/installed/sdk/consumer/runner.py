#!/usr/bin/env python3
"""Public C/C++ consumers and the complete installed SDK layout."""
from functools import partial
from pathlib import Path
from support.scratch import scratch
from support.artifacts import relative as artifact_relative
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil
import sys
import tomllib

from installed.sdk.layout import check_installation
from installed.sdk.consumer.bytecode import check_bytecode
from installed.sdk.consumer.pkgconfig import check_pkgconfig
from installed.sdk.consumer.documentation import check_documentation
from support.bytecode import TEST_SUPPORT_DIR
from support.process import check, isolated_environment, run


def exact(result, expected):
    check(result.stdout == expected and not result.stderr,
          f"unexpected consumer output: {result.stdout!r} {result.stderr!r}")


def check_runtime_only(execute, cmake, source, prefix, root, phase):
    runtime = root / f'{phase}-runtime-sdk'
    for directory in ('include', 'lib/cmake/BEAM'):
        shutil.copytree(prefix / directory, runtime / directory)
    for filename in (artifact_relative(prefix,'runtime'), 'lib/beam/sdk.toml', 'lib/beam/runtime-artifacts.json',
                     'lib/beam/target.toml', 'lib/beam/compiler-generation.json'):
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
    for tool in ('pkg-config', 'compiler', 'cxx-compiler'):
        parser.add_argument('--' + tool, type=Path, required=True)
    parser.add_argument('--without-package', action='store_true')
    parser.add_argument('--opposite-libc-sdk', type=Path,
                        help='Explicit real Linux SDK for cross-libc rejection checks')
    for name in ('test-dir', 'source-dir'):
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
        if sys.platform.startswith('linux'):
            facts = tomllib.loads((prefix/'lib/beam/target.toml').read_text())
            opposite = 'glibc' if facts['libc']=='musl' else 'musl'
            for language in ('C','CXX'):
                project = root/('wrong-libc-'+language)
                project.mkdir()
                (project/'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.25)\n'
                    f'project(wrong_libc LANGUAGES {language})\n'
                    f'include("{prefix}/lib/cmake/BEAM/support/BEAMTarget.cmake")\n'
                    f'beam_check_compiler_target(SYSTEM Linux ARCHITECTURE {facts["architecture"]} LIBC {opposite})\n')
                result = execute([args.cmake,'-S',project,'-B',project/'build'],status=None)
                check(result.returncode != 0 and 'compiler libc mismatch' in result.stderr,
                      'opposite libc declaration was accepted')
                if args.opposite_libc_sdk:
                    (project/'CMakeLists.txt').write_text(
                        'cmake_minimum_required(VERSION 3.25)\n'
                        f'project(wrong_sdk LANGUAGES {language})\nfind_package(BEAM CONFIG REQUIRED)\n')
                    result = execute([args.cmake,'-S',project,'-B',project/'other-build',
                        f'-DBEAM_DIR={args.opposite_libc_sdk}/lib/cmake/BEAM'],status=None)
                    check(result.returncode != 0 and 'compiler libc mismatch' in result.stderr,
                          'real opposite-libc SDK was accepted by native consumer')
        for phase in ('fresh', 'relocated'):
            if phase == 'relocated':
                destination = root / 'relocated préfix 日本'
                prefix.rename(destination)
                prefix = destination
            check_installation(prefix, bundled=not args.without_package)
            check_documentation(prefix)
            if sys.platform != 'win32':
                check_pkgconfig(pkg_config=args.pkg_config, compilers={'c': args.compiler, 'cxx': args.cxx_compiler},
                                prefix=prefix, source=source / 'runtime_only/main.c',
                                root=root / (phase + '-pkgconfig'), environment=env)
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
        copied = root / 'copied first-call 日本'
        shutil.copytree(prefix / 'share/doc/beam/examples/first-call', copied)
        execute([args.cmake, '-S', copied, '-B', sample,
                 f'-DCMAKE_PREFIX_PATH={prefix}', f'-DEXPECTED_VERSION={expected_version()}'])
        execute([args.cmake, '--build', sample])
        exact(execute([sample / 'first-call']), '42\n')


if __name__ == '__main__':
    main()
