"""Direct native consumers of the shared-runtime pkg-config interface."""
import json
import os
from pathlib import Path
import shlex
import shutil
import tomllib

from support.artifacts import relative
from support.process import check, run


def flags(output):
    # pkgconf can shell-escape individual UTF-8 bytes. Preserve those bytes
    # until tokenization, including on Windows where paths use forward slashes.
    return [word.encode('utf-8', 'surrogateescape').decode('utf-8')
            for word in shlex.split(output.decode('utf-8', 'surrogateescape'))]


def check_pkgconfig(*, pkg_config, compilers, prefix, source, root, environment,
                    msvc=False, link_library=None, inspector=None, owned_helpers=()):
    root.mkdir(parents=True)
    sdk = root / 'runtime SDK 日本'
    for directory in ('include', 'lib/pkgconfig'):
        shutil.copytree(prefix / directory, sdk / directory)
    metadata = tomllib.loads((prefix / 'lib/beam/sdk.toml').read_text())
    raw = (sdk / 'lib/pkgconfig/beam.pc').read_text()
    check(str(prefix) not in raw and str(Path(source).resolve().parents[5]) not in raw,
          'pkg-config metadata retains a development prefix')
    catalog = json.loads((prefix / 'lib/beam/runtime-artifacts.json').read_text())
    inputs = {'lib/beam/sdk.toml', 'lib/beam/target.toml',
              'lib/beam/compiler-generation.json', 'lib/beam/runtime-artifacts.json',
              relative(prefix, 'runtime')}
    if os.name == 'nt':
        inputs.add('lib/' + link_library)
        inputs.update(record['path'] for record in catalog['artifacts'].values()
                      if record['kind'] == 'runtime-dependency')
    for name in inputs:
        destination = sdk / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(prefix / name, destination)
    env = {k: v for k, v in environment.items() if not k.upper().startswith('PKG_CONFIG')}
    env['PKG_CONFIG_LIBDIR'] = str(sdk / 'lib/pkgconfig')
    env['PKG_CONFIG_PATH'] = ''

    def execute(command, **kwargs):
        return run(command, cwd=root, env=env, timeout=60, owned_helpers=owned_helpers, **kwargs)

    def query(*arguments):
        result = execute([pkg_config, *arguments, 'beam'])
        check(not result.stderr, f'pkg-config diagnostics: {result.stderr!r}')
        return result.stdout

    query('--validate')
    check(query('--modversion').decode().strip() == metadata['identity'], 'pkg-config release mismatch')
    for name, key in (('build_id', 'build-id'), ('flavor', 'flavor'), ('target_triple', 'triple')):
        check(query('--variable=' + name).decode().strip() == metadata[key], f'pkg-config {name} mismatch')
    for name, path in (('sdk_metadata', 'lib/beam/sdk.toml'), ('target_metadata', 'lib/beam/target.toml'),
                       ('artifact_catalog', 'lib/beam/runtime-artifacts.json')):
        value = flags(query('--variable=' + name))
        check(len(value) == 1 and Path(value[0]).resolve() == (sdk / path).resolve(),
              f'pkg-config {name} does not relocate')
    syntax = ['--msvc-syntax'] if msvc else []
    cflags = flags(query(*syntax, '--cflags'))
    libraries = flags(query(*syntax, '--libs'))
    check(not any(str(prefix) in flag for flag in cflags + libraries), 'pkg-config retained original prefix')
    runtime = sdk / relative(sdk, 'runtime')
    for language, compiler in compilers.items():
        executable = root / ('consumer-' + language + ('.exe' if os.name == 'nt' else ''))
        if msvc:
            command = [compiler, '/nologo', '/TP' if language == 'cxx' else '/TC', *cflags,
                       source, '/Fe' + str(executable), '/Fo' + str(executable.with_suffix('.obj')),
                       '/link', *libraries]
        else:
            command = [compiler, '-x', 'c++' if language == 'cxx' else 'c', *cflags,
                       source, '-o', executable, *libraries]
            if os.name != 'nt':
                command.append('-Wl,-rpath,' + str(sdk / 'lib'))
        execute(command)
        expected_runtime = runtime
        if os.name == 'nt':
            for library in (sdk / 'bin').glob('*.dll'):
                shutil.copy2(library, root / library.name)
            expected_runtime = root / runtime.name
        result = execute([executable, metadata['build-id']], text=True)
        lines = result.stdout.splitlines()
        check(not result.stderr and len(lines) == 2 and lines[1] == 'Runtime SDK only',
              f'pkg-config consumer failed: {result.stdout!r} {result.stderr!r}')
        check(Path(lines[0]).resolve() == expected_runtime.resolve(), 'pkg-config linked another runtime')
        if inspector:
            result = execute([inspector, '/imports' if msvc else '-p', executable], text=True)
            check(runtime.name.lower() in result.stdout.lower(), 'pkg-config consumer lacks shared runtime import')
