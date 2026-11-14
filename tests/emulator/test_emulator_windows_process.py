"""Native Windows ownership/containment regressions; no external Erlang runtime.

Set BEAM_WINDOWS_PROCESS_TOOL to the built private emulator_process.exe, then run
this file with the selected native Python. Payloads remain under workspace tmp.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import shutil
import time
import unittest
import uuid


WORKSPACE_TMP = Path(__file__).resolve().parents[3] / "tmp"
TOOL = os.environ.get("BEAM_WINDOWS_PROCESS_TOOL")
ERLEXEC = os.environ.get("BEAM_WINDOWS_ERLEXEC")


@unittest.skipUnless(sys.platform == "win32" and TOOL,
                     "requires native Windows and BEAM_WINDOWS_PROCESS_TOOL")
class WindowsProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="windows-process-test-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.name = "Local\\beam-full-" + uuid.uuid4().hex
        self.job = subprocess.Popen([TOOL, "job-server", self.name], stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.close_job)
        self.assertEqual({"status": "ready", "job": self.name},
                         json.loads(self.job.stdout.readline()))
        self.env = dict(os.environ, BEAM_TEST_JOB_NAME=self.name,
                        BEAM_TEST_RUN_NONCE=uuid.uuid4().hex,
                        BEAM_TEST_PROCESS_JOURNAL=str(self.root / "processes.jsonl"))

    def close_job(self):
        if self.job.poll() is None:
            self.job.stdin.close()
            self.job.wait(timeout=10)
        if self.job.returncode:
            self.fail("job cleanup failed: " + self.job.stderr.read())
        self.job.stdout.close()
        self.job.stderr.close()

    def invoke(self, *arguments, **kwargs):
        return subprocess.run([TOOL, *map(str, arguments)], capture_output=True,
                              text=True, timeout=15, **kwargs)

    def spawn(self, code, *arguments):
        record = self.root / (uuid.uuid4().hex + ".json")
        log = self.root / "spawn.log"
        with log.open("wb") as output:
            result = subprocess.run([TOOL, "spawn", str(record), sys.executable,
                                     "-c", code, *arguments], env=self.env,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=15)
        self.assertEqual(0, result.returncode, log.read_text())
        return json.loads(record.read_text())

    def inspect(self, pid):
        result = self.invoke("inspect", pid)
        return json.loads(result.stdout) if not result.returncode else None

    def wait_file(self, path):
        deadline = time.monotonic() + 10
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(path.exists(), str(path))

    def test_run_preserves_arguments_and_receipts_actual_child(self):
        record = self.root / "run.json"
        arguments = ["", "two words", 'embedded"quote', "trailing\\",
                     "slashes\\\\\"quote", "unicode-\u03bb"]
        result = self.invoke("run", record, sys.executable, "-c",
                             "import json,sys; print(json.dumps(sys.argv[1:])); sys.exit(23)",
                             *arguments, env=self.env)
        self.assertEqual(23, result.returncode, result.stderr)
        self.assertEqual(arguments, json.loads(result.stdout))
        identity = json.loads(record.read_text())
        journal = [json.loads(line) for line in
                   (self.root / "processes.jsonl").read_text().splitlines()]
        self.assertEqual(1, len(journal))
        self.assertEqual(identity["pid"], journal[0]["pid"])
        self.assertEqual(self.env["BEAM_TEST_RUN_NONCE"], journal[0]["nonce"])
        self.assertEqual(identity["birth_ticks"],
                         identity["birth_sec"] * 10000000 + identity["birth_usec"])
        self.assertEqual(Path(sys.executable).resolve(), Path(identity["path"]).resolve())

    def test_mismatched_creation_identity_cannot_terminate_process(self):
        identity = self.spawn("import time; time.sleep(120)")
        result = self.invoke("signal", identity["pid"], identity["birth_sec"] + 1,
                             identity["birth_usec"], identity["path"], 9)
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(identity["birth_ticks"], self.inspect(identity["pid"])["birth_ticks"])
        result = self.invoke("signal", identity["pid"], identity["birth_sec"],
                             identity["birth_usec"], identity["path"] + ".wrong", 9)
        self.assertNotEqual(0, result.returncode)
        self.assertIsNotNone(self.inspect(identity["pid"]))
        result = self.invoke("signal", identity["pid"], identity["birth_sec"],
                             identity["birth_usec"], identity["path"], 15)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIsNone(self.inspect(identity["pid"]))

    def test_private_job_cleans_descendants_and_preserves_unrelated_process(self):
        outside = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"])
        def cleanup_outside():
            if outside.poll() is None:
                outside.terminate()
                outside.wait(timeout=5)
        self.addCleanup(cleanup_outside)
        child_file = self.root / "descendant.txt"
        identity = self.spawn(
            "import pathlib,subprocess,sys,time; "
            "p=subprocess.Popen([sys.executable,'-c','import time; time.sleep(120)']); "
            "pathlib.Path(sys.argv[1]).write_text(str(p.pid)); time.sleep(120)",
            str(child_file))
        self.wait_file(child_file)
        descendant = int(child_file.read_text())
        self.assertIsNotNone(self.inspect(descendant))
        self.close_job()
        self.assertIsNone(self.inspect(identity["pid"]))
        self.assertIsNone(self.inspect(descendant))
        self.assertIsNone(outside.poll())
        outside.terminate()
        outside.wait(timeout=5)

    def test_listener_must_belong_to_exact_loopback_owner(self):
        port_file = self.root / "port.txt"
        identity = self.spawn(
            "import pathlib,socket,sys,time; s=socket.socket(); "
            "s.bind(('127.0.0.1',0)); s.listen(); "
            "pathlib.Path(sys.argv[1]).write_text(str(s.getsockname()[1])); time.sleep(120)",
            str(port_file))
        self.wait_file(port_file)
        port = int(port_file.read_text())
        result = self.invoke("owns-listener", identity["pid"], port)
        self.assertEqual(0, result.returncode, result.stderr)
        result = self.invoke("owns-listener", os.getpid(), port)
        self.assertNotEqual(0, result.returncode)

    def test_missing_job_rejects_child_before_payload_runs(self):
        marker = self.root / "must-not-exist.txt"
        record = self.root / "rejected.json"
        environment = dict(self.env, BEAM_TEST_JOB_NAME="Local\\beam-full-nonexistent")
        result = self.invoke("run", record, sys.executable, "-c",
                             "import pathlib,sys; pathlib.Path(sys.argv[1]).touch()",
                             str(marker), env=environment)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse(marker.exists())
        self.assertFalse(record.exists())

    def test_existing_job_name_is_rejected_without_taking_ownership(self):
        result = self.invoke("job-server", self.name, stdin=subprocess.DEVNULL)
        self.assertNotEqual(0, result.returncode)
        self.assertIsNone(self.job.poll())
        self.assertIn("already exists", result.stderr)

    def test_journal_hardlink_is_rejected_before_payload_runs(self):
        protected = self.root / "protected.txt"
        protected.write_text("unchanged")
        os.link(protected, self.root / "processes.jsonl")
        marker = self.root / "must-not-exist.txt"
        result = self.invoke("run", self.root / "rejected.json", sys.executable, "-c",
                             "import pathlib,sys; pathlib.Path(sys.argv[1]).touch()",
                             str(marker), env=self.env)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse(marker.exists())
        self.assertEqual("unchanged", protected.read_text())

    def test_unicode_and_spaced_evidence_paths_are_supported(self):
        nested = self.root / "space \u03bb"
        nested.mkdir()
        record = nested / "record.json"
        environment = dict(self.env, BEAM_TEST_PROCESS_JOURNAL=str(nested / "journal.jsonl"))
        result = self.invoke("run", record, sys.executable, "-c", "pass", env=environment)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertTrue(record.is_file())
        self.assertTrue((nested / "journal.jsonl").is_file())

    def test_same_job_assignment_is_idempotent_and_other_job_is_rejected(self):
        identity = self.spawn("import time; time.sleep(120)")
        arguments = [identity["pid"], identity["birth_sec"], identity["birth_usec"], identity["path"]]
        result = self.invoke("assign-job", *arguments, self.name)
        self.assertEqual(0, result.returncode, result.stderr)
        other_name = "Local\\beam-full-" + uuid.uuid4().hex
        other = subprocess.Popen([TOOL, "job-server", other_name], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            self.assertEqual({"status": "ready", "job": other_name}, json.loads(other.stdout.readline()))
            other_environment = dict(self.env, BEAM_TEST_JOB_NAME=other_name,
                                     BEAM_TEST_PROCESS_JOURNAL=str(self.root / "other-processes.jsonl"))
            with (self.root / "other-spawn.log").open("wb") as output:
                populated = subprocess.run([TOOL, "spawn", str(self.root / "other-process.json"),
                                            sys.executable, "-c", "import time; time.sleep(120)"],
                                           env=other_environment, stdout=output,
                                           stderr=subprocess.STDOUT, timeout=15)
            self.assertEqual(0, populated.returncode, (self.root / "other-spawn.log").read_text())
            result = self.invoke("assign-job", *arguments, other_name)
            self.assertNotEqual(0, result.returncode, "process was reassigned outside its owned run Job")
            self.assertIsNotNone(self.inspect(identity["pid"]))
        finally:
            other.stdin.close()
            other.wait(timeout=10)
            self.assertEqual(0, other.returncode, other.stderr.read())
            other.stdout.close()
            other.stderr.close()

    @unittest.skipUnless(ERLEXEC, "requires the selected private BEAM_WINDOWS_ERLEXEC")
    def test_parser_helper_before_rejected_candidate_is_contained(self):
        directory = self.root / "parser"
        directory.mkdir()
        launcher = directory / "erl.exe"
        shutil.copy2(ERLEXEC, launcher)
        (directory / "erl.ini").write_text(
            "[erlang]\nBindir=" + directory.as_posix() + "\nRootdir=" + self.root.as_posix()
            + "\nProgname=" + launcher.as_posix() + "\n", encoding="utf-8")
        pid_file = self.root / "parser-helper.pid"
        # The unchanged parser starts its custom epmd command before calling
        # start_emulator. Use a bounded Python helper, never an Erlang VM.
        command = subprocess.list2cmdline([sys.executable, "-c",
            "import os,pathlib,sys,time; pathlib.Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(120)",
            str(pid_file)])
        environment = {key: value for key, value in self.env.items()
                       if key.upper() not in ("ERL_AFLAGS", "ERL_FLAGS", "ERL_ZFLAGS", "ERL_CONSOLE_MODE")}
        environment["BEAM_TEST_ACTUAL_VM"] = sys.executable
        record = None
        try:
            with (self.root / "parser.log").open("wb") as output:
                result = subprocess.run([str(launcher), "-sname", "parser_probe", "-epmd", command,
                                         "-emu_type", "lcnt"], env=environment,
                                        stdout=output, stderr=subprocess.STDOUT, timeout=15)
            self.assertEqual(1, result.returncode, (self.root / "parser.log").read_text())
            self.assertIn("cannot select", (self.root / "parser.log").read_text())
            self.wait_file(pid_file)
            record = self.inspect(int(pid_file.read_text()))
            self.assertIsNotNone(record)
            self.assertFalse((self.root / "processes.jsonl").exists(), "rejected candidate reached process backend")
            self.close_job()
            deadline = time.monotonic() + 5
            while self.inspect(record["pid"]) and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertIsNone(self.inspect(record["pid"]), "parser-started helper escaped the run Job")
        finally:
            if record and self.inspect(record["pid"]):
                self.invoke("signal", record["pid"], record["birth_sec"], record["birth_usec"], record["path"], 9)


if __name__ == "__main__":
    unittest.main()
