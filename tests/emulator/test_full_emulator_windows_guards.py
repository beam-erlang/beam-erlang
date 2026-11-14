"""Native guard policy regressions; never launch the retained test corpus."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


SOURCE_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "windows_guard_driver", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)
POLICY = json.loads((SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json").read_text())
WINDOWS_RULES = [rule for rule in POLICY["rules"] if rule["id"].startswith("windows-")
                 and rule["id"] != "windows-massive-port-case-on-macos"]


def native_facts():
    return dict(platform="Windows", os_family="win32", os_name="nt", wordsize=8,
                flavor="emu", build_type="opt", debug_compiled="false",
                lock_checking="false", timezone_probe="native_win32_tzutil",
                timezone_probe_succeeded="true", timezone_id="Pacific Standard Time",
                stockholm_timezone="false", known_proc_files=[],
                mseg_alloc_probe="ok", mseg_alloc="false")


class NativeGuardPolicyTests(unittest.TestCase):
    def test_every_windows_guard_binds_unchanged_pinned_source(self):
        for rule in WINDOWS_RULES + POLICY["empty_suite_rules"]:
            with self.subTest(rule=rule["id"]):
                data = (SOURCE_ROOT / rule["source"]).read_bytes().replace(b"\r\n", b"\n")
                self.assertEqual(rule["source_sha256"], hashlib.sha256(data).hexdigest())
                self.assertIn(rule["guard"], data.decode("utf-8"))

    def test_all_native_windows_guards_reject_absent_family_and_other_platform(self):
        for rule in WINDOWS_RULES + POLICY["empty_suite_rules"]:
            with self.subTest(rule=rule["id"]):
                facts = native_facts()
                self.assertTrue(DRIVER.condition(rule, facts, {}))
                del facts["os_family"]
                self.assertFalse(DRIVER.condition(rule, facts, {}))
                facts = dict(native_facts(), platform="Linux", os_family="unix")
                self.assertFalse(DRIVER.condition(rule, facts, {}))

    def test_timezone_skip_requires_a_successful_matching_native_probe(self):
        rules = [rule for rule in WINDOWS_RULES if rule["id"].startswith("windows-time-")]
        self.assertEqual(2, len(rules))
        for rule in rules:
            for changes in ({"timezone_probe": "unix_date"},
                            {"timezone_probe_succeeded": "false"},
                            {"stockholm_timezone": "true"}):
                with self.subTest(rule=rule["id"], changes=changes):
                    self.assertFalse(DRIVER.condition(rule, dict(native_facts(), **changes), {}))
            for field in ("timezone_probe", "timezone_probe_succeeded", "stockholm_timezone"):
                facts = native_facts()
                del facts[field]
                self.assertFalse(DRIVER.condition(rule, facts, {}))

    def test_win64_api_guard_does_not_cover_other_word_sizes(self):
        rule = next(rule for rule in WINDOWS_RULES if rule["id"] == "windows-nif-api-2-0")
        self.assertFalse(DRIVER.condition(rule, dict(native_facts(), wordsize=4), {}))

    def test_proc_file_guard_requires_candidate_absence_measurement(self):
        rule = next(rule for rule in WINDOWS_RULES if rule["id"] == "windows-no-linux-proc-files")
        facts = native_facts()
        del facts["known_proc_files"]
        self.assertFalse(DRIVER.condition(rule, facts, {}))
        self.assertFalse(DRIVER.condition(rule, facts, {
            "guard_facts": {"known_proc_files": ["/proc/meminfo"]}}))
        self.assertTrue(DRIVER.condition(rule, facts, {"guard_facts": {"known_proc_files": []}}))


class GuardAccountingEvidence(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(
            prefix="windows-guard-test-", dir=SOURCE_ROOT.parent / "tmp")
        self.addCleanup(self.scratch.cleanup)
        self.evidence = Path(self.scratch.name)
        self.logs = self.evidence / "logs"
        self.logs.mkdir()
        self.image = self.evidence / "image"
        self.image.mkdir()
        self.args = SimpleNamespace(source_root=SOURCE_ROOT, flavor="emu", build_type="opt")
        (self.evidence / "execution-skip-policy.json").write_bytes(
            (SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json").read_bytes())


class NativeTimezoneAccountingTests(GuardAccountingEvidence):
    def classify(self, case="univ_to_local", *, guard_facts=None, reason=None):
        result = dict(passed=0, failed=0, user_skipped=1, auto_skipped=0)
        selection = DRIVER.requested_selection("time_SUITE", None, case)
        metadata = dict(selection=copy.deepcopy(selection), coverage_mode="exact_cases",
                        execution_suites=["time_SUITE"], empty_suites=[],
                        expected_cases=[{"module": "time_SUITE", "case": case}],
                        expected_groups=[], expected_group_paths=[], applied_exclusions=[])
        event = dict(event="tc_done", suite="time_SUITE", case=case,
                     outcome="skipped", skipkind="user",
                     reasontext=reason or "This test is only valid for Stockholm timezone",
                     guard_facts=native_facts() if guard_facts is None else guard_facts)
        events = [{"event": "handler_started"},
                  {"event": "start_info", "suites": 1, "cases": 1}, event,
                  dict(event="test_stats", **result),
                  {"event": "test_done"}, {"event": "handler_stopped"}]
        (self.logs / "events.jsonl").write_text("".join(
            json.dumps(dict(value, seq=number)) + "\n"
            for number, value in enumerate(events, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", result)
        # Keep timezone evidence at event time; startup facts cannot fill a
        # missing native observation on the guarded testcase.
        runtime = native_facts()
        for key in ("timezone_probe", "timezone_probe_succeeded", "stockholm_timezone"):
            runtime.pop(key)
        DRIVER.write_json(self.logs / "runtime-facts.json", runtime)
        DRIVER.write_json(self.evidence / "execution-environment-facts.json", {"platform": "Windows"})
        return DRIVER.classify(self.args, self.image, self.logs, {}, selection,
                               focus={"inventory": metadata})

    def test_only_two_exact_timezone_cases_accept_native_guard(self):
        for case in ("univ_to_local", "local_to_univ"):
            with self.subTest(case=case):
                summary = self.classify(case)
                self.assertEqual("not_applicable", summary["status"])
                self.assertEqual(1, len(summary["accepted_skips"]))
        self.assertEqual("failed", self.classify("bad_univ_to_local")["status"])
        self.assertEqual("failed", self.classify(reason="Other timezone skip")["status"])

    def test_probe_failure_and_missing_event_evidence_remain_failed(self):
        self.assertEqual("failed", self.classify(guard_facts={})["status"])
        self.assertEqual("failed", self.classify(guard_facts=dict(
            native_facts(), timezone_probe_succeeded="false"))["status"])


class EmptyNativeSuiteAccountingTests(GuardAccountingEvidence):
    def empty_classify(self, *, suite="os_signal_SUITE", omit=None, planned=None,
                       facts=None, group=None, cases=None, extra_event=None,
                       include_stats=False):
        result = dict(passed=0, failed=0, user_skipped=0, auto_skipped=0)
        selection = DRIVER.requested_selection(suite, group, cases)
        metadata = dict(selection=copy.deepcopy(selection), coverage_mode="ct_planned",
                        execution_suites=[suite], empty_suites=[suite], expected_cases=[],
                        expected_groups=[], expected_group_paths=[], applied_exclusions=[])
        # This is the measured pinned Common Test shape: no suite events and
        # no test_stats when a sole upstream all/0 returns [].
        events = [{"event": "handler_started"}, {"event": "test_start"},
                  dict(event="start_info", **(planned or dict(tests=1, suites=0, cases=0))),
                  {"event": "test_done"}, {"event": "handler_stopped"}]
        if omit:
            events = [event for event in events if event["event"] != omit]
        if extra_event:
            events.insert(-2, extra_event)
        if include_stats:
            events.insert(-2, dict(event="test_stats", **result))
        (self.logs / "events.jsonl").write_text("".join(
            json.dumps(dict(value, seq=number)) + "\n"
            for number, value in enumerate(events, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", result)
        DRIVER.write_json(self.logs / "runtime-facts.json", facts if facts is not None
                          else dict(native_facts(), suite_all_empty=[suite]))
        DRIVER.write_json(self.evidence / "execution-environment-facts.json", {})
        return DRIVER.classify(self.args, self.image, self.logs, {}, selection,
                               focus={"inventory": metadata})

    def test_native_zero_case_shape_is_audited_not_applicable(self):
        summary = self.empty_classify()
        self.assertEqual("not_applicable", summary["status"])
        self.assertEqual("windows-os-signal-empty",
                         summary["audited_empty_suites"]["os_signal_SUITE"]["rule"])
        self.assertEqual([], summary["accepted_skips"])
        self.assertEqual([], summary["problems"])

    def test_empty_guard_still_requires_every_run_boundary(self):
        for omitted in ("handler_started", "start_info", "test_done", "handler_stopped"):
            with self.subTest(omitted=omitted):
                self.assertEqual("failed", self.empty_classify(omit=omitted)["status"])

    def test_empty_guard_rejects_changed_planning_or_suite_events(self):
        for planned in (dict(tests=2, suites=0, cases=0),
                        dict(tests=1, suites=1, cases=0),
                        dict(tests=1, suites=0, cases=1)):
            with self.subTest(planned=planned):
                self.assertEqual("failed", self.empty_classify(planned=planned)["status"])
        for include_stats in (False, True):
            with self.subTest(include_stats=include_stats), self.assertRaisesRegex(
                    RuntimeError, "disagrees with observed testcases"):
                self.empty_classify(include_stats=include_stats, extra_event=dict(
                    event="tc_done", suite="os_signal_SUITE", callback="init_per_suite",
                    case="init_per_suite", outcome="passed"))

    def test_unknown_empty_suite_cannot_qualify(self):
        with self.assertRaisesRegex(RuntimeError, "exact audited upstream guard"):
            self.empty_classify(suite="unknown_empty_SUITE")

    def test_native_empty_observation_and_family_are_required(self):
        for missing in ("suite_all_empty", "os_family", "platform"):
            facts = dict(native_facts(), suite_all_empty=["os_signal_SUITE"])
            del facts[missing]
            with self.subTest(missing=missing), self.assertRaises(RuntimeError):
                self.empty_classify(facts=facts)

    def test_empty_guard_never_accepts_partial_selections(self):
        for partial in (dict(group="group"), dict(cases="case")):
            with self.subTest(partial=partial), self.assertRaisesRegex(RuntimeError, "partial selection"):
                self.empty_classify(**partial)


if __name__ == "__main__":
    unittest.main()
