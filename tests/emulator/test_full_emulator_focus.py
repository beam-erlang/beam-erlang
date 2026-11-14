"""Bounded regressions for focused execution accounting and receipt integrity.

Run with Python's unittest runner; these tests never launch a VM or edit corpus
inputs. Temporary evidence belongs to the workspace's disposable tmp directory.
"""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


SOURCE_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_TMP = SOURCE_ROOT.parent / "tmp"
MODULE_SPEC = importlib.util.spec_from_file_location(
    "full_emulator", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(DRIVER)


def leaf(case, suite="probe_SUITE", kind="tc_done", outcome="passed", **extra):
    return dict(event=kind, suite=suite, case=case, outcome=outcome, **extra)


def callback(group, name, outcome="passed", **extra):
    return leaf(name, callback=name, group=group, outcome=outcome, **extra)


def inventory(selection, cases, groups=(), paths=(), mode="exact_cases"):
    return {"selection": copy.deepcopy(selection), "coverage_mode": mode,
            "expected_cases": [{"module": "probe_SUITE", "case": case}
                               for case in cases],
            "expected_groups": list(groups),
            "expected_group_paths": [list(path) for path in paths],
            "spec": "specs/focused.spec"}


class FocusedCoverageTests(unittest.TestCase):
    def setUp(self):
        self.selection = DRIVER.requested_selection("probe_SUITE", "outer", "nested")
        self.inventory = inventory(self.selection, ["nested"], ["outer", "inner"],
                                   [["outer"], ["outer", "inner"]])
        self.events = [callback("outer", "init_per_group"),
                       callback("inner", "init_per_group"), leaf("nested"),
                       callback("inner", "end_per_group"),
                       callback("outer", "end_per_group")]

    def problems(self, events=None, metadata=None):
        return DRIVER.focused_coverage(
            self.events if events is None else events, self.selection,
            {"inventory": self.inventory if metadata is None else metadata})

    def test_nested_group_callbacks_and_exact_leaf_are_required(self):
        self.assertEqual([], self.problems())
        events = [event for event in self.events
                  if not (event.get("group") == "inner"
                          and event.get("callback") == "end_per_group")]
        self.assertIn({"incomplete_group_callbacks": "inner"}, self.problems(events))

    def test_missing_and_extra_cases_cannot_cancel_each_other(self):
        events = [dict(event, case="sibling") if event.get("case") == "nested"
                  else event for event in self.events]
        self.assertIn({"missing_cases": [("probe_SUITE", "nested")],
                       "unexpected_cases": [("probe_SUITE", "sibling")]},
                      self.problems(events))

    def test_another_suite_is_rejected_even_when_requested_case_ran(self):
        problems = self.problems(self.events + [leaf("extra", suite="other_SUITE")])
        self.assertIn({"expected_suite": "probe_SUITE",
                       "observed_suites": ["other_SUITE", "probe_SUITE"]}, problems)

    def test_inventory_cannot_change_the_requested_selection(self):
        changed = copy.deepcopy(self.inventory)
        changed["selection"]["cases"] = ["sibling"]
        self.assertIn("Focused inventory differs from the requested selection",
                      self.problems(metadata=changed))

    def test_user_skipped_group_does_not_require_teardown(self):
        metadata = inventory(self.selection, ["nested"], ["outer"], [["outer"]])
        events = [callback("outer", "init_per_group", "skipped", skipkind="user"),
                  leaf("nested", kind="tc_user_skip", outcome="skipped", skipkind="user")]
        self.assertEqual([], self.problems(events, metadata))

    def test_ancestor_user_skip_accounts_for_unentered_descendant(self):
        events = [callback("outer", "init_per_group", "skipped", skipkind="user"),
                  leaf("nested", kind="tc_user_skip", outcome="skipped", skipkind="user")]
        self.assertEqual([], self.problems(events))

    def test_ancestor_skip_does_not_hide_unrelated_missing_group(self):
        metadata = copy.deepcopy(self.inventory)
        metadata["expected_groups"].append("unrelated")
        metadata["expected_group_paths"].append(["unrelated"])
        events = [callback("outer", "init_per_group", "skipped", skipkind="user"),
                  leaf("nested", kind="tc_user_skip", outcome="skipped", skipkind="user")]
        self.assertIn({"incomplete_group_callbacks": "unrelated"},
                      self.problems(events, metadata))

    def test_unknown_coverage_mode_cannot_disable_case_accounting(self):
        metadata = copy.deepcopy(self.inventory)
        metadata["coverage_mode"] = "unknown"
        self.assertTrue(self.problems(metadata=metadata))


class InvocationSelectionTests(unittest.TestCase):
    def test_structured_selection_does_not_depend_on_command_tail(self):
        selection = DRIVER.requested_selection("probe_SUITE", "outer", "nested")
        self.assertEqual(selection, DRIVER.invocation_selection(
            {"selection": selection, "command": ["command-format-may-change"]}))

    def test_legacy_case_selection_remains_explicitly_historical(self):
        selection = DRIVER.invocation_selection({"command": [
            "erl", "-extra", "/image", "emu", "opt", "/logs",
            "probe_SUITE", "one,two"]})
        self.assertEqual({"mode": "focused", "suite": "probe_SUITE",
                          "group": "", "cases": ["one", "two"], "legacy": True},
                         selection)

    def test_legacy_full_campaign_is_not_recovered_as_a_suite(self):
        selection = DRIVER.invocation_selection({"command": [
            "erl", "-extra", "/image", "emu", "opt", "/logs", "full", ""]})
        self.assertEqual("full", selection["mode"])
        self.assertIsNone(selection["suite"])
        self.assertTrue(selection["legacy"])

    def test_unrecognized_legacy_argument_shape_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Unrecognized legacy"):
            DRIVER.invocation_selection({"command": ["erl", "-extra", "partial"]})


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-full-focus-unit-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.evidence = Path(self.temporary.name)
        self.image = self.evidence / "image"
        self.logs = self.evidence / "logs"
        (self.image / "specs").mkdir(parents=True)
        self.logs.mkdir()
        self.selection = DRIVER.requested_selection("probe_SUITE", cases="one")
        self.metadata = inventory(self.selection, ["one"])
        (self.image / "specs/focused.spec").write_text("{enable_builtin_hooks,false}.\n")
        DRIVER.write_json(self.logs / "focus-inventory.json", self.metadata)
        self.focus = DRIVER.focus_evidence(self.image, self.logs)
        self.identity = {"fingerprint": "test-fingerprint"}
        self.artifacts = {"candidate_sha256": "test-candidate"}
        DRIVER.write_json(self.image / "prepared-receipt.json", {"identity": self.identity})
        DRIVER.write_json(self.evidence / "cleanup.json", {"remaining": []})
        DRIVER.write_json(self.evidence / "invocation.json", {
            "identity": self.identity, "artifacts": self.artifacts,
            "selection": self.selection, "focus": self.focus})
        self.args = SimpleNamespace(source_root=SOURCE_ROOT, attempt=self.evidence,
                                    build_type="opt", flavor="emu")

    def assert_audit_rejects_tamper(self):
        with mock.patch.object(DRIVER, "verify_execution_image", return_value=self.artifacts), \
                mock.patch.object(DRIVER, "classify") as classify:
            with self.assertRaisesRegex(RuntimeError, "differs from execution evidence"):
                DRIVER.audit(self.args)
            classify.assert_not_called()

    def test_modified_focused_specification_is_rejected_before_classification(self):
        (self.image / "specs/focused.spec").write_text("{suites,\"../emulator_test\",all}.\n")
        self.assert_audit_rejects_tamper()

    def test_modified_expected_case_inventory_is_rejected_before_classification(self):
        self.metadata["expected_cases"][0]["case"] = "different"
        DRIVER.write_json(self.logs / "focus-inventory.json", self.metadata)
        self.assert_audit_rejects_tamper()

    def test_inventory_specification_cannot_escape_its_execution_image(self):
        self.metadata["spec"] = "../outside.spec"
        (self.evidence / "outside.spec").write_text("outside\n")
        DRIVER.write_json(self.logs / "focus-inventory.json", self.metadata)
        with self.assertRaisesRegex(RuntimeError, "escapes its owned root"):
            DRIVER.focus_evidence(self.image, self.logs)

    def classify(self, *, duplicate_leaf=False, legacy=False):
        result = {"passed": 1, "failed": 0, "user_skipped": 0, "auto_skipped": 0}
        events = [{"event": "handler_started"},
                  {"event": "start_info", "suites": 1, "cases": 1},
                  leaf("one", kind="tc_start"), leaf("one")]
        if duplicate_leaf:
            events.append(leaf("one"))
        events += [dict(event="test_stats", **result),
                   {"event": "test_done"}, {"event": "handler_stopped"}]
        (self.logs / "events.jsonl").write_text("".join(
            json.dumps(dict(event, seq=number)) + "\n"
            for number, event in enumerate(events, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", result)
        DRIVER.write_json(self.logs / "runtime-facts.json", {
            "flavor": "emu", "build_type": "opt", "debug_compiled": "false",
            "lock_checking": "false", "architecture": "aarch64-apple-darwin",
            "perf": "false"})
        (self.evidence / "execution-skip-policy.json").write_bytes(
            (SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json").read_bytes())
        selection = dict(self.selection, legacy=True) if legacy else self.selection
        return DRIVER.classify(self.args, self.image, self.logs, {}, selection,
                               focus=None if legacy else self.focus)

    def test_focused_pass_remains_diagnostic(self):
        summary = self.classify()
        self.assertEqual("passed", summary["status"])
        self.assertEqual("diagnostic", summary["purpose"])
        self.assertFalse(summary["historical"])

    def test_legacy_diagnostic_is_not_upgraded_to_current_acceptance(self):
        summary = self.classify(legacy=True)
        self.assertEqual("passed", summary["status"])
        self.assertEqual("diagnostic", summary["purpose"])
        self.assertTrue(summary["historical"])

    def test_duplicate_terminal_events_still_fail_total_accounting(self):
        summary = self.classify(duplicate_leaf=True)
        self.assertEqual("failed", summary["status"])
        self.assertTrue(any(isinstance(problem, dict) and "leaf_event_counts" in problem
                            for problem in summary["problems"]))


if __name__ == "__main__":
    unittest.main()
