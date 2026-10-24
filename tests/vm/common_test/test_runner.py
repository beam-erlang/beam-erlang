"""Negative selection, complete-case protocol and owned-process cleanup checks."""

from copy import deepcopy
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

from support.process import isolated_environment
from support.scratch import scratch
from . import runner


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-vm-selection-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sdk, self.otp = self.root / "sdk", self.root / "otp"
        (self.sdk / "lib/beam").mkdir(parents=True)
        (self.otp / "bin").mkdir(parents=True)
        artifacts = {}
        for name in runner.SDK_ROOTS:
            relative = "lib/libbeam.a" if name == "runtime.static" else "lib/beam/" + name
            path = self.sdk / relative
            path.write_bytes(name.encode())
            artifacts[name] = {"kind": "runtime-static" if name == "runtime.static" else "fixture",
                               "path": relative, "sha256": runner.digest(path), "requires": []}
        artifacts["runtime.static"]["requires"] = ["link.static", "link.static.configuration", "link.static.exports"]
        artifacts["header.erl_nif"]["requires"] = ["header.erl_drv_nif", "header.erl_nif_api_funcs"]
        artifacts["header.erl_drv_nif"]["requires"] = ["header.erl_int_sizes_config"]
        for command in ("erl", "erlc"):
            path = self.otp / "bin" / command
            path.write_bytes(b"#!/bin/sh\nexit 0\n")
            path.chmod(0o755)
        self.version, self.build_id = "17.0-r1-preview.1", "a" * 64
        self.manifest = {"format": 1, "version": self.version, "build-id": self.build_id,
                         "flavor": "emu", "architecture": "x86_64", "triple": "x86_64-linux",
                         "artifacts": artifacts}
        self.selection = {"version": self.version, "build_id": self.build_id, "flavor": "emu",
                          "architecture": "x86_64", "triple": "x86_64-linux", "upstream_commit": runner.UPSTREAM,
                          "artifacts": {name: entry["sha256"] for name, entry in artifacts.items()}}
        self.record = {"format": 1, "upstream_commit": runner.UPSTREAM, "runtime": "candidate",
                       "flavor": "emu", "prefix": str(self.otp),
                       "build": {"linkage": "static", "sdk_selection": self.selection},
                       "entries": runner.inventory(self.otp)}
        self.provenance = self.root / "provenance.json"
        self.save()

    def save(self):
        (self.sdk / "lib/beam/runtime-artifacts.json").write_text(json.dumps(self.manifest))
        self.provenance.write_text(json.dumps(self.record))

    def validate(self, **options):
        values = dict(sdk=self.sdk, prefix=self.otp, provenance=self.provenance,
                      version=self.version, build_id=self.build_id, flavor="emu")
        values.update(options)
        return runner.validate_inputs(**values)

    def test_valid_and_relocated_selection(self):
        self.assertEqual(self.validate()["sdk"], self.selection)
        relocated = self.root / "relocated OTP"
        self.otp.rename(relocated)
        record = self.validate(prefix=relocated)
        self.assertEqual(record["installation"]["prefix"], str(relocated))
        self.assertEqual(record["installation"]["original_prefix"], str(self.otp))

    def test_independent_expected_identity(self):
        for values in ({"version": "other"}, {"build_id": "b" * 64}, {"flavor": "jit"}, {"build_id": ""}):
            with self.subTest(values=values), self.assertRaises(ValueError):
                self.validate(**values)

    def test_wrong_runtime_linkage_pin_and_manifest(self):
        original = deepcopy(self.record)
        changes = [("runtime", "reference"), ("format", 2), ("upstream_commit", "b" * 40), ("flavor", "jit")]
        for key, value in changes:
            with self.subTest(key=key):
                self.record = deepcopy(original)
                self.record[key] = value
                self.save()
                with self.assertRaises(ValueError):
                    self.validate()
        self.record = deepcopy(original)
        self.record["build"]["linkage"] = "shared"
        self.save()
        with self.assertRaises(ValueError):
            self.validate()
        self.record = original
        self.manifest["build-id"] = "b" * 64
        self.save()
        with self.assertRaises(ValueError):
            self.validate()

    def test_static_artifact_missing_or_changed(self):
        del self.selection["artifacts"]["runtime.static"]
        self.save()
        with self.assertRaises(ValueError):
            self.validate()
        self.selection["artifacts"]["runtime.static"] = self.manifest["artifacts"]["runtime.static"]["sha256"]
        self.save()
        (self.sdk / "lib/libbeam.a").write_bytes(b"different static archive")
        with self.assertRaisesRegex(ValueError, "SDK artifact differs"):
            self.validate()

    def test_consumed_closure_cannot_shrink(self):
        for name in ("link.static.configuration", "header.erl_int_sizes_config", "core.erlang",
                     "core-source.atomics", "helper.child_setup"):
            with self.subTest(name=name):
                original_hash = self.selection["artifacts"].pop(name)
                self.save()
                with self.assertRaises(ValueError):
                    self.validate()
                original_entry = self.manifest["artifacts"].pop(name)
                self.save()
                with self.assertRaises(ValueError):
                    self.validate()
                self.selection["artifacts"][name] = original_hash
                self.manifest["artifacts"][name] = original_entry
        self.manifest["artifacts"]["header.erl_nif"]["requires"] = ["absent.header"]
        self.save()
        with self.assertRaises(ValueError):
            self.validate()
        self.manifest["artifacts"]["header.erl_nif"]["requires"] = "header.erl_drv_nif"
        self.save()
        with self.assertRaises(ValueError):
            self.validate()

    def test_tree_bytes_modes_extra_entries_and_external_links(self):
        command = self.otp / "bin/erl"
        command.chmod(0o644)
        with self.assertRaises(ValueError):
            self.validate()
        command.chmod(0o755)
        command.write_bytes(b"changed command")
        with self.assertRaises(ValueError):
            self.validate()
        command.write_bytes(b"#!/bin/sh\nexit 0\n")
        (self.otp / "extra").write_text("extra")
        with self.assertRaises(ValueError):
            self.validate()
        (self.otp / "extra").unlink()
        (self.otp / "external").symlink_to(self.sdk)
        with self.assertRaises(ValueError):
            self.validate()

    def test_artifact_escape_and_duplicate_json_keys(self):
        self.manifest["artifacts"]["runtime.static"]["path"] = "../outside"
        self.save()
        with self.assertRaises(ValueError):
            self.validate()
        self.provenance.write_text('{"format":1,"format":1}')
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.validate()


class ProtocolTests(unittest.TestCase):
    suite = "bs_match_tail_SUITE"

    def setUp(self):
        self.cases = runner.CASES[self.suite]
        self.events = [{"event": "handler_start"}] + [
            {"event": "tc_done", "suite": self.suite, "name": case, "status": "passed", "reason": "ok"}
            for case in self.cases] + [{"event": "handler_stop"}]

    def validate(self, events=None, aggregate="{4,0,{0,0}}.", cases=None):
        return runner.protocol(self.suite, self.cases if cases is None else cases,
                               self.events if events is None else events, aggregate)

    def test_complete_result(self):
        cases, counts, complete = self.validate()
        self.assertTrue(complete)
        self.assertEqual(len(cases), 4)
        self.assertEqual(counts, dict(passed=4, failed=0, skipped=0, user_skipped=0, auto_skipped=0))

    def test_missing_repeated_reordered_unknown_and_narrowed_cases(self):
        variants = [self.events[:-1], self.events[:2] + self.events[1:],
                    [self.events[0], self.events[2], self.events[1], *self.events[3:]]]
        unknown = deepcopy(self.events)
        unknown[1]["name"] = "unexpected"
        variants.append(unknown)
        wrong_suite = deepcopy(self.events)
        wrong_suite[1]["suite"] = "other_SUITE"
        variants.append(wrong_suite)
        for events in variants:
            with self.subTest(events=events), self.assertRaises(ValueError):
                self.validate(events=events)
        with self.assertRaises(ValueError):
            self.validate(cases=self.cases[:-1])

    def test_counts_malformed_and_skips(self):
        for aggregate in ("{3,0,{0,0}}.", "{4,0,{0,0}}.\n{4,0,{0,0}}.", "ok"):
            with self.subTest(aggregate=aggregate), self.assertRaises(ValueError):
                self.validate(aggregate=aggregate)
        for status, aggregate in (("failed", "{3,1,{0,0}}."),
                                  ("user_skipped", "{3,0,{1,0}}."),
                                  ("auto_skipped", "{3,0,{0,1}}.")):
            events = deepcopy(self.events)
            events[1]["status"] = status
            self.assertFalse(self.validate(events, aggregate)[2])
        events[1].update(event="tc_auto_skip", status="passed")
        with self.assertRaises(ValueError):
            self.validate(events)

    def test_failed_configuration_is_not_hidden_by_aggregate(self):
        event = dict(event="tc_done", suite=self.suite, name="init_per_suite", status="failed")
        with self.assertRaises(ValueError):
            self.validate([self.events[0], event, *self.events[1:]])

    def test_framework_defaults_are_configuration_only(self):
        begin = dict(event="tc_done", suite="ct_framework", name="init_per_suite", status="passed")
        end = dict(event="tc_done", suite="ct_framework", name="end_per_suite", status="passed")
        events = [self.events[0], begin, *self.events[1:-1], end, self.events[-1]]
        self.assertTrue(self.validate(events)[2])
        for field, value in (("status", "failed"), ("name", "error_in_suite"),
                             ("name", self.cases[0]), ("suite", "other_SUITE")):
            broken = deepcopy(events)
            broken[1][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.validate(broken)


@unittest.skipUnless(sys.platform.startswith("linux"), "Process cleanup uses Linux process observations")
class ProcessTests(unittest.TestCase):
    def assert_stopped(self, pid):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                state = Path(f"/proc/{pid}/stat").read_text().split(")", 1)[1].split()[0]
                if state == "Z":
                    return
            except FileNotFoundError:
                return
            time.sleep(0.01)
        self.fail(f"Owned process {pid} survived cleanup")

    def execute_failure(self, leader_exits):
        with tempfile.TemporaryDirectory(prefix="beam-vm-cleanup-") as temporary:
            root = Path(temporary) / "work"
            root.mkdir()
            program = ("import subprocess,sys,time;from pathlib import Path;"
                       "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)']);"
                       "Path('child.pid').write_text(str(p.pid));"
                       + ("sys.exit(0)" if leader_exits else "time.sleep(60)"))
            def reject(_):
                raise ValueError("missing complete-case protocol")
            pid = None
            with self.assertRaises(AssertionError):
                with scratch(root):
                    try:
                        runner.run_process([sys.executable, "-c", program], root,
                                           isolated_environment(root), 0.3,
                                           reject if leader_exits else None)
                    finally:
                        if (root / "child.pid").is_file():
                            pid = int((root / "child.pid").read_text())
            self.assertIsNotNone(pid, "Synthetic process did not create its descendant")
            self.assert_stopped(pid)
            self.assertTrue(json.loads((root / "cleanup.json").read_text())["payloads_removed"])
            self.assertFalse((root / "home").exists())
            self.assertFalse((root / "child.pid").exists())
            self.assertTrue((root / "stdout.log").exists())

    def test_timeout_cleanup(self):
        self.execute_failure(False)

    def test_protocol_failure_cleans_exited_leader_descendants(self):
        self.execute_failure(True)


if __name__ == "__main__":
    unittest.main()
