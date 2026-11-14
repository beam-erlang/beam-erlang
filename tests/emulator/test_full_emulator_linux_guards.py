"""Exact Linux win_massive guard accounting; never launch a VM."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


SOURCE_ROOT = Path(os.environ.get("BEAM_GUARD_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
POLICY_PATH = Path(os.environ.get("BEAM_GUARD_POLICY", SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json"))
SCRATCH = Path(os.environ.get("BEAM_GUARD_TMP", SOURCE_ROOT.parent / "tmp"))
POLICY = json.loads(POLICY_PATH.read_text())
RULE = next(rule for rule in POLICY["rules"] if rule["id"] == "linux-port-win-massive")
MAC_RULE = next(rule for rule in POLICY["rules"] if rule["id"] == "windows-massive-port-case-on-macos")
SPEC = importlib.util.spec_from_file_location("linux_guard_driver", SOURCE_ROOT / "tests/emulator/full_emulator.py")
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)
PINNED_SOURCE_SHA256 = "0e7f45000f98e0f7a0ecdc07d968b2b54992c8d41216ed794b5b2cf1d78ba078"


def linux_observation():
    return {"platform": "Linux", "os_family": "unix", "os_name": "linux"}


class LinuxGuardSourceTests(unittest.TestCase):
    def test_exact_case_and_unchanged_source_guard_are_bound(self):
        self.assertEqual("port_SUITE", RULE["suite"])
        self.assertEqual("win_massive", RULE["case"])
        self.assertEqual("^Only on Windows\\.$", RULE["reason_regex"])
        self.assertNotIn("class", RULE)
        self.assertFalse(RULE["id"].startswith("windows-"))
        for field in ("suite", "case", "reason_regex", "source", "source_sha256", "guard", "guard_lines"):
            self.assertEqual(MAC_RULE[field], RULE[field])
        raw = (SOURCE_ROOT / RULE["source"]).read_bytes()
        canonical = raw.replace(b"\r\n", b"\n")
        self.assertEqual(PINNED_SOURCE_SHA256, RULE["source_sha256"])
        self.assertEqual(PINNED_SOURCE_SHA256, hashlib.sha256(canonical).hexdigest())
        self.assertIn(RULE["guard"], canonical.decode("utf-8"))
        self.assertEqual({"kind": "platform_equals", "value": "Darwin"}, MAC_RULE["condition"])

    def test_linux_requires_native_family_name_and_platform(self):
        self.assertTrue(DRIVER.condition(RULE, {}, {"guard_facts": linux_observation()}))
        for field in ("platform", "os_family", "os_name"):
            facts = linux_observation()
            del facts[field]
            with self.subTest(missing=field):
                self.assertFalse(DRIVER.condition(RULE, {}, {"guard_facts": facts}))

    def test_other_platforms_and_contradictory_native_observations_reject(self):
        for facts in ({"platform": "Windows", "os_family": "win32", "os_name": "nt"},
                      {"platform": "Darwin", "os_family": "unix", "os_name": "darwin"},
                      {"platform": "FreeBSD", "os_family": "unix", "os_name": "freebsd"},
                      dict(linux_observation(), os_family="win32"),
                      dict(linux_observation(), os_name="darwin")):
            with self.subTest(facts=facts):
                self.assertFalse(DRIVER.condition(RULE, {}, {"guard_facts": facts}))
        self.assertFalse(DRIVER.condition(RULE, linux_observation(), {"guard_facts": {"os_name": "nt"}}))

    def test_source_or_guard_drift_is_not_accepted(self):
        with tempfile.TemporaryDirectory(prefix="linux-port-source-", dir=SCRATCH) as temporary:
            root = Path(temporary)
            source = root / RULE["source"]
            source.parent.mkdir(parents=True)
            original = (SOURCE_ROOT / RULE["source"]).read_bytes()
            source.write_bytes(original)
            DRIVER.guard_identity(root, RULE)
            source.write_bytes(original + b"\n% changed source\n")
            with self.assertRaisesRegex(RuntimeError, "source identity changed"):
                DRIVER.guard_identity(root, RULE)
            source.write_bytes(original)
            bad_guard = dict(RULE, guard=RULE["guard"].replace("Only on Windows.", "Only on Linux."))
            with self.assertRaisesRegex(RuntimeError, "source identity changed"):
                DRIVER.guard_identity(root, bad_guard)


class LinuxGuardAccountingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="linux-port-accounting-", dir=SCRATCH)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "product"
        self.evidence = self.root / "attempt"
        self.image = self.evidence / "image"
        self.logs = self.evidence / "logs"
        self.logs.mkdir(parents=True)
        self.image.mkdir()
        for relative in ("UPSTREAM_COMMIT", "tests/emulator/emulator_scope.json", RULE["source"]):
            target = self.source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((SOURCE_ROOT / relative).read_bytes())
        self.policy_path = self.source / "tests/emulator/emulator_skip_policy.json"
        self.policy_path.write_text(json.dumps(POLICY, indent=2) + "\n")
        (self.evidence / "execution-skip-policy.json").write_bytes(self.policy_path.read_bytes())
        self.args = SimpleNamespace(source_root=self.source, flavor="emu", build_type="opt")

    def classify(self, *, case="win_massive", reason="Only on Windows.", observation=None,
                 outcome="skipped", planned=1, omit_terminal=False):
        observation = linux_observation() if observation is None else observation
        selection = DRIVER.requested_selection("port_SUITE", cases=case)
        inventory = dict(selection=copy.deepcopy(selection), coverage_mode="exact_cases",
                         execution_suites=["port_SUITE"], expected_cases=[{"module": "port_SUITE", "case": case}],
                         expected_groups=[], expected_group_paths=[], empty_suites=[], applied_exclusions=[])
        totals = dict(passed=int(outcome == "passed"), failed=int(outcome == "failed"),
                      user_skipped=int(outcome == "skipped"), auto_skipped=int(outcome == "auto_skipped"))
        event = dict(event="tc_done", suite="port_SUITE", case=case, outcome=outcome,
                     reasontext=reason, guard_facts={k: v for k, v in observation.items() if k != "platform"})
        if outcome == "skipped":
            event["skipkind"] = "user"
        elif outcome == "auto_skipped":
            event["skipkind"] = "auto"
        events = [{"event": "handler_started"}, {"event": "start_info", "tests": 1, "suites": 1, "cases": planned},
                  {"event": "tc_start", "suite": "port_SUITE", "case": case}]
        if not omit_terminal:
            events.append(event)
        events += [dict(event="test_stats", **totals), {"event": "test_done"}, {"event": "handler_stopped"}]
        (self.logs / "events.jsonl").write_text("".join(json.dumps(dict(e, seq=n)) + "\n" for n, e in enumerate(events, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", totals)
        DRIVER.write_json(self.logs / "runtime-facts.json", dict(flavor="emu", build_type="opt", debug_compiled="false", lock_checking="false"))
        DRIVER.write_json(self.evidence / "execution-environment-facts.json", {"platform": observation["platform"]} if "platform" in observation else {})
        return DRIVER.classify(self.args, self.image, self.logs, {}, selection, focus={"inventory": inventory})

    def test_exact_linux_case_is_accounted_without_scope_exclusion(self):
        result = self.classify()
        self.assertEqual("not_applicable", result["status"])
        self.assertEqual([RULE["id"]], result["accepted_skips"][0]["rules"])
        self.assertEqual([], result["scope_exclusions"])
        self.assertFalse(result["qualification_eligible"])

    def test_reason_and_case_do_not_cover_unrelated_skips(self):
        for changes in ({"case": "win_massive_other"}, {"case": "exit_status"},
                        {"reason": "Only on Windows"}, {"reason": "Only on Windows. extra"},
                        {"reason": "Other unavailable fixture"}):
            with self.subTest(changes=changes):
                result = self.classify(**changes)
                self.assertEqual("failed", result["status"])
                self.assertTrue(result["rejected_skips"])

    def test_missing_native_facts_remain_failed(self):
        for field in ("platform", "os_family", "os_name"):
            observation = linux_observation()
            del observation[field]
            with self.subTest(missing=field):
                self.assertEqual("failed", self.classify(observation=observation)["status"])

    def test_existing_darwin_rule_and_windows_behavior_are_preserved(self):
        darwin = self.classify(observation={"platform": "Darwin", "os_family": "unix", "os_name": "darwin"})
        self.assertEqual("not_applicable", darwin["status"])
        self.assertEqual([MAC_RULE["id"]], darwin["accepted_skips"][0]["rules"])
        windows = self.classify(observation={"platform": "Windows", "os_family": "win32", "os_name": "nt"})
        self.assertEqual("failed", windows["status"])
        self.assertTrue(windows["rejected_skips"])

    def test_failed_and_automatic_skipped_cases_remain_failed(self):
        for outcome in ("failed", "auto_skipped"):
            with self.subTest(outcome=outcome):
                self.assertEqual("failed", self.classify(outcome=outcome)["status"])

    def test_incomplete_planning_and_missing_terminal_remain_failed(self):
        self.assertEqual("failed", self.classify(planned=2)["status"])
        self.assertEqual("failed", self.classify(omit_terminal=True)["status"])


if __name__ == "__main__":
    unittest.main()
