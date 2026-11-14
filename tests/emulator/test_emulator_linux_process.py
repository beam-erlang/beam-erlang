"""Native Linux ownership regressions; no external Erlang runtime.

Set BEAM_LINUX_PROCESS_TOOL to the built private emulator_process, then run this
file with the selected native Python. Disposable payloads use workspace tmp.
"""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import uuid


WORKSPACE_TMP = Path(__file__).resolve().parents[3] / "tmp"
TOOL = os.environ.get("BEAM_LINUX_PROCESS_TOOL")


@unittest.skipUnless(sys.platform.startswith("linux") and TOOL,
                     "requires native Linux and BEAM_LINUX_PROCESS_TOOL")
class LinuxProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="linux-process-test-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.env = dict(os.environ, BEAM_TEST_ACTUAL_VM=str(Path(sys.executable).resolve()),
                        BEAM_TEST_RUN_NONCE=uuid.uuid4().hex,
                        BEAM_TEST_PROCESS_JOURNAL=str(self.root / "processes.jsonl"))

    def invoke(self, *arguments, **kwargs):
        return subprocess.run([TOOL, *map(str, arguments)], capture_output=True,
                              text=True, timeout=10, **kwargs)

    def inspect(self, pid):
        result = self.invoke("inspect", pid)
        self.assertEqual(0, result.returncode, result.stderr)
        return json.loads(result.stdout)

    def spawn(self, code="import time; time.sleep(120)", *arguments, **kwargs):
        log = (self.root / (uuid.uuid4().hex + ".log")).open("wb")
        self.addCleanup(log.close)
        process = subprocess.Popen([sys.executable, "-c", code, *map(str, arguments)],
                                   stdin=subprocess.DEVNULL, stdout=log, stderr=log, **kwargs)
        self.addCleanup(self.stop, process)
        return process

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)

    def wait_file(self, path):
        deadline = time.monotonic() + 5
        while not path.exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(path.exists(), str(path))

    def signal(self, identity, signal_no, **overrides):
        identity = dict(identity, **overrides)
        return self.invoke("signal", identity["pid"], identity["birth_sec"],
                           identity["birth_usec"], identity["path"], signal_no)

    def test_inspection_matches_proc_and_handles_parenthesized_comm(self):
        ready = self.root / "ready"
        child = self.spawn(
            "import ctypes,pathlib,sys,time; "
            "assert ctypes.CDLL(None).prctl(15,b'a ) odd\\ncomm',0,0,0)==0; "
            "pathlib.Path(sys.argv[1]).touch(); time.sleep(120)", ready)
        self.wait_file(ready)
        identity = self.inspect(child.pid)
        stat = Path("/proc", str(child.pid), "stat").read_text().rsplit(")", 1)[1].split()
        ticks = os.sysconf("SC_CLK_TCK")
        start = int(stat[19])
        self.assertEqual(child.pid, identity["pid"])
        self.assertEqual(os.getuid(), identity["uid"])
        self.assertEqual(os.getpid(), identity["ppid"])
        self.assertEqual(os.getpgid(child.pid), identity["pgid"])
        self.assertEqual(Path(sys.executable).resolve(), Path(identity["path"]))
        self.assertEqual(start // ticks, identity["birth_sec"])
        self.assertEqual(start % ticks * 1000000 // ticks, identity["birth_usec"])

    def test_mismatched_identity_and_unapproved_signals_preserve_process(self):
        child = self.spawn()
        identity = self.inspect(child.pid)
        for signal_no, changes in (
                (9, {"birth_sec": identity["birth_sec"] + 1}),
                (9, {"birth_usec": (identity["birth_usec"] + 1) % 1000000}),
                (9, {"path": str(Path("/bin/true").resolve())}),
                (1, {}), (0, {})):
            with self.subTest(signal=signal_no, changes=changes):
                result = self.signal(identity, signal_no, **changes)
                self.assertNotEqual(0, result.returncode)
                self.assertIsNone(child.poll())

    def test_verified_term_and_kill_use_live_process_identity(self):
        for signal_no in (15, 9):
            with self.subTest(signal=signal_no):
                child = self.spawn()
                result = self.signal(self.inspect(child.pid), signal_no)
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual(-signal_no, child.wait(timeout=5))
                self.assertNotEqual(0, self.invoke("inspect", child.pid).returncode)

    def test_listener_requires_owned_fd_not_only_shared_network_table(self):
        child = self.spawn()
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
            self.assertNotEqual(0, self.invoke("owns-listener", os.getpid(), port).returncode)
            listener.listen()
            result = self.invoke("owns-listener", os.getpid(), port)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertTrue(json.loads(result.stdout)["owns_listener"])
            self.assertNotEqual(0, self.invoke("owns-listener", child.pid, port).returncode)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as datagram:
            datagram.bind(("127.0.0.1", 0))
            self.assertNotEqual(0, self.invoke("owns-listener", os.getpid(),
                                              datagram.getsockname()[1]).returncode)

    def test_ipv6_listener_is_owned(self):
        with socket.socket(socket.AF_INET6) as listener:
            listener.bind(("::1", 0))
            listener.listen()
            result = self.invoke("owns-listener", os.getpid(), listener.getsockname()[1])
            self.assertEqual(0, result.returncode, result.stderr)

    def test_exec_preserves_arguments_and_reports_post_detach_identity(self):
        ready = self.root / "ready"
        payload = self.root / "arguments.json"
        arguments = ["", "two words", 'embedded"quote', "trailing\\", "unicode-\u03bb"]
        command = [TOOL, "-c", "import json,pathlib,sys,time; "
                   "pathlib.Path(sys.argv[1]).write_text(json.dumps(sys.argv[3:])); "
                   "pathlib.Path(sys.argv[2]).touch(); time.sleep(120)",
                   str(payload), str(ready), *arguments]
        process = subprocess.Popen(command, env=self.env, start_new_session=True,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        self.addCleanup(self.stop, process)
        self.wait_file(ready)
        self.assertEqual(arguments, json.loads(payload.read_text()))
        records = [json.loads(line) for line in
                   Path(self.env["BEAM_TEST_PROCESS_JOURNAL"]).read_text().splitlines()]
        self.assertEqual(1, len(records))
        record = records[0]
        self.assertEqual("exec", record["event"])
        self.assertEqual(self.env["BEAM_TEST_RUN_NONCE"], record["nonce"])
        self.assertEqual(process.pid, record["pid"])
        self.assertEqual(process.pid, record["pgid"])
        self.assertEqual(str(Path(TOOL).resolve()), record["launcher_path"])
        actual = self.inspect(process.pid)
        for key in ("pid", "uid", "birth_sec", "birth_usec", "path"):
            self.assertEqual(record[key], actual[key], key)
        result = self.signal(record, 15)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(-15, process.wait(timeout=5))

    def test_concurrent_exec_receipts_remain_complete_json_records(self):
        nonce = 'quotes" backslash\\ newline\n unicode-\u03bb'
        environment = dict(self.env, BEAM_TEST_RUN_NONCE=nonce)
        with ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(lambda _: self.invoke("-c", "pass", env=environment),
                                        range(16)))
        self.assertTrue(all(result.returncode == 0 for result in results),
                        [result.stderr for result in results if result.returncode])
        records = [json.loads(line) for line in
                   Path(self.env["BEAM_TEST_PROCESS_JOURNAL"]).read_text().splitlines()]
        self.assertEqual(16, len(records))
        self.assertEqual(16, len({record["pid"] for record in records}))
        self.assertTrue(all(record["nonce"] == nonce for record in records))

    def test_symlink_journal_is_rejected_before_candidate_runs(self):
        protected = self.root / "protected"
        protected.write_text("unchanged")
        Path(self.env["BEAM_TEST_PROCESS_JOURNAL"]).symlink_to(protected)
        marker = self.root / "must-not-exist"
        result = self.invoke("-c", "import pathlib,sys; pathlib.Path(sys.argv[1]).touch()",
                             marker, env=self.env)
        self.assertNotEqual(0, result.returncode)
        self.assertFalse(marker.exists())
        self.assertEqual("unchanged", protected.read_text())

    def test_invalid_pid_and_listener_arguments_fail_closed(self):
        for arguments in (("inspect", "0"), ("inspect", "1"), ("inspect", "-2"),
                          ("inspect", "2x"), ("owns-listener", str(os.getpid()), "0"),
                          ("owns-listener", str(os.getpid()), "65536")):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("error", json.loads(result.stderr))


if __name__ == "__main__":
    unittest.main()
