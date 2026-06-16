"""Independent OTP/VM execution and cleanup of acquired qualification inputs."""
import importlib.util
import os
from pathlib import Path
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import release

spec = importlib.util.spec_from_file_location('qualify_otp', release.ROOT / 'release/qualify-otp.py')
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)


class QualificationTests(unittest.TestCase):
    def inputs(self, root):
        candidate = {'schema': 1, 'version': '17.0-r1-preview.1',
            **release.version_metadata('17.0-r1-preview.1'), 'flavor': 'emu',
            'sdk': {'identity': '17.0-r1-preview.1', 'build-id': 'a' * 64, 'flavor': 'emu'},
            'archive': {'name': 'sdk.tar.gz'},
            'inputs': {'otp': {'commit': 'b' * 40}, 'qualification_assets': {}}}
        record = root / 'candidate.json'
        release.write(record, candidate)
        args = SimpleNamespace(record=str(record), inputs_dir=str(root / 'inputs'),
            work=str(root / 'work'), report=str(root / 'otp.json'), vm_report=str(root / 'beam-vm.json'))
        return args, candidate

    def test_otp_failure_or_timeout_still_runs_vm_and_preserves_both_reports(self):
        for timeout in [False, True]:
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                args, candidate = self.inputs(root)
                calls = []
                def execute(command, work, label, **kwargs):
                    calls.append((label, command))
                    if label == 'otp-ctest' and timeout:
                        raise AssertionError('owned OTP process timed out')
                    if label.endswith('ctest'):
                        path = command[command.index('--report') + 1]
                        release.write(path, {'kind': 'otp' if label == 'otp-ctest' else 'beam-vm',
                                             'eligible': label != 'otp-ctest'})
                    return 1 if label == 'otp-ctest' else 0
                with patch.object(qualification, 'run_command', side_effect=execute), \
                        patch.object(qualification, 'stage_product'):
                    status = qualification.qualify_suites(args, candidate, root / 'otp', root / 'sdk', root)
                self.assertNotEqual(status, 0)
                self.assertEqual([label for label, _ in calls],
                                 ['otp-install', 'vm-configure', 'vm-ctest', 'otp-ctest'])
                self.assertFalse(release.read(args.report)['eligible'])
                if timeout:
                    self.assertEqual({test['status'] for test in release.read(args.report)['tests']}, {'untested'})
                self.assertTrue(release.read(args.vm_report)['eligible'])
                configure = next(command for label, command in calls if label == 'vm-configure')
                self.assertIn('-DBEAM_VM_TEST_SDK_BUILD_ID=' + 'a' * 64, configure)
                self.assertFalse(any('OTP_SOURCE' in word or 'OTP_BUILD' in word for word in configure))

    def test_completed_consumer_cleanup_removes_owned_payloads_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            args, candidate = self.inputs(root)
            source = root / 'inputs/otp'
            source.mkdir(parents=True)
            marker = source / 'preserved'
            marker.write_text('input')
            def extract(archive, destination):
                destination.mkdir(parents=True)
                (destination / 'payload.bin').write_bytes(b'payload')
                return destination
            def consumers(args, candidate, source, sdk, work):
                self.assertTrue((sdk / 'payload.bin').exists())
                release.write(args.report, {'eligible': False})
                release.write(args.vm_report, {'eligible': True})
                return 1
            with patch.object(release, 'inputs'), patch.object(release, 'verify_asset'), \
                    patch.object(release, 'clean_revision', return_value='b' * 40), \
                    patch.object(release, 'extract', side_effect=extract), \
                    patch.object(qualification, 'run_command', return_value=0), \
                    patch.object(qualification, 'qualify_suites', side_effect=consumers):
                self.assertEqual(qualification.qualify(args), 1)
            self.assertEqual(marker.read_text(), 'input')
            self.assertTrue(Path(args.record).is_file())
            self.assertTrue(Path(args.vm_report).is_file())
            self.assertEqual(list(Path(args.work).rglob('payload.bin')), [])
            cleanups = list(Path(args.work).rglob('cleanup.json'))
            self.assertTrue(cleanups)
            self.assertTrue(all(release.read(path)['payloads_removed'] for path in cleanups))

    def test_vm_preparation_or_test_failure_does_not_suppress_otp(self):
        for failing in ['otp-install', 'vm-configure', 'vm-ctest']:
            with self.subTest(failing=failing), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                args, candidate = self.inputs(root)
                calls = []
                def execute(command, work, label, **kwargs):
                    calls.append(label)
                    if label.endswith('ctest'):
                        release.write(command[command.index('--report') + 1], {'eligible': label != failing})
                    return int(label == failing)
                with patch.object(qualification, 'run_command', side_effect=execute), \
                        patch.object(qualification, 'stage_product'):
                    self.assertNotEqual(qualification.qualify_suites(args, candidate, root / 'otp', root / 'sdk', root), 0)
                self.assertEqual(calls[-1], 'otp-ctest')
                self.assertTrue(release.read(args.report)['eligible'])
                self.assertFalse(release.read(args.vm_report)['eligible'])

    @unittest.skipUnless(os.name == 'posix', 'Process-session cleanup requires POSIX')
    def test_timeout_allows_runner_to_reap_its_separate_child_session(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pidfile = root / 'child.pid'
            script = '''import os, signal, subprocess, sys, time
from pathlib import Path
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(600)'], start_new_session=True)
def stop(signum, frame):
    os.killpg(child.pid, signal.SIGTERM)
    child.wait()
    raise SystemExit(1)
signal.signal(signal.SIGTERM, stop)
Path(sys.argv[1]).write_text(str(child.pid))
while True:
    time.sleep(1)
'''
            try:
                with self.assertRaises(AssertionError):
                    qualification.run_command([sys.executable, '-c', script, str(pidfile)],
                                              root, 'timeout', timeout=1)
                pid = int(pidfile.read_text())
                with self.assertRaises(ProcessLookupError):
                    os.kill(pid, 0)
            finally:
                if pidfile.exists():
                    try:
                        os.kill(int(pidfile.read_text()), 9)
                    except ProcessLookupError:
                        pass

    def test_short_fixture_root_is_unique_bounded_and_cleaned_after_failure(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {}, clear=True):
            root = Path(temporary)
            roots = []
            for index in range(2):
                work = root / str(index)
                work.mkdir()
                with self.assertRaisesRegex(RuntimeError, 'fixture failed'):
                    with qualification.short_work(work) as short:
                        roots.append(short)
                        self.assertEqual(short.parent, Path('/tmp'))
                        self.assertLess(len(str(short)), 40)
                        (short / 'payload.beam').write_bytes(b'disposable')
                        (short / 'result.json').write_text('{"complete":false}')
                        raise RuntimeError('fixture failed')
                self.assertFalse(short.exists())
                evidence = work / 'logs/short-path-tests'
                self.assertFalse((evidence / 'payload.beam').exists())
                self.assertFalse(release.read(evidence / 'result.json')['complete'])
                self.assertIn('fixture failed', release.read(evidence / 'cleanup.json')['outcome'])
            self.assertNotEqual(*roots)


if __name__ == '__main__':
    unittest.main()
