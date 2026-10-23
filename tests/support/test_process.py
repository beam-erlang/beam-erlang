"""Regression checks for the acceptance harness itself."""

from pathlib import Path
from unittest.mock import patch
import os
import json
import subprocess
import sys
import tempfile
import time
import unittest

from .scratch import scratch
from .files import hidden, hidden_paths
from .process import child_process, isolated_environment, kill_group, run


class HarnessTests(unittest.TestCase):
    test_root = None

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=self.test_root)
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.env = isolated_environment(self.work)

    def test_scratch_cleanup_success_failure_and_interrupt(self):
        outside = self.work / "selected-sdk"
        outside.mkdir()
        (outside / "input").write_bytes(b"unchanged")
        for error in (None, RuntimeError("failed"), KeyboardInterrupt()):
            root = self.work / "case"
            root.mkdir(exist_ok=True)
            try:
                with scratch(root):
                    (root / "payload").write_bytes(b"large disposable payload")
                    (root / "input-link").symlink_to(outside, target_is_directory=True)
                    (root / "result.json").write_text('{"checked": true}')
                    if error is not None:
                        raise error
            except BaseException as caught:
                self.assertIs(caught, error)
            self.assertFalse((root / "payload").exists())
            self.assertFalse((root / "input-link").exists())
            self.assertEqual((outside / "input").read_bytes(), b"unchanged")
            self.assertTrue(json.loads((root / "result.json").read_text())["checked"])
            self.assertTrue(json.loads((root / "cleanup.json").read_text())["payloads_removed"])

    def test_scratch_diagnostic_limit_and_explicit_retention(self):
        root = self.work / "case"
        root.mkdir()
        with scratch(root):
            (root / "large.log").write_bytes(b"x" * (2 * 1024 * 1024))
        self.assertEqual((root / "large.log").stat().st_size, 1024 * 1024)
        with patch.dict(os.environ, {"BEAM_TEST_KEEP_WORK": "1"}):
            with scratch(root):
                (root / "payload").write_bytes(b"debug")
        self.assertTrue((root / "payload").is_file())

    def test_scratch_sigterm_cleanup(self):
        root = self.work / "terminated"
        program = ("import os, signal, sys; from pathlib import Path; "
                   f"sys.path.insert(0, {str(Path(__file__).resolve().parents[1])!r}); "
                   "from support.scratch import scratch; "
                   f"root=Path({str(root)!r}); root.mkdir();\n"
                   "with scratch(root):\n"
                   "    (root/'payload').write_bytes(b'temporary')\n"
                   "    os.kill(os.getpid(), signal.SIGTERM)\n")
        child = subprocess.run([sys.executable, "-B", "-c", program],
                               capture_output=True, timeout=5)
        self.assertEqual(child.returncode, 143, child.stderr)
        self.assertFalse((root / "payload").exists())
        self.assertIn("SystemExit", json.loads((root / "cleanup.json").read_text())["outcome"])

    def python(self, source, **options):
        return run([sys.executable, "-c", source], cwd=self.work,
                   env=self.env, timeout=3, **options)

    def test_capture_and_status(self):
        result = self.python("import sys; print('out'); print('err', file=sys.stderr); sys.exit(7)",
                             status=(7, 8))
        self.assertEqual((result.returncode, result.stdout, result.stderr), (7, b"out\n", b"err\n"))
        with self.assertRaisesRegex(AssertionError, "status=9, expected=0") as caught:
            self.python("import sys; print('retained'); sys.exit(9)")
        self.assertIn("retained", str(caught.exception))
        self.assertEqual(self.python("print('λ')", text=True).stdout, "λ\n")
        self.assertEqual(self.python("import sys; sys.stdout.buffer.write(b'\\xff')").stdout, b"\xff")
        with self.assertRaisesRegex(AssertionError, "UnicodeDecodeError"):
            self.python("import sys; sys.stdout.buffer.write(b'\\xff')", text=True)

    def test_environment(self):
        overrides = {name: "poison" for name in (
            "ERL_FLAGS", "BEAM_PREFIX", "BEAM_DIR", "DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH",
            "LD_PRELOAD", "LD_AUDIT", "ESOCK_DEBUG", "ROOTDIR", "BINDIR", "EMU",
            "CMAKE_PREFIX_PATH", "PYTHONPATH", "PYTHONHOME", "LD_DEBUG")}
        with patch.dict(os.environ, overrides):
            os.environ["XDG_RUNTIME_DIR"] = "/inherited-runtime"
            os.environ["XDG_CONFIG_DIRS"] = "/inherited-config"
            os.environ["XDG_DATA_DIRS"] = "/inherited-data"
            env = isolated_environment(self.work)
        self.assertFalse(set(overrides) & env.keys())
        for name in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
                     "XDG_RUNTIME_DIR", "XDG_CONFIG_DIRS", "XDG_DATA_DIRS"):
            self.assertTrue(Path(env[name]).is_dir())
            self.assertEqual(Path(env[name]).parent, self.work)
        self.assertEqual(env["LANG"], "C.UTF-8")
        self.assertEqual(Path(env["XDG_RUNTIME_DIR"]).stat().st_mode & 0o777, 0o700)

    def assert_stopped(self, pid):
        # A killed orphan may await reaping by init; it must no longer execute.
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                state = Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1].split()[0]
            except (FileNotFoundError, ProcessLookupError):
                # Linux procfs may report ESRCH after open but before read.
                return
            if state == "Z":
                return
            time.sleep(0.01)
        self.fail(f"child {pid} survived failed interaction")

    def test_timeout_kills_group_after_leader_exit(self):
        pidfile = self.work / "child.pid"
        source = ("import os,time; from pathlib import Path; pid=os.fork(); "
                  f"Path({str(pidfile)!r}).write_text(str(pid)) if pid else None; "
                  "print('before timeout', flush=True); "
                  "os._exit(0) if pid else time.sleep(60)")
        with self.assertRaisesRegex(AssertionError, "TimeoutExpired") as caught:
            run([sys.executable, "-c", source], cwd=self.work, env=self.env, timeout=0.5)
        self.assertIn("before timeout", str(caught.exception))
        self.assert_stopped(int(pidfile.read_text()))

    def test_interaction_failure_reaps_child(self):
        with self.assertRaisesRegex(AssertionError, "probe failed"):
            with child_process([sys.executable, "-c", "import time; print('ready',flush=True); time.sleep(60)"],
                               cwd=self.work, env=self.env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE) as child:
                self.assertEqual(child.stdout.readline(), b"ready\n")
                raise ValueError("probe failed")
        self.assertIsNotNone(child.returncode)
        with self.assertRaises(ChildProcessError):
            os.waitpid(child.pid, os.WNOHANG)

    def test_success_does_not_hide_surviving_children(self):
        source = ("import os,subprocess,sys; "
                  "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(60)'], "
                  "stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL); "
                  "print(os.getpid(),child.pid)")
        result = self.python(source, text=True)
        leader, child = map(int, result.stdout.split())
        try:
            os.kill(child, 0)
            state = Path(f"/proc/{child}/stat").read_text().split(") ", 1)[1].split()[0]
            self.assertNotEqual(state, "Z")
        finally:
            kill_group(leader)
        self.assert_stopped(child)

    def test_hidden_restores_on_failure(self):
        asset = self.work / "asset"
        asset.write_bytes(b"original")
        with self.assertRaisesRegex(ValueError, "failed"):
            with hidden(asset):
                self.assertFalse(asset.exists())
                raise ValueError("failed")
        self.assertEqual(asset.read_bytes(), b"original")
        with self.assertRaises(FileNotFoundError):
            with hidden_paths([asset, self.work / "missing"]):
                self.fail("partial acquisition should fail")
        self.assertEqual(asset.read_bytes(), b"original")


if __name__ == "__main__":
    unittest.main()
