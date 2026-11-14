"""Exercise actual private native commands with bounded filesystem/process work."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


WORKSPACE_TMP = Path(__file__).resolve().parents[3] / "tmp"
COMMAND_BIN = os.environ.get("BEAM_WINDOWS_COMMAND_BIN")


@unittest.skipUnless(sys.platform == "win32" and COMMAND_BIN,
                     "requires native Windows and BEAM_WINDOWS_COMMAND_BIN")
class NativeWindowsCommandTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="windows-command-test-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = Path(COMMAND_BIN).resolve()
        self.env = dict(os.environ, PATH=os.pathsep.join((str(self.bin),
                            str(Path(os.environ["SystemRoot"]) / "System32"))))

    def invoke(self, command, *arguments, **kwargs):
        return subprocess.run([str(self.bin / (command + ".exe")), *arguments],
                              cwd=self.root, env=self.env, capture_output=True,
                              text=True, encoding="utf-8", timeout=10, **kwargs)

    def test_true_has_ordinary_success_semantics_and_no_output(self):
        for arguments in ((), ("ignored", "two words", "\u03bb", "-x")):
            with self.subTest(arguments=arguments):
                result = self.invoke("true", *arguments)
                self.assertEqual(0, result.returncode)
                self.assertEqual("", result.stdout)
                self.assertEqual("", result.stderr)

    def test_ls_lists_actual_names_including_spaces_and_unicode(self):
        (self.root / "zeta").mkdir()
        (self.root / "alpha file").write_text("contents")
        (self.root / "unicode-\u03bb").write_text("contents")
        (self.root / ".hidden").write_text("contents")
        result = self.invoke("ls")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(["alpha file", "unicode-\u03bb", "zeta"], result.stdout.splitlines())
        self.assertEqual("", result.stderr)

    def test_ls_path_operands_empty_directory_and_error_statuses(self):
        directory = self.root / "empty \u03bb directory"
        directory.mkdir()
        self.assertEqual("", self.invoke("ls", str(directory)).stdout)
        named_file = self.root / "file \u03bb"
        named_file.write_text("contents")
        result = self.invoke("ls", str(named_file))
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([str(named_file)], result.stdout.splitlines())
        for arguments in (("does-not-exist",), ("--unsupported",)):
            with self.subTest(arguments=arguments):
                result = self.invoke("ls", *arguments)
                self.assertEqual(2, result.returncode)
                self.assertTrue(result.stderr)

    def test_sleep_waits_in_seconds_and_validates_arguments(self):
        started = time.monotonic()
        result = self.invoke("sleep", "0.08")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertGreaterEqual(time.monotonic() - started, 0.07)
        self.assertEqual("", result.stdout)
        for arguments in ((), ("-1",), ("nan",), ("inf",), ("1e100",),
                          ("invalid",), ("0", "1"), ("1seconds",)):
            with self.subTest(arguments=arguments):
                result = self.invoke("sleep", *arguments)
                self.assertEqual(1, result.returncode)
                self.assertTrue(result.stderr)

    def test_five_second_sleep_remains_alive_for_port_unregister(self):
        process = subprocess.Popen([str(self.bin / "sleep.exe"), "5"],
                                   cwd=self.root, env=self.env)
        try:
            time.sleep(0.1)
            self.assertIsNone(process.poll())
        finally:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)

    def test_cmd_dispatch_matches_retained_windows_commands(self):
        comspec = str(Path(os.environ["SystemRoot"]) / "System32/cmd.exe")
        (self.root / "listed by cmd").write_text("contents")
        for command in ("true", "ls"):
            result = subprocess.run([comspec, "/d", "/q", "/c", command],
                                    cwd=self.root, env=self.env, capture_output=True,
                                    text=True, encoding="utf-8", timeout=10)
            self.assertEqual(0, result.returncode, result.stderr)
            if command == "ls":
                self.assertEqual(["listed by cmd"], result.stdout.splitlines())


if __name__ == "__main__":
    unittest.main()
