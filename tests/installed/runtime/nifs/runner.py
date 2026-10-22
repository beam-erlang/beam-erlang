"""Installed external NIF admission, project launch and repeated teardown."""
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
from support.process import run, isolated_environment, child_process
from support.terminal import TerminalShell
from installed.cli.scenarios.terminal import check_beam_shell_history


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', required=True)
    for key in ('test-dir', 'source-dir'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument("--linkage", choices=("shared", "static"), default="shared")
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    root = args.test_dir.resolve()
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    source = args.source_dir.resolve()
    env = isolated_environment(root)
    def execute(argv, **kw):
        return run([str(a) for a in argv], cwd=root, env=env, timeout=180, text=True, **kw)
    prefix = root / 'SDK prefix λ'
    stage_installation(args, prefix)
    build = root / 'native-build'
    execute([args.cmake, '-S', source, '-B', build, f'-DCMAKE_PREFIX_PATH={prefix}',
             f'-DRUNTIME_COMPONENT={"RuntimeStatic" if args.linkage == "static" else "Runtime"}'])
    execute([args.cmake, '--build', build, '-j', '4'])
    execute([prefix / 'bin/beam', 'compile', '-o', build, '--', source / 'probe.erl', source / 'probe2.erl'])
    stage = root / 'relocated native λ'
    (stage / 'bin').mkdir(parents=True)
    (stage / 'lib').mkdir()
    shutil.copy2(build / 'native_acceptance', stage / 'bin/host')
    shutil.copy2(build / 'builtin_acceptance', stage / 'bin/builtins')
    if args.linkage == 'shared':
        shutil.copy2(prefix / 'lib/libbeam.so', stage / 'lib/libbeam.so')
    else:
        for mode in ('second_runtime', 'transitive_runtime'):
            log = root / f'{mode}.events'
            env['NIF_ACCEPTANCE_LOG'] = str(log)
            execute([stage / 'bin/host', build / 'probe.beam', build / f'{mode}.so', 'foreign-runtime'])
            execute([stage / 'bin/host', build / 'probe.beam', build / mode, 'ordinary-foreign-runtime'])
            assert not log.exists(), 'Foreign native initializer ran'
        shutil.rmtree(prefix)
    builtin_log = root / 'builtins.events'
    env['NIF_ACCEPTANCE_LOG'] = str(builtin_log)
    result = execute([stage / 'bin/builtins', build / 'probe.beam'])
    assert result.stdout == 'BUILTIN_LIFETIMES_OK\n' and not result.stderr, result
    assert builtin_log.read_text() == 'LDULDU'
    for mode in ('probe', 'fail_load', 'bad_abi', 'bad_module', 'bad_function', 'unstamped', 'wrongstamp'):
        log = root / f'{mode}.events'
        env['NIF_ACCEPTANCE_LOG'] = str(log)
        expected = 'success' if mode == 'probe' else ('prepare-failure' if mode in ('unstamped', 'wrongstamp') else 'activation-failure')
        execute([stage / 'bin/host', build / 'probe.beam', build / f'{mode}.so', expected])
        assert (log.read_text() if log.exists() else '') == ('LDULDU' if mode == 'probe' else '')
    execute([stage / 'bin/host', build / 'probe.beam', build / 'missing.so', 'prepare-failure'])
    log = root / 'partial.events'
    env['NIF_ACCEPTANCE_LOG'] = str(log)
    execute([stage / 'bin/host', build / 'probe.beam', build / 'probe.so', 'partial',
             build / 'probe2.beam', build / 'bad_module.so'])
    assert log.read_text() == 'LULU'
    # Unrecoverable native failures must preserve their distinct dump/exit modes.
    for mode, expected_status, has_dump in ((0, -signal.SIGABRT, True),
                                            (1, -signal.SIGABRT, False),
                                            (2, 1, True)):
        dump = root / f'native-failure-{mode}.dump'
        fatal_env = dict(env, ERL_CRASH_DUMP=str(dump),
                         ERL_CRASH_DUMP_SECONDS='5', ERL_CRASH_DUMP_BYTES='1048576')
        result = run([stage / 'bin/host', build / 'probe.beam', build / 'probe.so',
                      f'fatal-{mode}'], cwd=root, env=fatal_env, timeout=30,
                     text=True, status=expected_status)
        marker = f'public-native-failure mode={mode} term=native_failure_marker'
        assert marker in result.stderr, result
        assert dump.exists() == has_dump, (mode, result)
        if has_dump:
            assert marker in dump.read_text(errors='replace')
    if args.linkage == 'static':
        print('Static native admission and repeated teardown passed without the SDK.')
        return
    project = root / 'project'
    project.mkdir()
    for name in ('probe.c', 'probe.erl', 'packet_probe.h'):
        shutil.copy2(source / name, project / name)
    (project / 'beam.toml').write_text('''format = 1
[package]
name = "acceptance/native-project"
version = "1"
[build]
sources = ["probe.erl"]
[build.native]
cmake = "."
outputs = ["priv/probe.so"]
nifs = { probe = "priv/probe.so" }
[host]
modules = ["erlang", "io", "persistent_term"]
[entry]
module = "probe"
''')
    (project / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(native_project LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
add_library(probe MODULE probe.c)
beam_target_nif(probe)
set_target_properties(probe PROPERTIES PREFIX "")
install(TARGETS probe LIBRARY DESTINATION priv)
''')
    execute([prefix / 'bin/beam', 'build', project])
    execute([prefix / 'bin/beam-pkg', 'install', project])
    deployed = root / 'deployed project λ'
    shutil.copytree(project / '.beam', deployed / '.beam')
    shutil.rmtree(project)
    for relative in ('lib/beam/tools/compiler', 'libexec'):
        shutil.rmtree(prefix / relative)
    moved = root / 'moved SDK 日本語'
    prefix.rename(moved)
    prefix = moved
    env['PATH'] = ''
    log = root / 'project.events'
    env['NIF_ACCEPTANCE_LOG'] = str(log)
    result = execute([prefix / 'bin/beam', 'run', '--project', deployed, '--', 'literal value'])
    assert result.stdout == 'NATIVE_PROJECT ["literal value"]\n' and not result.stderr, result
    assert log.read_text() == 'LDU'
    before = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in deployed.rglob('*') if p.is_file()}
    execute([prefix / 'bin/beam', 'run', '--project', deployed, '--', 'halt'], status=7)
    failure = execute([prefix / 'bin/beam', 'run', '--project', deployed, '--', 'exception'], status=1)
    assert 'native_project_exception' in failure.stderr
    line = execute([prefix / 'bin/beam', 'run', '--project', deployed, '--', 'input'], input_data='hello λ\n'.encode())
    assert line.stdout == 'prompt> hello λ\n'
    shell_command = [str(prefix / 'bin/beam'), 'shell', '--project', str(deployed)]
    for ending, status in [('q().\n', 0), ('', 0), ('erlang:halt(9).\n', 9)]:
        log.write_text('')
        result = execute(shell_command, input_data=(
            'false=erlang:module_loaded(beam_pkg), false=erlang:module_loaded(beam_pkg_task), '
            'false=erlang:module_loaded(beam_pkg_environment), false=erlang:module_loaded(compile), '
            'false=erlang:module_loaded(beam_file), false=erlang:module_loaded(beam_socket), '
            'false=erlang:module_loaded(application), probe:retain(), shell_native_ok.\n' + ending).encode(), status=status)
        assert 'shell_native_ok' in result.stdout and not result.stderr, result
        assert log.read_text() == 'LDU'
    log.write_text('')
    terminal = TerminalShell(shell_command, root, env)
    try:
        terminal.expect(r"(?:^|\n)1> ")
        terminal.send_and_expect('probe:retain(), X = 40 + 2.\n', r'\n42\n2> ')
        terminal.send_and_expect('X + 2\x7f1.\n', r'\n43\n3> ')
        terminal.send_and_expect('erlang:error(shell_failure).\n', r'exception error: shell_failure[\s\S]*\n4> ')
        terminal.send_and_expect('length("λ").\n', r'\n1\n5> ')
        start = len(terminal.output)
        terminal.send_and_expect('spawn(fun()->receive after 20 -> io:format("ASYNC~n") end end), ok.\n', r'\nok\n\d+> ')
        terminal.expect(r'ASYNC\n', start)
        check_beam_shell_history(terminal)
        terminal.send_and_expect('self()!kept, self()!chosen, receive chosen -> yes end, receive kept -> yes end.\n', r'\nyes\n\d+> ')
        os.write(terminal.fd, b'q().\n')
        terminal.wait_exit(0)
    finally:
        terminal.close()
    assert log.read_text() == 'LDU'
    after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in deployed.rglob('*') if p.is_file()}
    assert before == after
    for filename in ('probe.beam', 'probe.so'):
        target, = deployed.rglob(filename)
        data = target.read_bytes()
        target.unlink()
        execute([prefix / 'bin/beam', 'run', '--project', deployed], status=1)
        execute(shell_command, status=1)
        target.write_bytes(data)
    # Broken output must unwind through VM termination rather than SIGPIPE.
    log.write_text('')
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        with child_process(shell_command, cwd=root, env=env, stdin=subprocess.DEVNULL,
                           stdout=write_fd, stderr=subprocess.PIPE) as child:
            _, diagnostic = child.communicate(timeout=15)
            assert child.returncode == 1, (child.returncode, diagnostic)
    finally:
        os.close(write_fd)
    assert log.read_text() == 'LU'
    library, = deployed.rglob('probe.so')
    saved = library.read_bytes()
    try:
        for mode in ('bad_abi', 'bad_module', 'bad_function', 'fail_load'):
            library.write_bytes((build / (mode + '.so')).read_bytes())
            rejected = execute(shell_command, status=1)
            assert '1> ' not in rejected.stdout and rejected.stderr
    finally:
        library.write_bytes(saved)
    print('Independent native/project acceptance passed.')


if __name__ == '__main__':
    main()
