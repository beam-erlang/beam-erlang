#!/usr/bin/env python3
"""Qualify OTP and shared VM suites against one verified acquired SDK."""
import argparse
from contextlib import contextmanager
import importlib.util
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import release

sys.path.insert(0, str(release.ROOT / 'tests'))
from support.process import child_process, kill_group
from support.scratch import scratch


def unexecuted(report, kind, candidate, reason, status='unexecuted'):
    names = release.VM_TESTS if kind == 'beam-vm' else release.OTP_TESTS
    release.write(report, {'schema': 1, 'kind': kind, 'candidate': candidate,
        'eligible': False, 'tests': [{'name': name, 'status': status, 'reason': reason}
                                   for name in sorted(names)]})


def run_command(command, work, label, timeout=21600):
    """Stream diagnostics and stop owned processes before scratch cleanup."""
    logs = work / 'logs'
    logs.mkdir(exist_ok=True)
    with (logs / (label + '.log')).open('w') as log:
        with child_process(command, cwd=release.ROOT, env=None,
                           stdout=log, stderr=subprocess.STDOUT) as process:
            try:
                status = process.wait(timeout=timeout)
            except BaseException:
                stop_group(process)
                raise
            if status:
                stop_group(process)
            return status


def stop_group(process):
    """Allow test runners to stop their own child sessions before forced cleanup."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        process.poll()
        try:
            os.killpg(process.pid, 0)
        except ProcessLookupError:
            return
        time.sleep(0.05)
    kill_group(process.pid)
    if process.poll() is None:
        process.wait()


@contextmanager
def short_work(work):
    """Keep filename-to-atom fixtures short without sharing writable scratch."""
    short = Path(tempfile.mkdtemp(prefix='otp-preview-', dir='/tmp'))
    try:
        with scratch(short):
            yield short
    finally:
        if os.environ.get('BEAM_TEST_KEEP_WORK') != '1':
            logs = work / 'logs'
            logs.mkdir(exist_ok=True)
            # scratch has removed payloads; keep only its bounded diagnostics.
            shutil.move(str(short), str(logs / 'short-path-tests'))


def stage_product(source, build, prefix, output, candidate):
    """Use the OTP-owned inventory format outside its unchanged installation."""
    spec = importlib.util.spec_from_file_location('otp_installation_inventory',
                                                  source / 'tools/installation_inventory.py')
    module = importlib.util.module_from_spec(spec)
    exec(compile(Path(spec.origin).read_text(), spec.origin, 'exec'), module.__dict__)
    selection = release.read(build / 'sdk-selection.json')
    for key, field in [('version', 'identity'), ('build_id', 'build-id'), ('flavor', 'flavor')]:
        if selection.get(key) != candidate['sdk'][field]:
            raise ValueError('OTP build selected a different SDK: ' + key)
    provenance = {'format': 1, 'runtime': 'candidate', 'flavor': candidate['flavor'],
        'upstream_commit': module.UPSTREAM_COMMIT, 'prefix': str(prefix),
        'build': {'linkage': 'static', 'sdk_selection': selection,
                  'otp_source_revision': candidate['inputs']['otp']['commit'],
                  'sdk_archive': candidate['archive']},
        'entries': module.inventory(prefix)}
    release.write(output, provenance)


def qualify_suites(args, candidate, source, sdk, work):
    """An OTP test failure must not prevent independent shared VM coverage."""
    build = work / 'build'
    vm_status = 1
    vm_started = False
    try:
        prefix = work / 'OTP candidate ü'
        provenance = work / 'otp-provenance.json'
        status = run_command(['cmake', '--install', str(build), '--prefix', str(prefix)],
                             work, 'otp-install', timeout=300)
        if status:
            raise RuntimeError('OTP installation failed')
        stage_product(source, build, prefix, provenance, candidate)
        vm_build = work / 'vm-ct'
        command = ['cmake', '-S', str(release.ROOT / 'tests/vm/common_test'),
            '-B', str(vm_build), '-G', 'Ninja', '-DBEAM_SDK_ROOT=' + str(sdk),
            '-DBEAM_VM_TEST_OTP_PREFIX=' + str(prefix),
            '-DBEAM_VM_TEST_OTP_PROVENANCE=' + str(provenance),
            '-DBEAM_VM_TEST_SDK_VERSION=' + candidate['sdk']['identity'],
            '-DBEAM_VM_TEST_SDK_BUILD_ID=' + candidate['sdk']['build-id'],
            '-DBEAM_VM_TEST_FLAVOR=' + candidate['flavor']]
        if run_command(command, work, 'vm-configure', timeout=300):
            raise RuntimeError('Shared VM test configuration failed')
        vm_started = True
        unexecuted(args.vm_report, 'beam-vm', release.digest(args.record),
                   'Shared VM execution has not produced a complete report', status='untested')
        vm_status = run_command([sys.executable, str(release.ROOT / 'release/release.py'),
            'ctest', '--kind', 'beam-vm', '--build', str(vm_build), '--record', args.record,
            '--report', args.vm_report], work, 'vm-ctest')
    except Exception as error:
        unexecuted(args.vm_report, 'beam-vm', release.digest(args.record), str(error),
                   status='untested' if vm_started else 'unexecuted')
    # Finish the bounded VM batch before the broad OTP suite can exhaust a job
    # deadline. A VM preparation or execution failure does not suppress OTP.
    unexecuted(args.report, 'otp', release.digest(args.record),
               'OTP execution has not produced a complete report', status='untested')
    try:
        otp_status = run_command([sys.executable, str(release.ROOT / 'release/release.py'),
            'ctest', '--kind', 'otp', '--build', str(build), '--record', args.record,
            '--report', args.report], work, 'otp-ctest')
    except Exception as error:
        otp_status = 1
        unexecuted(args.report, 'otp', release.digest(args.record), str(error), status='untested')
    return otp_status or vm_status


def qualify(args):
    candidate_digest = release.digest(args.record)
    for report, kind in [(args.report, 'otp'), (args.vm_report, 'beam-vm')]:
        unexecuted(report, kind, candidate_digest, 'Qualification prerequisites have not completed')
    candidate = release.read(args.record)
    release.validate_candidate_version(candidate)
    root = Path(args.inputs_dir).resolve()
    work_root = Path(args.work).resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='qualification-', dir=work_root))
    with scratch(work), short_work(work) as short:
        release.inputs(release.ROOT / 'release/inputs.json', qualification=True)
        release.verify_asset(Path(args.record).parent / candidate['archive']['name'], candidate['archive'])
        sdk = release.extract(Path(args.record).parent / candidate['archive']['name'], work / 'SDK ü')
        if release.clean_revision(root / 'otp') != candidate['inputs']['otp']['commit']:
            raise ValueError('OTP source pin mismatch')
        for key, item in candidate['inputs']['qualification_assets'].items():
            release.verify_asset(root / key, {'name': key, 'sha256': item['sha256'], 'size': item['size']})
        reference = release.extract(root / ('reference-' + candidate['flavor']), work / 'original')
        postgres = release.extract(root / 'postgres', work / 'postgres')
        ldap = release.extract(root / 'ldap', work / 'ldap')
        ftp = release.extract(root / 'ftp', work / 'ftp')
        command = ['cmake', '-S', str(root / 'otp'), '-B', str(work / 'build'), '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DBUILD_TESTING=ON',
            '-DBEAM_SDK_ROOT=' + str(sdk), '-DOTP_BEAM_FLAVOR=' + candidate['flavor'],
            '-DOTP_BEAM_SDK_VERSION=' + candidate['sdk']['identity'],
            '-DOTP_BEAM_SDK_BUILD_ID=' + candidate['sdk']['build-id'],
            '-DOTP_REFERENCE_PREFIX=' + str(reference),
            '-DOTP_REFERENCE_PROVENANCE=' + str(root / ('provenance-' + candidate['flavor'])),
            '-DOTP_REBAR3_FILE=' + str(root / 'rebar3'), '-DOTP_POSTGRES_ROOT=' + str(postgres),
            '-DOTP_LDAP_ROOT=' + str(ldap), '-DOTP_FTP_SERVER=' + str(ftp / 'usr/sbin/vsftpd'),
            '-DOTP_SHORT_TEST_ROOT=' + str(short)]
        if run_command(command, work, 'otp-configure', timeout=600):
            raise RuntimeError('OTP configuration failed')
        if run_command(['cmake', '--build', str(work / 'build'), '--parallel', '2'], work, 'otp-build'):
            raise RuntimeError('OTP build failed')
        return qualify_suites(args, candidate, root / 'otp', sdk, work)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['record', 'inputs-dir', 'work', 'report', 'vm-report']:
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    try:
        return qualify(args)
    except Exception as error:
        for path in (args.report, args.vm_report):
            if Path(path).is_file():
                report = release.read(path)
                if report.get('tests') and all(test['status'] == 'unexecuted' for test in report['tests']):
                    for test in report['tests']:
                        test['reason'] = str(error)
                    release.write(path, report)
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
