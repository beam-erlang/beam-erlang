"""Qualify native Windows SDK consumers through a disposable installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import sys

from support.installation import add_installation_arguments, resolve_installation, stage_installation
from support.process import check, isolated_environment, run
from installed.sdk.consumer.pkgconfig import check_pkgconfig
from installed.sdk.consumer.documentation import check_documentation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'ninja', 'compiler', 'linker', 'librarian', 'rc', 'inspector',
                 'source-dir', 'test-dir', 'pkg-config', 'cxx-compiler', 'runtime-source'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--link-library-name', required=True)
    add_installation_arguments(parser)
    parser.add_argument('--inspector-kind', choices=('dumpbin', 'objdump'), default='dumpbin')
    args = resolve_installation(parser, parser.parse_args())
    check(os.name == 'nt', 'native Windows SDK qualification requires Windows')
    tools = {name: getattr(args, name.replace('-', '_')).resolve(strict=True) for name in
             ('cmake', 'ninja', 'compiler', 'cxx-compiler', 'linker', 'librarian', 'rc', 'inspector', 'pkg-config')}
    root = args.test_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    results = []
    deployments = []
    helpers = ([tools['compiler'].with_name('vctip.exe').resolve(strict=True)]
               if tools['compiler'].name.lower() == 'cl.exe' else [])
    inspect_option = '/imports' if args.inspector_kind == 'dumpbin' else '-p'
    # TemporaryDirectory removes only this runner's owned outputs after every
    # Job Object has been closed, including exception and interruption paths.
    with tempfile.TemporaryDirectory(prefix='native-', dir=root) as temporary:
        work = Path(temporary)
        source = work / 'independent sources 日本'
        shutil.copytree(args.source_dir, source, ignore=shutil.ignore_patterns('__pycache__'))
        prefix = work / 'installed'
        stage_installation(args, prefix)
        environment = isolated_environment(work)
        build_environment = environment.copy()
        build_environment['PATH'] = os.pathsep.join(dict.fromkeys(
            [str(p.parent) for p in tools.values()] + [environment['PATH']]))
        for phase in ('fresh', 'relocated'):
            if phase == 'relocated':
                destination = work / 'installed prefix 日本'
                prefix.rename(destination)
                prefix = destination
            check_documentation(prefix)
            check_pkgconfig(pkg_config=tools['pkg-config'],
                            compilers={'c': tools['compiler'], 'cxx': tools['cxx-compiler']},
                            prefix=prefix, source=args.runtime_source,
                            root=work / ('pkgconfig-' + phase), environment=build_environment,
                            msvc=args.inspector_kind == 'dumpbin', link_library=args.link_library_name,
                            inspector=tools['inspector'], owned_helpers=helpers)
            build = work / ('consumer-' + phase)
            configure = [tools['cmake'], '-S', source, '-B', build, '-G', 'Ninja',
                         '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',
                         f'-DCMAKE_PREFIX_PATH={prefix.as_posix()}']
            for variable, tool in (('CMAKE_MAKE_PROGRAM', 'ninja'), ('CMAKE_C_COMPILER', 'compiler'),
                                   ('CMAKE_LINKER', 'linker'), ('CMAKE_AR', 'librarian'),
                                   ('CMAKE_RC_COMPILER', 'rc')):
                configure.append(f'-D{variable}={tools[tool].as_posix()}')
            for step, command in (('configure', configure), ('build', [tools['cmake'], '--build', build])):
                result = run(command, cwd=work, env=build_environment, timeout=180, text=True, status=None,
                             owned_helpers=helpers)
                (root / f'{phase}-{step}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                (root / f'{phase}-{step}-helpers.json').write_text(
                    json.dumps(result.terminated_helpers) + '\n', encoding='utf-8')
                check(result.returncode == 0, f'{phase} {step} failed: {result.stdout} {result.stderr}')
            execution = environment.copy()
            execution['PATH'] = str(prefix / 'bin') + os.pathsep + execution['PATH']
            inputs = (build / 'inputs.txt').read_text(encoding='utf-8').splitlines()
            runtime_name = inputs.pop().lower()
            bundle = work / f'deployment {phase} 日本'
            run([tools['cmake'], '--install', build, '--prefix', bundle], cwd=work,
                env=build_environment, timeout=30)
            bundle_environment = environment.copy()
            bundle_environment['PATH'] = str(bundle/'bin') + os.pathsep + environment['PATH']
            deployed = run([bundle/'bin/deployment_probe.exe'], cwd=work, env=bundle_environment,
                           timeout=30, text=True)
            output = deployed.stdout.splitlines()
            check(not deployed.stderr and len(output) == 2 and output[0] == 'WINDOWS_DEPLOYMENT_OK',
                  f'deployment failed: {deployed.stdout!r} {deployed.stderr!r}')
            check(Path(output[1]).resolve() == (bundle/'bin'/runtime_name).resolve(),
                  f'deployment loaded another runtime: {output[1]}')
            deployments.append(dict(phase=phase, outcome='passed',
                                    files=sorted(p.relative_to(bundle).as_posix()
                                                 for p in bundle.rglob('*') if p.is_file())))
            for mode in ('shared', 'static', 'builtin'):
                executable = build / f'consumer_{mode}.exe'
                if mode != 'builtin':
                    invalid = work / 'invalid module.dll'
                    invalid.write_bytes(b'not a PE image\n')
                    rejected = run([executable, inputs[0], invalid, sys.executable, prefix], cwd=work, env=execution,
                                   timeout=10, status=1)
                    check(not rejected.stdout and b'Windows loader error' in rejected.stderr,
                          f'invalid DLL was not rejected: {rejected.stdout!r} {rejected.stderr!r}')
                result = run([executable, *inputs, sys.executable, prefix], cwd=work, env=execution, timeout=30)
                check(result.stdout == b'WINDOWS_NATIVE_SDK_RESTORED\r\n' and not result.stderr,
                      f'unexpected native consumer output: {result.stdout!r} {result.stderr!r}')
                imports = run([tools['inspector'], inspect_option, executable], cwd=work,
                              env=build_environment, timeout=30, text=True).stdout
                check((runtime_name in imports.lower()) == (mode == 'shared'),
                      f'{mode}: incorrect runtime import inventory')
                (root / f'{phase}-{mode}-imports.txt').write_text(imports, encoding='utf-8')
                results.append(dict(phase=phase, mode=mode, outcome='passed',
                                    sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                                    healthy_vm_cycles=3, rejected_vm_initializations=3,
                                    observed_live_descendants_stopped=3))
            native = Path(inputs[1])
            imports = run([tools['inspector'], inspect_option, native], cwd=work,
                          env=build_environment, timeout=30, text=True).stdout
            check(runtime_name not in imports.lower(), 'callback NIF imported a second runtime')
            (root / f'{phase}-nif-imports.txt').write_text(imports, encoding='utf-8')
    (root / 'results.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    (root / 'deployment-results.json').write_text(json.dumps(deployments, indent=2) + '\n', encoding='utf-8')
    print('Native Windows SDK: six installed consumer configurations passed')


if __name__ == '__main__':
    main()
