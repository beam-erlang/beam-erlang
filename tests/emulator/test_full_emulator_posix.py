"""Preparation supervision regressions; no candidate VM or upstream edits."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


SOURCE_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("full_emulator_posix", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class PosixPreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-posix-preparation-", dir=SOURCE_ROOT.parent / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ("home", "work", "runtime/lib", "bin"):
            (self.root / name).mkdir(parents=True)
        self.args = SimpleNamespace(runtime_bin=self.root / "bin", process_tool=self.root / "process-tool")
        self.runtime = self.root / "runtime"
        self.log = self.root / "compile.log"

    def boot(self, timeout=1):
        # These process-supervision tests mock Popen; locale probing uses Popen
        # internally and has its own native and failure-path regression tests.
        with mock.patch.object(DRIVER, "linux_utf8_locale", return_value="C.utf8"):
            return DRIVER.boot(self.args, self.runtime, self.root, "", [], self.log, timeout=timeout)

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_success_journals_candidate_and_cleans_before_returning(self):
        process = mock.Mock(returncode=0)
        process.poll.side_effect = [None, 0]
        with mock.patch.object(DRIVER.subprocess, "Popen", return_value=process) as launch, \
                mock.patch.object(DRIVER, "owned_processes") as discover, \
                mock.patch.object(DRIVER, "cleanup", return_value=[]) as cleanup:
            self.boot()
        command = launch.call_args.args[0]
        environment = launch.call_args.kwargs["env"]
        self.assertEqual(str(self.args.process_tool), command[0])
        self.assertEqual(str(self.args.runtime_bin / "beam"), environment["BEAM_TEST_ACTUAL_VM"])
        self.assertEqual(str(self.root / "compile-processes.jsonl"), environment["BEAM_TEST_PROCESS_JOURNAL"])
        self.assertTrue(environment["BEAM_TEST_RUN_NONCE"])
        discover.assert_called_once()
        cleanup.assert_called_once()
        self.assertEqual({"owned_processes": [], "remaining": [], "complete": True},
                         DRIVER.read_json(self.root / "compile-cleanup.json"))

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_timeout_collects_children_while_parent_is_alive_then_cleans(self):
        process = mock.Mock(returncode=None)
        process.poll.return_value = None
        child = {"pid": 123, "path": "/owned-child", "birth_sec": 4, "birth_usec": 5}
        order = []

        def discover(tool, runtime, journal, nonce, known):
            known[child["pid"]] = child
            order.append("discover")

        def cleanup(tool, runtime, journal, nonce, known):
            self.assertEqual(child, known[child["pid"]])
            order.append("cleanup")
            return []

        process.wait.side_effect = lambda **kwargs: order.append("wait") or 0
        with mock.patch.object(DRIVER.subprocess, "Popen", return_value=process), \
                mock.patch.object(DRIVER, "owned_processes", side_effect=discover), \
                mock.patch.object(DRIVER, "cleanup", side_effect=cleanup), \
                mock.patch.object(DRIVER.time, "monotonic", side_effect=[0, 0, 2]):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.boot()
        self.assertEqual(["discover", "cleanup", "wait"], order)
        process.kill.assert_not_called()
        process.terminate.assert_not_called()
        self.assertEqual([child], DRIVER.read_json(self.root / "compile-cleanup.json")["owned_processes"])

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_failed_candidate_still_records_complete_cleanup(self):
        process = mock.Mock(returncode=23)
        process.poll.return_value = 23
        with mock.patch.object(DRIVER.subprocess, "Popen", return_value=process), \
                mock.patch.object(DRIVER, "cleanup", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, r"Candidate command failed \(23\)"):
                self.boot()
        self.assertTrue(DRIVER.read_json(self.root / "compile-cleanup.json")["complete"])

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_cleanup_failure_remains_failure_with_evidence(self):
        process = mock.Mock(returncode=0)
        process.poll.return_value = 0
        with mock.patch.object(DRIVER.subprocess, "Popen", return_value=process), \
                mock.patch.object(DRIVER, "cleanup", side_effect=RuntimeError("inspection failed")):
            with self.assertRaisesRegex(RuntimeError, "Preparation process cleanup failed"):
                self.boot()
        receipt = DRIVER.read_json(self.root / "compile-cleanup.json")
        self.assertFalse(receipt["complete"])
        self.assertIsNone(receipt["remaining"])
        self.assertEqual("inspection failed", receipt["error"])

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_spawn_failure_records_cleanup_and_preserves_original_error(self):
        with mock.patch.object(DRIVER.subprocess, "Popen", side_effect=OSError("spawn failed")), \
                mock.patch.object(DRIVER, "cleanup", return_value=[]):
            with self.assertRaisesRegex(OSError, "spawn failed"):
                self.boot()
        self.assertTrue(DRIVER.read_json(self.root / "compile-cleanup.json")["complete"])

    @mock.patch.object(DRIVER, "WINDOWS", False)
    def test_linux_environment_is_explicit_and_does_not_inherit_external_erlang(self):
        with mock.patch.dict(DRIVER.os.environ, {"ERL_FLAGS": "external", "TZ": "UTC"}):
            environment = DRIVER.environment(self.root, self.args.runtime_bin)
            self.assertEqual("UTC", DRIVER.os.environ["TZ"])
        self.assertNotIn("ERL_FLAGS", environment)
        self.assertEqual("Europe/Stockholm", environment["TZ"])
        with mock.patch.object(DRIVER.platform, "system", return_value="Linux"), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=lambda command, **kwargs:
                                  subprocess.CompletedProcess(command, 0,
                                      "UTF-8\n" if command[-1] == "charmap" else "CET\n")):
            DRIVER.capture_environment(self.root, environment)
        facts = json.loads((self.root / "execution-environment-facts.json").read_text())
        self.assertEqual("Linux", facts["platform"])
        self.assertEqual("Europe/Stockholm", facts["timezone"])


@unittest.skipUnless(DRIVER.platform.system() == "Linux" and os.environ.get("BEAM_LINUX_PROCESS_TOOL"),
                     "Native Linux process helper required")
class NativePosixPreparationTests(unittest.TestCase):
    setUp = PosixPreparationTests.setUp
    boot = PosixPreparationTests.boot

    def test_timeout_cleans_real_candidate_child_and_preserves_unrelated_process(self):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "Native preparation regression needs the selected C compiler")
        self.args.process_tool = Path(os.environ["BEAM_LINUX_PROCESS_TOOL"]).resolve()
        source = self.root / "candidate.c"
        source.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    char path[4096];
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        execl("/bin/sleep", "sleep", "60", (char *) 0);
        _exit(3);
    }
    snprintf(path, sizeof(path), "%s/preparation-child.pid", getenv("HOME"));
    FILE *out = fopen(path, "w");
    if (!out) return 4;
    fprintf(out, "%ld\n", (long) child);
    fclose(out);
    for (;;) pause();
}''')
        subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(source),
                        "-o", str(self.args.runtime_bin / "beam")], check=True, capture_output=True)
        unrelated = subprocess.Popen(["/bin/sleep", "60"])
        try:
            with self.assertRaises(subprocess.TimeoutExpired):
                self.boot(timeout=0.5)
            child = int((self.root / "home/preparation-child.pid").read_text())
            self.assertIsNone(DRIVER.inspect(self.args.process_tool, child))
            self.assertIsNone(unrelated.poll())
            receipt = DRIVER.read_json(self.root / "compile-cleanup.json")
            self.assertTrue(receipt["complete"])
            self.assertEqual([], receipt["remaining"])
            self.assertIn(child, [record["pid"] for record in receipt["owned_processes"]])
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
