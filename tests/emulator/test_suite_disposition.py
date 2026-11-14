"""Exercise audited N/A diagnostics without executing or changing a corpus."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


SOURCE_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_TMP = SOURCE_ROOT.parent / "tmp"
MODULE_SPEC = importlib.util.spec_from_file_location(
    "full_emulator_disposition", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(DRIVER)
OMISSION = "Audited fixture omission"
AUTO_PLAN = object()


def leaf(suite, case="test", outcome="passed", reason=OMISSION):
    event = {"event": "tc_done", "suite": suite, "case": case, "outcome": outcome}
    if outcome in ("skipped", "auto_skipped"):
        event.update(event="tc_user_skip" if outcome == "skipped" else "tc_auto_skip",
                     skipkind="user" if outcome == "skipped" else "auto", reasontext=reason)
    return event


class SuiteOutcomeTests(unittest.TestCase):
    def test_companion_pass_does_not_change_primary_not_applicable(self):
        skipped = leaf("a_SUITE", outcome="skipped")
        outcomes = DRIVER.suite_outcomes(
            [skipped, leaf("z_SUITE")], ["a_SUITE", "z_SUITE"], [{"event": skipped}], [])
        self.assertEqual("not_applicable", outcomes["a_SUITE"]["status"])
        self.assertEqual({"passed": 0, "failed": 0, "user_skipped": 1, "auto_skipped": 0},
                         outcomes["a_SUITE"]["result"])
        self.assertEqual("passed", outcomes["z_SUITE"]["status"])

    def test_scope_exclusions_are_not_passed_cases(self):
        skipped = leaf("excluded_SUITE", "excluded", "skipped")
        outcomes = DRIVER.suite_outcomes([skipped], ["excluded_SUITE"], [], [{"event": skipped}])
        self.assertEqual("not_applicable", outcomes["excluded_SUITE"]["status"])
        self.assertEqual(0, outcomes["excluded_SUITE"]["result"]["passed"])

    def test_empty_missing_failed_auto_and_unexplained_suites_are_failed(self):
        for events in ([], [leaf("other_SUITE")], [leaf("probe_SUITE", outcome="failed")],
                       [leaf("probe_SUITE", outcome="auto_skipped")],
                       [leaf("probe_SUITE", outcome="skipped")]):
            with self.subTest(events=events):
                self.assertEqual("failed", DRIVER.suite_outcomes(
                    events, ["probe_SUITE"], [], [])["probe_SUITE"]["status"])

    def test_callbacks_cannot_hide_failure_or_unaudited_omission(self):
        for outcome in ("failed", "auto_skipped", "skipped"):
            callback = dict(leaf("probe_SUITE", "init_per_suite", outcome), callback="init_per_suite")
            with self.subTest(outcome=outcome):
                outcomes = DRIVER.suite_outcomes([callback, leaf("probe_SUITE")], ["probe_SUITE"], [], [])
                self.assertEqual("failed", outcomes["probe_SUITE"]["status"])


class DispositionClassifierTests(unittest.TestCase):
    def setUp(self):
        WORKSPACE_TMP.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-disposition-unit-", dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        policy_dir = self.source / "tests/emulator"
        policy_dir.mkdir(parents=True)
        (self.source / "UPSTREAM_COMMIT").write_text("fixture-upstream\n")
        guard = self.source / "fixture-guard.txt"
        guard.write_text("exact fixture skip guard\n")
        scope = {"schema_version": 1, "directory": "../emulator_test", "exclusions": [{
            "id": "fixture-scope", "origin": "companion-spec", "directive": "skip_cases",
            "suite": "excluded_SUITE", "case": "excluded", "reason": OMISSION, "required": False}]}
        scope_file = policy_dir / "emulator_scope.json"
        DRIVER.write_json(scope_file, scope)
        rules = [{"id": "fixture-" + suite, "suite": suite, "reason_regex": "^" + OMISSION + "$",
                  "source": "fixture-guard.txt", "source_sha256": DRIVER.sha(guard),
                  "guard": "exact fixture skip guard", "condition": {"kind": "always"}}
                 for suite in ("a_SUITE", "z_SUITE", "probe_SUITE")]
        rules.append({"id": "fixture-scope", "class": "scope", "suite": "excluded_SUITE",
                      "case": "excluded", "reason_regex": "^" + OMISSION + "$",
                      "source": "tests/emulator/emulator_scope.json",
                      "source_sha256": DRIVER.sha(scope_file), "guard": '"id": "fixture-scope"',
                      "condition": {"kind": "always"}})
        DRIVER.write_json(policy_dir / "emulator_skip_policy.json", {
            "upstream_commit": "fixture-upstream", "default": "reject", "rules": rules})
        self.evidence = self.root / "tmp/beam-full-unit-disposition"
        self.image = self.evidence / "image"
        self.logs = self.evidence / "logs"
        (self.image / "specs").mkdir(parents=True)
        self.logs.mkdir()
        (self.image / "specs/focused.spec").write_text("{enable_builtin_hooks,false}.\n")
        self.args = SimpleNamespace(source_root=self.source, attempt=self.evidence,
                                    flavor="emu", build_type="opt")
        self.artifacts = {"candidate_sha256": "fixture-candidate"}

    def execution(self, events, primary="probe_SUITE", execution_suites=None, *,
                  full=False, planned_cases=AUTO_PLAN, controller_exit_code=0, exact_cases=None):
        execution_suites = execution_suites or [primary]
        selection = DRIVER.requested_selection(None if full else primary,
                                                cases=",".join(exact_cases) if exact_cases else None)
        counts = dict(passed=0, failed=0, user_skipped=0, auto_skipped=0)
        mapping = {"passed": "passed", "failed": "failed", "skipped": "user_skipped",
                   "auto_skipped": "auto_skipped"}
        for event in events:
            if not event.get("callback") and event.get("outcome") in mapping:
                counts[mapping[event["outcome"]]] += 1
        journal = [{"event": "handler_started"}, {"event": "start_info",
                   "suites": len(execution_suites),
                   "cases": sum(counts.values()) if planned_cases is AUTO_PLAN else planned_cases}]
        journal += events + [dict(event="test_stats", **counts),
                             {"event": "test_done"}, {"event": "handler_stopped"}]
        (self.logs / "events.jsonl").write_text("".join(
            json.dumps(dict(event, seq=number)) + "\n" for number, event in enumerate(journal, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", counts)
        DRIVER.write_json(self.logs / "runtime-facts.json", {
            "flavor": "emu", "build_type": "opt", "debug_compiled": "false",
            "lock_checking": "false", "architecture": "aarch64-apple-darwin", "perf": "false"})
        shutil.copy2(self.source / "tests/emulator/emulator_skip_policy.json",
                     self.evidence / "execution-skip-policy.json")
        metadata = {"selection": selection, "execution_suites": execution_suites,
                    "coverage_mode": "ct_planned" if exact_cases is None else "exact_cases",
                    "expected_cases": [{"module": primary, "case": case} for case in exact_cases or []],
                    "expected_groups": [],
                    "expected_group_paths": [], "spec": "specs/focused.spec"}
        DRIVER.write_json(self.logs / "focus-inventory.json", metadata)
        focus = None if full else DRIVER.focus_evidence(self.image, self.logs)
        corpus = {"suites": execution_suites, "no_opt": [], "stripped_types": []}
        identity = {"fingerprint": "fixture-fingerprint"}
        DRIVER.write_json(self.image / "prepared-receipt.json", dict(corpus, identity=identity))
        DRIVER.write_json(self.evidence / "invocation.json", {
            "identity": identity, "artifacts": self.artifacts, "selection": selection, "focus": focus})
        DRIVER.write_json(self.evidence / "cleanup.json", {"remaining": []})
        DRIVER.write_json(self.evidence / "result.json", {"controller_exit_code": controller_exit_code})
        summary = DRIVER.classify(self.args, self.image, self.logs, corpus, selection, focus=focus)
        return summary

    def audit(self):
        with mock.patch.object(DRIVER, "verify_execution_image", return_value=self.artifacts), \
                contextlib.redirect_stdout(io.StringIO()):
            return DRIVER.audit(self.args)

    def test_audited_all_skip_is_not_applicable_and_reaudits_as_exit_77(self):
        summary = self.execution([leaf("probe_SUITE", outcome="skipped")])
        self.assertEqual("not_applicable", summary["status"])
        self.assertEqual("diagnostic", summary["purpose"])
        self.assertEqual("not_applicable", summary["suite_results"]["probe_SUITE"]["status"])
        self.assertEqual(77, self.audit())
        report = next((self.evidence / "audits").glob("*/audit.json"))
        self.assertEqual("not_applicable", DRIVER.read_json(report)["status"])

    def test_fully_excluded_suite_is_not_reported_as_passed(self):
        summary = self.execution([leaf("excluded_SUITE", "excluded", "skipped")], "excluded_SUITE")
        self.assertEqual("not_applicable", summary["status"])
        self.assertEqual([], summary["accepted_skips"])
        self.assertEqual(1, len(summary["scope_exclusions"]))
        self.assertEqual(0, summary["suite_results"]["excluded_SUITE"]["result"]["passed"])

    def test_companion_pass_cannot_turn_zero_pass_primary_green(self):
        summary = self.execution([leaf("a_SUITE", outcome="skipped"), leaf("z_SUITE")],
                                 "a_SUITE", ["a_SUITE", "z_SUITE"])
        self.assertEqual("not_applicable", summary["status"])
        self.assertEqual("not_applicable", summary["suite_results"]["a_SUITE"]["status"])
        self.assertEqual("passed", summary["suite_results"]["z_SUITE"]["status"])
        self.assertEqual(77, self.audit())

    def test_primary_pass_with_audited_companion_omission_is_passed(self):
        summary = self.execution([leaf("a_SUITE"), leaf("z_SUITE", outcome="skipped")],
                                 "a_SUITE", ["a_SUITE", "z_SUITE"])
        self.assertEqual("passed", summary["status"])
        self.assertEqual("not_applicable", summary["suite_results"]["z_SUITE"]["status"])

    def test_empty_missing_failed_auto_and_unexplained_targets_are_failed(self):
        cases = [[], [leaf("probe_SUITE", outcome="failed")],
                 [leaf("probe_SUITE", outcome="auto_skipped")],
                 [leaf("probe_SUITE", outcome="skipped", reason="Unexplained omission")]]
        for events in cases:
            with self.subTest(events=events):
                self.assertEqual("failed", self.execution(events)["status"])

    def test_missing_or_failed_companion_is_failed(self):
        for events in ([leaf("a_SUITE")],
                       [leaf("a_SUITE"), leaf("z_SUITE", outcome="failed")],
                       [leaf("a_SUITE"), leaf("z_SUITE", outcome="skipped", reason="Unexplained")]):
            with self.subTest(events=events):
                summary = self.execution(events, "a_SUITE", ["a_SUITE", "z_SUITE"])
                self.assertEqual("failed", summary["status"])

    def test_reversed_lifecycle_is_failed_despite_both_suites_passing(self):
        summary = self.execution([leaf("z_SUITE"), leaf("a_SUITE")],
                                 "z_SUITE", ["a_SUITE", "z_SUITE"])
        self.assertEqual("failed", summary["status"])
        self.assertTrue(any(isinstance(problem, dict) and "lifecycle_suite_order" in problem
                            for problem in summary["problems"]))

    def test_not_applicable_cannot_hide_planned_case_mismatch(self):
        summary = self.execution([leaf("probe_SUITE", outcome="skipped")], planned_cases=2)
        self.assertEqual("failed", summary["status"])

    def test_unknown_planned_count_cannot_establish_whole_suite_not_applicable(self):
        for planned in (None, "unknown", True, 1.0):
            with self.subTest(planned=planned):
                summary = self.execution([leaf("probe_SUITE", outcome="skipped")], planned_cases=planned)
                self.assertEqual("failed", summary["status"])
                self.assertIn("Not-applicable diagnostic lacks complete planned or exact case coverage",
                              summary["problems"])

    def test_exact_case_closure_can_establish_not_applicable_with_unknown_planned_count(self):
        summary = self.execution([leaf("probe_SUITE", outcome="skipped")],
                                 planned_cases="unknown", exact_cases=["test"])
        self.assertEqual("not_applicable", summary["status"])
        self.assertEqual(77, self.audit())

    def test_nonzero_controller_cannot_be_reaudited_as_not_applicable(self):
        self.execution([leaf("probe_SUITE", outcome="skipped")], controller_exit_code=1)
        with self.assertRaisesRegex(RuntimeError, "remains nonpassing"):
            self.audit()

    def test_unexplained_omission_cannot_be_reaudited_as_not_applicable(self):
        self.execution([leaf("probe_SUITE", outcome="skipped", reason="Unexplained")])
        with self.assertRaisesRegex(RuntimeError, "remains nonpassing"):
            self.audit()

    def test_full_gate_still_rejects_zero_passes_even_with_audited_skips(self):
        summary = self.execution([{"event": "tc_start", "suite": "a_SUITE", "case": "test"},
                                  leaf("a_SUITE", outcome="skipped"),
                                  {"event": "tc_start", "suite": "z_SUITE", "case": "test"},
                                  leaf("z_SUITE", outcome="skipped")],
                                 execution_suites=["a_SUITE", "z_SUITE"], full=True)
        self.assertEqual("failed", summary["status"])
        self.assertEqual("complete-campaign", summary["purpose"])
        self.assertNotIn("suite_results", summary)
        self.assertEqual(["Nonpassing totals or unaudited skips"], summary["problems"])
        with self.assertRaisesRegex(RuntimeError, "remains nonpassing"):
            self.audit()

    def input_fixture(self):
        """Supply real fingerprint inputs; none of these placeholder bytes execute."""
        self.args.binary_root = self.root / "build"
        self.args.full_root = self.args.binary_root / "tests/emulator/full"
        self.args.native_root = self.args.full_root / "native"
        self.args.runtime_bin = self.args.binary_root / "bin"
        self.args.support_files = self.args.full_root / "support-files.txt"
        self.args.native_manifest = self.args.native_root / "native-manifest.json"
        self.args.process_tool = self.args.native_root / "tools/emulator_process"
        for relative in ("emulator/full_emulator.py", "emulator/beam_emulator_harness.erl",
                         "emulator/beam_emulator_full_prepare.erl", "emulator/beam_emulator_full_run.erl",
                         "emulator/beam_emulator_focus.erl", "cmake/RunEmulator.py.in",
                         "emulator/beam_emulator_events.erl", "CMakeLists.txt",
                         "cmake/FullEmulatorTests.cmake", "cmake/FullEmulatorNative.cmake",
                         "emulator/emulator_process.c", "emulator/support/lib/fixture.hrl"):
            path = self.source / "tests" / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture input\n")
        self.args.full_root.mkdir(parents=True, exist_ok=True)
        self.args.support_files.write_text("lib/fixture.hrl\n")
        self.args.native_root.mkdir(parents=True)
        DRIVER.write_json(self.args.native_manifest, {
            "artifacts": [], "toolchain": {"CMAKE_C_COMPILER": sys.executable}})
        variables = self.args.native_root / "compiler_config/variables"
        variables.parent.mkdir()
        variables.write_text("{'CC', \"" + sys.executable + "\"}.\n")
        for path in (self.args.runtime_bin / "beam", self.args.runtime_bin / "erl_child_setup",
                     self.args.process_tool):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture native input\n")
        self.corpus_input = self.source / "erts/emulator/test/fixture.erl"
        self.corpus_input.parent.mkdir(parents=True)
        self.corpus_input.write_text("original fixture source\n")
        return DRIVER.input_files(self.args)

    def test_complete_execution_input_identity_accepts_unchanged_fixture(self):
        identity = self.input_fixture()
        self.assertEqual(identity, DRIVER.validate_execution_inputs(self.args, identity))

    def test_input_drift_during_classification_rejects_commit_without_restoring_bytes(self):
        identity = self.input_fixture()
        self.args.suite = self.args.group = self.args.cases = None
        self.args.timeout = 1
        runtime = self.args.full_root / "runtime"
        for relative in ("support", "tools", "bin", "erts-17.0/bin"):
            (runtime / relative).mkdir(parents=True, exist_ok=True)
        corpus = {"suites": ["probe_SUITE"], "no_opt": [], "stripped_types": []}
        DRIVER.write_json(runtime / "support/corpus-selection.json", corpus)
        (runtime / "tools/emulator_process").write_text("fixture process tool\n")
        outputs = {str(path.relative_to(runtime)): DRIVER.sha(path)
                   for path in runtime.rglob("*") if path.is_file()}
        DRIVER.write_json(runtime / "prepared-receipt.json", dict(corpus, identity=identity, outputs=outputs))
        controller = SimpleNamespace(returncode=0, poll=lambda: 0, wait=lambda timeout: 0)
        epmd = SimpleNamespace(pid=123456, wait=lambda timeout: 0)

        def classify_and_change_input(args, image, logdir, _corpus, selection, focus=None):
            summary = {"status": "passed", "problems": [], "selection": selection,
                       "result": {"passed": 1, "failed": 0, "user_skipped": 0, "auto_skipped": 0}}
            DRIVER.write_json(logdir / "audit.json", summary)
            self.corpus_input.write_text("changed fixture source\n")
            return summary

        def completed_controller(_command, *, cwd, **_options):
            DRIVER.write_json(Path(cwd).parent / "logs/ct-result.json", {
                "passed": 1, "failed": 0, "user_skipped": 0, "auto_skipped": 0})
            return controller

        with mock.patch.object(DRIVER, "stage_candidate", return_value=self.args.runtime_bin / "beam"), \
                mock.patch.object(DRIVER, "verify_execution_image", return_value={}), \
                mock.patch.object(DRIVER, "start_epmd", return_value=(epmd, {"pid": epmd.pid}, 12345)), \
                mock.patch.object(DRIVER.subprocess, "run", return_value=SimpleNamespace(stdout="CET\n")), \
                mock.patch.object(DRIVER.subprocess, "Popen", side_effect=completed_controller), \
                mock.patch.object(DRIVER, "classify", side_effect=classify_and_change_input), \
                mock.patch.object(DRIVER, "cleanup", return_value=[]) as cleanup, \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "Execution inputs changed.*beam_emulator_full_test_support"):
                DRIVER.run(self.args)
            cleanup.assert_called_once()
        evidence = Path((self.args.full_root / "latest-run.txt").read_text().strip())
        self.assertEqual("changed fixture source\n", self.corpus_input.read_text())
        for relative in ("logs/audit.json", "result.json", "failure.json"):
            self.assertEqual("failed", DRIVER.read_json(evidence / relative)["status"])
        self.assertEqual([], DRIVER.read_json(evidence / "cleanup.json")["remaining"])


if __name__ == "__main__":
    unittest.main()
