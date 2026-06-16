"""Installed package commands, native build, relocation and minimal help hosting."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil

from support.files import hidden
from support.process import check, isolated_environment, run


def snapshot(root):
    result = {}
    for path in root.rglob('*'):
        stat = path.lstat()
        content = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        result[str(path.relative_to(root))] = (stat.st_mode, stat.st_mtime_ns, content)
    return result


def cases(prefix, work):
    env = isolated_environment(work)
    env['PATH'] = str(prefix / 'bin')  # Discover the alias without external commands.
    pkg = prefix / 'bin/beam-pkg'
    beam = prefix / 'bin/beam'
    check(pkg.is_symlink() and os.readlink(pkg) == 'beam', 'alias is not relative')
    identity = run([beam, 'version'], cwd=work, env=env, timeout=20, text=True).stdout.strip()
    before = snapshot(work)

    def invoke(args, status, needle, executable='beam-pkg'):
        result = run([executable, *args], cwd=work, env=env, timeout=20,
                     status=status, text=True)
        output = result.stderr if status else result.stdout
        check(needle in output, f'missing diagnostic: {args}: {result}')
        check(not (result.stdout if status else result.stderr), f'wrong output stream: {args}')

    for args in ([], ['help'], *(['help', name] for name in ['help', 'version', 'build', 'install', 'tree'])):
        invoke(args, 0, 'usage:')
    invoke(['version'], 0, identity)
    invoke(['version'], 0, 'experimental local packages')
    for operation in ['tree']:
        for args in ([operation], [operation, '.'], [operation, 'missing project 日本語'],
                     [operation, './-literal'], [operation, str(work / 'project λ')]):
            invoke(args, 2, 'not implemented')
        for args in ([operation, ''], [operation, '--bad'], [operation, 'one', 'two'],
                     [operation, '--runtime=otp']):
            invoke(args, 1, 'invalid arguments')
    for args in (['unknown'], ['help', 'unknown'], ['version', 'extra'],
                 ['help', 'build', 'extra'], ['--runtime=beam', 'help']):
        invoke(args, 1, 'invalid arguments')
    for selector in ([],):
        for tail in (['--project'], ['--project', ''], ['--project', '--'],
                     ['--project', '.', 'extra'], ['--project', '--runtime=otp']):
            invoke([*selector, 'run', *tail], 1, 'usage:', str(beam))
    check(snapshot(work) == before, 'package scaffold changed project or home files')


def build_cases(prefix, root, cmake):
    """Independent installed fixture: package policy never comes from the playground."""
    work = root / 'build input λ'
    work.mkdir()
    env = isolated_environment(work)
    env['PATH'] = str(Path(cmake).parent) + os.pathsep + os.environ.get('PATH', '')
    manifest = work / 'beam.toml'
    manifest.write_text('format = 1\n[package]\nname = "acceptance/native"\nversion = "1"\n'
                        '[build]\nsources = ["pkg_acceptance.erl"]\n'
                        '[build.native]\ncmake = "."\noutputs = ["priv/acceptance.so"]\n')
    source = work / 'pkg_acceptance.erl'
    source.write_text('-module(pkg_acceptance).\n-export([load/1, value/0]).\n'
                      'load(P)->erlang:load_nif(P,0).\nvalue()->erlang:nif_error(unloaded).\n')
    (work / 'native.c').write_text('#include <erl_nif.h>\n'
        'static ERL_NIF_TERM value(ErlNifEnv* e,int n,const ERL_NIF_TERM a[]){return enif_make_int(e,19);}\n'
        'static ErlNifFunc f[]={{"value",0,value,0}};\nERL_NIF_INIT(pkg_acceptance,f,NULL,NULL,NULL,NULL)\n')
    (work / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\n'
        'project(acceptance LANGUAGES C)\nfind_package(BEAM CONFIG REQUIRED)\n'
        'add_library(acceptance MODULE native.c)\nbeam_target_nif(acceptance)\n'
        'set_target_properties(acceptance PROPERTIES PREFIX "")\n'
        'install(TARGETS acceptance LIBRARY DESTINATION priv)\n')
    pkg = prefix / 'bin/beam-pkg'
    # Building needs no installed OTP applications.
    check(not (prefix / 'lib/beam/apps').exists() and not (prefix / 'lib/beam/runtime/otp').exists(),
          'OTP product assets remain installed')
    result = run([pkg, 'build', work], cwd=root, env=env, timeout=120, text=True)
    check('Built ' in result.stdout, 'build did not report an artifact')
    import io
    import re
    import tarfile
    digest = re.search(r'^"root" = "([a-f0-9]{64})"$', (work / 'beam.lock').read_text(), re.M).group(1)
    artifact = work / 'build/beam-pkg/artifacts' / (digest + '.bpkg.tar.gz')
    check(hashlib.sha256(artifact.read_bytes()).hexdigest() == digest, 'wrong artifact digest')
    extracted = root / 'native artifact λ'
    with tarfile.open(fileobj=io.BytesIO(artifact.read_bytes()), mode='r:gz') as tar:
        for entry in tar:
            check(entry.isfile() and not entry.name.startswith('/') and '..' not in Path(entry.name).parts, 'unsafe package output')
            target = extracted / entry.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(tar.extractfile(entry).read())
    consumer = root / 'native-consumer'
    run([cmake, '-S', Path(__file__).parent / 'native_consumer', '-B', consumer,
         f'-DCMAKE_PREFIX_PATH={prefix}'], cwd=root, env=env, timeout=30)
    run([cmake, '--build', consumer], cwd=root, env=env, timeout=30)
    result = run([consumer / 'package_native_consumer', extracted / 'ebin/pkg_acceptance.beam',
                  extracted / 'priv/acceptance.so'], cwd=root, env=env, timeout=30, text=True)
    check(result.stdout == 'PKG_NATIVE_OK\n', 'native package did not execute')
    saved = (work / 'beam.lock').read_bytes()
    source.write_text('invalid source\n')
    run([pkg, 'build', work], cwd=root, env=env, timeout=30, status=1)
    check((work / 'beam.lock').read_bytes() == saved, 'failed build replaced selected lock')
    check(not (work / '.beam').exists(), 'build created an installed environment')
    for tail in ([''], ['--bad'], ['one', 'two']):
        result = run([pkg, 'build', *tail], cwd=root, env=env, timeout=20, status=1, text=True)
        check('invalid arguments' in result.stderr, 'malformed build was accepted')


def project_shell_cases(prefix, root, env):
    def invoke(*args, **kw):
        kw.setdefault("timeout", 60)
        return run(*args, **kw)
    work = root / 'library shell 日本語'
    work.mkdir()
    beam = prefix / 'bin/beam'
    pkg = prefix / 'bin/beam-pkg'
    manifest = work / 'beam.toml'
    base = 'format = 1\n[package]\nname = "acceptance/library"\nversion = "1"\n'
    manifest.write_text(base + '[build]\nsources = ["library.erl"]\n'
                        '[scripts]\nversion = ["beam", "version"]\n')
    (work / 'library.erl').write_text('-module(library).\n-export([value/0]).\nvalue()->42.\n')
    invoke([beam, 'build', work], cwd=root, env=env, timeout=60)
    invoke([pkg, 'install', work], cwd=root, env=env, timeout=60)
    assert invoke([beam, 'task', '--project', work], cwd=root, env=env, text=True).stdout == 'version\n'
    assert invoke([beam, 'task', '--project', work, 'version'], cwd=root, env=env, text=True).stdout == invoke([beam, 'version'], cwd=root, env=env, text=True).stdout
    shell = [beam, 'shell', '--project', work]
    result = invoke(shell, cwd=root, env=env, input_data=b'library:value().\nq().\n', text=True)
    assert '42' in result.stdout and not result.stderr
    invoke([beam, 'run', '--project', work], cwd=root, env=env, status=1)
    invoke([beam, '--runtime=otp', 'shell', '--project', work], cwd=root, env=env, status=1)
    invoke(shell + ['--'], cwd=root, env=env, status=1)
    current = work / '.beam/current.toml'
    saved = current.read_bytes()
    current.write_text('format = 1\ngeneration = "../escape"\n')
    invoke(shell, cwd=root, env=env, status=1)
    current.write_bytes(saved)
    # An interactive helper is an actual provider, even though run does not load it.
    manifest.write_text(base + '[build]\nsources = ["sets.erl"]\n')
    (work / 'sets.erl').write_text('-module(sets).\n-export([value/0]).\nvalue()->42.\n')
    invoke([beam, 'build', work], cwd=root, env=env, timeout=60)
    invoke([pkg, 'install', work], cwd=root, env=env, timeout=60)
    result = invoke(shell, cwd=root, env=env, status=1, text=True)
    assert 'conflicts with host module: sets' in result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--test-dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.test_dir.resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    env = isolated_environment(root)
    stage = root / 'stage'
    run([args.cmake, '--install', args.build_dir, '--prefix', stage],
        cwd=root, env=env, timeout=120)
    work = root / 'unrelated work 日本語'
    project = work / 'project λ'
    project.mkdir(parents=True)
    # Opening the manifest would block: the command must not even read it.
    os.mkfifo(project / 'beam.toml')
    (project / 'beam.lock').write_text('untouched lock sentinel\n')
    cases(stage, work)
    build_cases(stage, root, args.cmake)
    project_shell_cases(stage, root, env)
    relocated = root / 'relocated prefix λ'
    stage.rename(relocated)
    # Keep exactly the runtime and ordinary support inventories plus package policy. No compiler,
    # OTP apps, private CLI helpers, file/network services or native programs.
    for path in (relocated / 'lib').iterdir():
        if path.name not in {'libbeam.so', 'beam'}:
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
    check(not (relocated / 'lib/beam/apps').exists(), 'OTP applications remain installed')
    for name in ['runtime', 'ebin', 'tools/compiler']:
        shutil.rmtree(relocated / 'lib/beam' / name)
    shutil.rmtree(relocated / 'libexec')
    cases(relocated, work)
    module = relocated / 'lib/beam/tools/pkg/ebin/beam_pkg.beam'
    with hidden(module):
        result = run([relocated / 'bin/beam-pkg', 'help'], cwd=work, env=env,
                     timeout=20, status=1, text=True)
        check('beam_pkg' in result.stderr and not result.stdout, 'missing policy was ignored')
    with hidden(relocated / 'lib/beam/boot'):
        run([relocated / 'bin/beam', 'run', '--project', 'missing'],
            cwd=work, env=env, timeout=20, status=1)
    print('installed package commands: alias, syntax, side effects, relocation and minimal assets passed')


if __name__ == '__main__':
    main()
