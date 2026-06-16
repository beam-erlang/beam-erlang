"""Reject false passes and retain native adapter diagnostics and cleanup."""

from pathlib import Path
import json
import os
import signal
import sys
import tempfile
import time
import unittest

from .ethread import CASES, SKIP, classify, execute


class EthreadRunnerTests(unittest.TestCase):
    test_root = None

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=self.test_root)
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)

    def python(self, text, case="mutex", timeout=3):
        return execute([sys.executable, "-c", text], case, self.work, timeout=timeout)

    def test_success_retains_output(self):
        self.assertEqual(self.python("import sys; print('diagnostic'); "
                                     "print('ETHR-TEST-SUCCESSdone', file=sys.stderr)"), 0)
        self.assertEqual((self.work / "stdout.log").read_bytes(), b"diagnostic\n")
        self.assertEqual((self.work / "stderr.log").read_bytes(), b"ETHR-TEST-SUCCESSdone\n")
        self.assertEqual(json.loads((self.work / "result.json").read_text())["outcome"], "passed")

    def test_permitted_skip(self):
        self.assertEqual(self.python("print('ETHR-TEST-SKIPthread names are not supported')",
                                     case="thread_name"), SKIP)
        self.assertEqual(json.loads((self.work / "result.json").read_text())["outcome"], "skipped")

    def test_native_protocol_rejections(self):
        for case, code, out, err in (
            ("mutex", 0, b"", b""),
            ("mutex", 0, b"ETHR-TEST-FAILUREbroken\n", b""),
            ("mutex", 0, b"ETHR-TEST-SKIPthread names are not supported\n", b""),
            ("thread_name", 0, b"ETHR-TEST-SKIPNo ethread library to test\n", b""),
            ("mutex", 0, b"ETHR-TEST-SUCCESS\n", b"ETHR-TEST-SUCCESS\n"),
            ("mutex", 0, b"ETHR-TEST-SUCCESS\nETHR-TEST-FAILUREbroken\n", b""),
            ("thread_name", 0, b"ETHR-TEST-SUCCESS\nETHR-TEST-SKIPthread names are not supported\n", b""),
            ("mutex", 0, b"ETHR-TEST-UNKNOWN\nETHR-TEST-SUCCESS\n", b""),
            ("mutex", 0, b"ETHR-TEST-PIDbad\nETHR-TEST-SUCCESS\n", b""),
            ("mutex", 1, b"ETHR-TEST-SUCCESS\n", b""),
            ("thread_name", 1, b"ETHR-TEST-SKIPthread names are not supported\n", b""),
        ):
            with self.subTest(case=case, code=code, stdout=out, stderr=err):
                with self.assertRaises(AssertionError):
                    classify(case, code, out, err)

    def test_failure_and_crash_retain_logs(self):
        for ending in ("sys.exit(9)", "os.kill(os.getpid(), signal.SIGKILL)"):
            with self.subTest(ending=ending):
                with self.assertRaisesRegex(AssertionError, "status"):
                    self.python("import os,sys,signal; print('before failure', flush=True); " + ending)
                self.assertIn(b"before failure", (self.work / "stdout.log").read_bytes())
                self.assertEqual(json.loads((self.work / "result.json").read_text())["outcome"], "failed")

    def test_timeout_cleans_process_group_and_retains_logs(self):
        script = ("import os,time; from pathlib import Path; pid=os.fork(); "
                  "Path('child.pid').write_text(str(pid)) if pid else None; "
                  "print('before timeout', flush=True); time.sleep(60)")
        with self.assertRaisesRegex(AssertionError, "TimeoutExpired"):
            self.python(script, timeout=0.5)
        pid = int((self.work / "child.pid").read_text())
        self.assert_stopped(pid)
        self.assertIn(b"before timeout", (self.work / "stdout.log").read_bytes())
        self.assertEqual(json.loads((self.work / "result.json").read_text())["outcome"], "failed")

    def assert_stopped(self, pid):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                state = Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1].split()[0]
                if state == "Z":
                    break
            except (FileNotFoundError, ProcessLookupError):
                break
            time.sleep(0.01)
        else:
            os.kill(pid, signal.SIGKILL)
            self.fail("native adapter left a descendant running after failure")

    def test_protocol_failure_cleans_descendant_after_leader_exit(self):
        script = ("import os,time; from pathlib import Path; pid=os.fork(); "
                  "Path('child.pid').write_text(str(pid)) if pid else None; "
                  "os._exit(0) if pid else time.sleep(60)")
        with self.assertRaisesRegex(AssertionError, "terminal marker"):
            self.python(script)
        self.assert_stopped(int((self.work / "child.pid").read_text()))

    def test_stress_cases_are_not_selected(self):
        self.assertEqual(len(CASES), 13)
        for case in ("detached_thread", "max_threads", "unknown"):
            with self.assertRaises(ValueError):
                execute([sys.executable], case, self.work)
