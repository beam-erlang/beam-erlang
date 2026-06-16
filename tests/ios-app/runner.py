#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Installed-only real iOS application acceptance; no playground dependency."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import tomllib

BUNDLE = 'org.erlangsters.acceptance.ios'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host-sdk', type=Path, required=True)
    p.add_argument('--target-sdk', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True, help='Parent of disposable validation directories')
    p.add_argument('--result', type=Path, required=True, help='Retained compact JSON evidence')
    destination = p.add_mutually_exclusive_group(required=True)
    destination.add_argument('--simulator', help='Explicit simulator UDID; booted by the runner')
    destination.add_argument('--device', help='Explicit connected device identifier')
    destination.add_argument('--build-only', action='store_true')
    p.add_argument('--signing-team')
    p.add_argument('--unsigned', action='store_true', help='Only permitted with --build-only')
    a = p.parse_args()
    if not os.environ.get('DEVELOPER_DIR'): p.error('Set DEVELOPER_DIR explicitly')
    if a.unsigned and not a.build_only: p.error('Execution requires signed applications')
    source = Path(__file__).resolve().parent
    host = a.host_sdk.resolve(strict=True)
    target = a.target_sdk.resolve(strict=True)
    facts = tomllib.loads((target/'lib/beam/target.toml').read_text())
    if facts['system'] != 'iOS': p.error('An iOS SDK is required')
    variant = facts['apple-variant']
    if a.simulator and variant != 'simulator': p.error('Simulator SDK required')
    if a.device and variant != 'device': p.error('Device SDK required')
    a.work.mkdir(parents=True, exist_ok=True)
    a.result.parent.mkdir(parents=True, exist_ok=True)
    evidence = {'status': 'running', 'target': facts, 'deployment_minimum': '15.0',
                'destination': a.simulator or a.device, 'debugger_attached': False,
                'configuration': 'Release', 'commands': [], 'variants': {},
                'source_sha256': {str(x.relative_to(source)): hashlib.sha256(x.read_bytes()).hexdigest()
                                  for x in sorted(source.rglob('*')) if x.is_file() and 'build' not in x.parts}}
    log = a.result.with_suffix('.log').open('w')
    installed = False
    booted_here = False

    def run(*command, check=True, isolated=False, timeout=600):
        command = list(map(str, command))
        if isolated:
            # Deny original inputs during consumer configuration and compilation.
            profile = '(version 1)(allow default)' + ''.join(
                '(deny file-read* (subpath '+json.dumps(str(x))+'))' for x in (source, target, original_export))
            command = ['sandbox-exec', '-p', profile, *command]
        evidence['commands'].append(command)
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, start_new_session=True)
        try:
            output, _ = process.communicate(timeout=timeout)
        except BaseException:
            os.killpg(process.pid, signal.SIGKILL)
            output, _ = process.communicate()
            log.write('$ '+repr(command)+'\n'+output+'\n'); log.flush()
            raise
        log.write('$ '+repr(command)+'\n'+output+'\n'); log.flush()
        if check and process.returncode: raise RuntimeError(f'command failed ({process.returncode}): {command}\n{output[-6000:]}')
        return output

    try:
        with tempfile.TemporaryDirectory(prefix='ios-app-', dir=a.work) as temporary:
            work = Path(temporary)
            project = work/'fixture'
            shutil.copytree(source, project, ignore=shutil.ignore_patterns('build', '__pycache__'))
            sdk = work/'relocated-sdk'
            shutil.copytree(target, sdk, symlinks=True)
            package = project/'package'
            original_export = work/'original-export'
            run(host/'bin/beam-pkg', 'build', '--target-sdk', target, package)
            run(host/'bin/beam-pkg', 'install', '--target-sdk', target, package)
            run(host/'bin/beam-pkg', 'export', '--target-sdk', target, '--output', original_export, package)
            export = work/'relocated-export'
            shutil.copytree(original_export, export, symlinks=True)
            if a.simulator:
                devices = json.loads(run('xcrun','simctl','list','devices','--json'))['devices']
                entries = [(runtime, d) for runtime, ds in devices.items() for d in ds if d['udid'] == a.simulator]
                if len(entries) != 1: raise RuntimeError('Explicit simulator not found')
                runtime, device = entries[0]
                evidence['simulator_runtime'] = runtime
                if device['state'] != 'Booted':
                    run('xcrun','simctl','boot',a.simulator); booted_here = True
                run('xcrun','simctl','bootstatus',a.simulator,'-b')
            for resource_variant in ('normal','missing','corrupt'):
                build = work/resource_variant
                args = ['cmake','-S',project,'-B',build,'-G','Xcode',
                        '-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'lib/cmake/BEAM/BEAMToolchain.cmake'),
                        '-DBEAM_PACKAGE_EXPORT='+str(export), '-DRESOURCE_VARIANT='+resource_variant]
                if a.signing_team: args += ['-DBEAM_SIGNING_TEAM='+a.signing_team]
                run(*args, isolated=True)
                settings = ['CODE_SIGNING_ALLOWED=NO'] if a.unsigned else []
                if a.signing_team and not a.unsigned: settings += ['-allowProvisioningUpdates']
                run('cmake','--build',build,'--config','Release','--',*settings, isolated=True)
                app = build/('Release-iphoneos' if variant == 'device' else 'Release-iphonesimulator')/'BEAMAcceptance.app'
                forbidden = [str(x.relative_to(app)) for x in app.rglob('*') if
                    (x.is_file() and (x.name in ('beam','beam-launch','beam-pkg','beam-compile','CMakeLists.txt','CMakeCache.txt','package.toml','sdk.toml','target.toml') or
                    x.suffix in ('.a','.h','.cmake','.bpk','.bpkg','.tar','.gz','.zip','.erl'))) or 'Headers' in x.parts or 'compiler' in x.parts]
                if forbidden: raise RuntimeError('Development inputs leaked into app: '+repr(forbidden))
                if not a.unsigned: run('codesign','--verify','--deep','--strict',app)
                item = {'build': 'passed', 'signing': 'unsigned' if a.unsigned else 'verified', 'execution': 'untested'}
                evidence['variants'][resource_variant] = item
                if a.build_only: continue
                if a.simulator:
                    run('xcrun','simctl','uninstall',a.simulator,BUNDLE,check=False)
                    run('xcrun','simctl','install',a.simulator,app); installed = True
                    container = Path(run('xcrun','simctl','get_app_container',a.simulator,BUNDLE,'data').strip())
                    run('xcrun','simctl','launch',a.simulator,BUNDLE)
                else:
                    run('xcrun','devicectl','device','uninstall','app','--device',a.device,BUNDLE,check=False)
                    run('xcrun','devicectl','device','install','app','--device',a.device,app); installed = True
                    run('xcrun','devicectl','device','process','launch','--device',a.device,BUNDLE)

                def poll(predicate):
                    end = time.monotonic()+90
                    last = None
                    while time.monotonic() < end:
                        if a.simulator: report = container/'Documents/acceptance.json'
                        else:
                            report = work/'device-report.json'
                            report.unlink(missing_ok=True)
                            run('xcrun','devicectl','device','copy','from','--device',a.device,
                                '--domain-type','appDataContainer','--domain-identifier',BUNDLE,
                                '--source','Documents/acceptance.json','--destination',report,check=False,timeout=20)
                        try: last = json.loads(report.read_text())
                        except (FileNotFoundError,json.JSONDecodeError): pass
                        if last and predicate(last): return last
                        if last and last.get('status') == 'failed' and resource_variant == 'normal':
                            raise RuntimeError('Application failure: '+repr(last))
                        time.sleep(0.5)
                    raise RuntimeError('Application result timeout: '+repr(last))

                if resource_variant != 'normal':
                    item['report'] = poll(lambda r: r.get('status') == 'failed')
                    if item['report'].get('bytecode_validation') != 'failed' or item['report'].get('generation',0) != 0:
                        raise RuntimeError('Resource variant did not fail before VM execution: '+repr(item['report']))
                    item['execution'] = 'passed-expected-resource-rejection'
                    continue
                first = poll(lambda r: r.get('status') == 'passed' and r.get('cycles',0) >= 3 and r.get('events',0) >= 3 and r.get('ui_heartbeats',0) >= 3)
                item['initial_report'] = first
                if a.simulator: run('xcrun','simctl','launch',a.simulator,'com.apple.Preferences')
                else: run('xcrun','devicectl','device','process','launch','--device',a.device,'com.apple.Preferences')
                # Suspension may interrupt teardown; foreground must serialize behind it.
                time.sleep(2)
                if a.simulator: run('xcrun','simctl','launch',a.simulator,BUNDLE)
                else: run('xcrun','devicectl','device','process','launch','--device',a.device,BUNDLE)
                item['recovery_report'] = poll(lambda r: r.get('status') == 'passed' and r.get('generation',0) > first['generation'] and r.get('state') == 'ready' and r.get('backgrounds',0) >= 1)
                item['execution'] = 'passed'
            evidence['status'] = 'build-only' if a.build_only else 'passed'
    except BaseException as error:
        evidence['status'] = 'failed'; evidence['error'] = str(error)
        if a.simulator:
            try:
                run('xcrun','simctl','spawn',a.simulator,'log','show','--style','compact',
                    '--last','2m','--predicate','process == "BEAMAcceptance"',check=False,timeout=20)
            except Exception as diagnostic_error:
                evidence['diagnostic_error'] = str(diagnostic_error)
        raise
    finally:
        cleanup_errors = []
        cleanup_commands = []
        if installed:
            cleanup_commands.append(['xcrun','simctl','uninstall',a.simulator,BUNDLE] if a.simulator else
                ['xcrun','devicectl','device','uninstall','app','--device',a.device,BUNDLE])
        if booted_here: cleanup_commands.append(['xcrun','simctl','shutdown',a.simulator])
        for command in cleanup_commands:
            try: run(*command, timeout=60)
            except Exception as cleanup_error: cleanup_errors.append(str(cleanup_error))
        evidence['cleanup'] = 'disposable inputs and applications removed' if not cleanup_errors else 'incomplete'
        if cleanup_errors:
            evidence['cleanup_errors'] = cleanup_errors
            evidence['status'] = 'failed'
        a.result.write_text(json.dumps(evidence,indent=2)+'\n'); log.close()
        if cleanup_errors: raise RuntimeError('Application cleanup failed: '+repr(cleanup_errors))


if __name__ == '__main__': main()
