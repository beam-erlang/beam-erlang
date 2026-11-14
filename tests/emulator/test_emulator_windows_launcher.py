"""Native generated-launcher regressions using a fake driver and private Job.

Set BEAM_WINDOWS_PROCESS_TOOL to the selected private native helper. Optional
BEAM_WINDOWS_LAUNCHER_TEMPLATE and BEAM_TEST_SOURCE_ROOT select a pending template
for validation without editing the product. No Erlang VM is launched.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


SOURCE_ROOT = Path(os.environ.get("BEAM_TEST_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
TEMPLATE = Path(os.environ.get("BEAM_WINDOWS_LAUNCHER_TEMPLATE",
                               SOURCE_ROOT / "tests/cmake/RunEmulator.py.in"))
WORKSPACE_TMP = SOURCE_ROOT.parent / "tmp"
TOOL = os.environ.get("BEAM_WINDOWS_PROCESS_TOOL")
FIXED = ["--fixture", 'space \u03bb/quote"']

FAKE_DRIVER = r'''
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

root = Path(os.environ["BEAM_FAKE_DRIVER_ROOT"])
mode = os.environ["BEAM_FAKE_DRIVER_MODE"]
tool = os.environ["BEAM_WINDOWS_PROCESS_TOOL"]
(root / "arguments.json").write_text(json.dumps(sys.argv), encoding="utf-8")
if mode in ("exit77", "failure"):
    time.sleep(0.25)
    sys.exit(77 if mode == "exit77" else 19)
if mode == "args":
    sys.exit(0)

def inspect(pid):
    result = subprocess.run([tool, "inspect", str(pid)], check=True,
                            capture_output=True, text=True, encoding="utf-8")
    return json.loads(result.stdout)

(root / "driver-record.json").write_text(json.dumps(inspect(os.getpid())), encoding="utf-8")
jobname = "Local\\beam-full-" + uuid.uuid4().hex
env = dict(os.environ, BEAM_TEST_JOB_NAME=jobname,
           BEAM_TEST_RUN_NONCE=uuid.uuid4().hex,
           BEAM_TEST_PROCESS_JOURNAL=str(root / "processes.jsonl"))
with (root / "job.log").open("wb") as log:
    server = subprocess.Popen([tool, "job-server", jobname], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=log, env=env)
    state = json.loads(server.stdout.readline())
    if state != {"status": "ready", "job": jobname}:
        raise RuntimeError("unexpected native Job readiness")
    (root / "server-record.json").write_text(json.dumps(inspect(server.pid)), encoding="utf-8")
    record = root / "child.json"
    subprocess.run([tool, "spawn", str(record), sys.executable, "-c",
                    "import time; time.sleep(120)"], env=env, stdout=log,
                   stderr=subprocess.STDOUT, check=True, timeout=10)
    ready = {"driver": inspect(os.getpid()), "server": inspect(server.pid),
             "child": json.loads(record.read_text(encoding="utf-8"))}
    temporary = root / "ready.part"
    temporary.write_text(json.dumps(ready), encoding="utf-8")
    temporary.replace(root / "ready.json")
    while True:
        time.sleep(1)
'''


@unittest.skipUnless(sys.platform == "win32" and TOOL,
                     "requires native Windows and BEAM_WINDOWS_PROCESS_TOOL")
class WindowsLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="windows-launcher-test-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.driver = self.root / "fake_driver.py"
        self.driver.write_text(FAKE_DRIVER, encoding="utf-8")
        self.launcher = self.root / "run.py"
        rendered = TEMPLATE.read_text(encoding="utf-8")
        replacements = {"@BEAM_FULL_LAUNCHER_PYTHON@": repr(sys.executable),
                        "@BEAM_FULL_LAUNCHER_DRIVER@": repr(str(self.driver)),
                        "@BEAM_FULL_LAUNCHER_ARGUMENTS@": "".join("    " + repr(arg) + ",\n" for arg in FIXED)}
        for token, replacement in replacements.items():
            self.assertEqual(1, rendered.count(token), token)
            rendered = rendered.replace(token, replacement)
        self.launcher.write_text(rendered, encoding="utf-8")

    def environment(self, mode):
        return dict(os.environ, BEAM_FAKE_DRIVER_MODE=mode,
                    BEAM_FAKE_DRIVER_ROOT=str(self.root))

    def invoke(self, mode, *selection):
        log = self.root / "launcher.log"
        with log.open("wb") as output:
            result = subprocess.run([sys.executable, "-B", str(self.launcher), *selection],
                                    env=self.environment(mode), stdout=output,
                                    stderr=subprocess.STDOUT, timeout=15)
        return result, log.read_text(encoding="utf-8", errors="replace")

    def inspect(self, record):
        result = subprocess.run([TOOL, "inspect", str(record["pid"])],
                                capture_output=True, text=True, encoding="utf-8", timeout=5)
        if result.returncode:
            return None
        current = json.loads(result.stdout)
        keys = ("birth_sec", "birth_usec", "uid", "path")
        return current if all(current.get(key) == record.get(key) for key in keys) else None

    def terminate(self, record):
        if self.inspect(record):
            subprocess.run([TOOL, "signal", str(record["pid"]), str(record["birth_sec"]),
                            str(record["birth_usec"]), record["path"], "9"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)

    def wait_gone(self, record, timeout=4):
        deadline = time.monotonic() + timeout
        while self.inspect(record) and time.monotonic() < deadline:
            time.sleep(0.05)
        return self.inspect(record) is None

    def test_forwards_fixed_selection_and_normalized_cases(self):
        result, log = self.invoke("args", "--suite", "probe_SUITE", "--timeout", "37",
                                  "--group", "outer", "--cases", "one, two")
        self.assertEqual(0, result.returncode, log)
        self.assertEqual([str(self.driver), "run", *FIXED, "--suite", "probe_SUITE",
                          "--timeout", "37", "--group", "outer", "--cases", "one,two"],
                         json.loads((self.root / "arguments.json").read_text(encoding="utf-8")))

    def test_waits_and_preserves_not_applicable_exit77(self):
        before = time.monotonic()
        result, log = self.invoke("exit77", "--suite", "probe_SUITE")
        self.assertEqual(77, result.returncode, log)
        self.assertGreaterEqual(time.monotonic() - before, 0.2)

    def test_waits_and_preserves_delayed_failure(self):
        before = time.monotonic()
        result, log = self.invoke("failure", "--suite", "probe_SUITE")
        self.assertEqual(19, result.returncode, log)
        self.assertGreaterEqual(time.monotonic() - before, 0.2)

    def test_wrapper_death_closes_job_and_preserves_unrelated_process(self):
        outside = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        launcher = None
        ready = None
        try:
            with (self.root / "launcher.log").open("wb") as output:
                launcher = subprocess.Popen([sys.executable, "-B", str(self.launcher),
                                             "--suite", "probe_SUITE"],
                                            env=self.environment("contain"),
                                            stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 10
            while not (self.root / "ready.json").is_file() and time.monotonic() < deadline:
                if launcher.poll() is not None:
                    self.fail("launcher exited before native Job readiness: " +
                              (self.root / "launcher.log").read_text(encoding="utf-8", errors="replace"))
                time.sleep(0.05)
            self.assertTrue((self.root / "ready.json").is_file(), "native Job readiness timeout")
            ready = json.loads((self.root / "ready.json").read_text(encoding="utf-8"))
            self.assertIsNotNone(self.inspect(ready["child"]))
            # Terminate only this unreaped Popen child, not a process tree.
            launcher.terminate()
            launcher.wait(timeout=5)
            self.assertTrue(self.wait_gone(ready["child"]),
                            "run-owned child survived generated launcher death")
            self.assertTrue(self.wait_gone(ready["server"]), "private Job server survived EOF")
            self.assertIsNone(outside.poll(), "unrelated process was terminated")
        finally:
            if launcher and launcher.poll() is None:
                launcher.terminate()
                launcher.wait(timeout=5)
            if ready:
                # The old subprocess wrapper intentionally fails the regression;
                # terminate its exact receipted driver so its private Job closes.
                self.terminate(ready["driver"])
                self.wait_gone(ready["child"])
                self.terminate(ready["child"])
                self.terminate(ready["server"])
            else:
                # Keep failures before the aggregate readiness receipt contained.
                for name in ("driver-record.json", "child.json", "server-record.json"):
                    record_path = self.root / name
                    if record_path.is_file():
                        self.terminate(json.loads(record_path.read_text(encoding="utf-8")))
            if outside.poll() is None:
                outside.terminate()
            outside.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
