"""Standalone audit integrity regressions; never launch a VM."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


SOURCE_ROOT = Path(os.environ.get("BEAM_AUDIT_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
SCRATCH = Path(os.environ.get("BEAM_AUDIT_TMP", SOURCE_ROOT.parent / "tmp"))
DRIVER_PATH = Path(os.environ.get("BEAM_AUDIT_DRIVER", Path(__file__).with_name("full_emulator.py")))
SPEC = importlib.util.spec_from_file_location("audit_integrity_driver", DRIVER_PATH)
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class AuditEvidence(unittest.TestCase):
    def setUp(self):
        SCRATCH.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="audit-integrity-", dir=SCRATCH)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "product"
        self.evidence = self.root / "tmp/beam-full-audit-test"
        self.image = self.evidence / "image"
        self.logs = self.evidence / "logs"
        (self.image / "specs").mkdir(parents=True)
        self.logs.mkdir()
        self.pin = "1" * 40
        self.source.mkdir()
        (self.source / "UPSTREAM_COMMIT").write_text(self.pin + "\n")
        self.guard = self.source / "erts/emulator/test/probe_SUITE.erl"
        self.guard.parent.mkdir(parents=True)
        self.guard.write_bytes(b"all() -> [one].\r\none(_) -> {skip, guarded}.\r\n")
        self.rule = dict(id="guard-one", suite="probe_SUITE", case="one",
                         source="erts/emulator/test/probe_SUITE.erl",
                         source_sha256=hashlib.sha256(self.guard.read_bytes().replace(b"\r\n", b"\n")).hexdigest(),
                         guard="one(_) -> {skip, guarded}.", reason_regex="guarded",
                         condition={"kind": "always"})
        self.policy_path = self.source / "tests/emulator/emulator_skip_policy.json"
        self.policy_path.parent.mkdir(parents=True)
        self.policy = dict(upstream_commit=self.pin, rules=[self.rule], empty_suite_rules=[])
        DRIVER.write_json(self.policy_path, self.policy)
        self.scope_path = self.source / "tests/emulator/emulator_scope.json"
        DRIVER.write_json(self.scope_path, {"exclusions": []})
        self.identity = dict(fingerprint="a" * 64, upstream_commit=self.pin, inputs={
            str(self.guard.resolve()): DRIVER.sha(self.guard),
            str(self.policy_path.resolve()): DRIVER.sha(self.policy_path)})
        self.current_identity = copy.deepcopy(self.identity)
        self.artifacts = {"candidate_sha256": "fixture-candidate"}
        self.selection = DRIVER.requested_selection("probe_SUITE", cases="one")
        inventory = dict(selection=copy.deepcopy(self.selection), coverage_mode="exact_cases",
                         execution_suites=["probe_SUITE"], expected_cases=[{"module": "probe_SUITE", "case": "one"}],
                         expected_groups=[], expected_group_paths=[], empty_suites=[],
                         spec="specs/focused.spec")
        (self.image / inventory["spec"]).write_text("{enable_builtin_hooks,false}.\n")
        DRIVER.write_json(self.logs / "focus-inventory.json", inventory)
        self.focus = DRIVER.focus_evidence(self.image, self.logs)
        self.args = SimpleNamespace(source_root=self.source, attempt=self.evidence,
                                    flavor="emu", build_type="opt")
        self.freeze_identity()
        DRIVER.write_json(self.evidence / "cleanup.json", {"remaining": [], "complete": True})
        DRIVER.write_json(self.evidence / "result.json", {"status": "passed", "controller_exit_code": 0})
        self.write_case()

    def freeze_identity(self):
        DRIVER.write_json(self.image / "prepared-receipt.json", {"identity": self.identity})
        DRIVER.write_json(self.evidence / "invocation.json", {
            "identity": self.identity, "artifacts": self.artifacts,
            "selection": self.selection, "focus": self.focus})
        (self.evidence / "execution-skip-policy.json").write_bytes(self.policy_path.read_bytes())

    def write_case(self, outcome="skipped"):
        result = dict(passed=int(outcome == "passed"), failed=int(outcome == "failed"),
                      user_skipped=int(outcome == "skipped"), auto_skipped=0)
        event = dict(event="tc_done", suite="probe_SUITE", case="one", outcome=outcome)
        if outcome == "skipped":
            event.update(skipkind="user", reasontext="guarded")
        events = [{"event": "handler_started"},
                  {"event": "start_info", "tests": 1, "suites": 1, "cases": 1},
                  {"event": "tc_start", "suite": "probe_SUITE", "case": "one"}, event,
                  dict(event="test_stats", **result), {"event": "test_done"},
                  {"event": "handler_stopped"}]
        (self.logs / "events.jsonl").write_text("".join(
            json.dumps(dict(value, seq=number)) + "\n" for number, value in enumerate(events, 1)))
        DRIVER.write_json(self.logs / "ct-result.json", result)
        DRIVER.write_json(self.logs / "runtime-facts.json", {
            "flavor": "emu", "build_type": "opt", "debug_compiled": "false",
            "lock_checking": "false", "architecture": "fixture", "perf": "false"})

    def audit(self):
        with mock.patch.object(DRIVER, "verify_execution_image", return_value=self.artifacts), \
                mock.patch.object(DRIVER, "input_files", return_value=self.current_identity), \
                mock.patch.object(DRIVER, "scope_manifest", return_value=(self.scope_path, {"exclusions": []})), \
                mock.patch.object(DRIVER, "check_scope_policy"):
            code = DRIVER.audit(self.args)
        report = next((self.evidence / "audits").glob("*/audit.json"))
        return code, DRIVER.read_json(report)

    def reject_before_classify(self, message="Execution integrity"):
        with mock.patch.object(DRIVER, "verify_execution_image", return_value=self.artifacts), \
                mock.patch.object(DRIVER, "classify") as classify:
            with self.assertRaisesRegex(RuntimeError, message):
                DRIVER.audit(self.args)
            classify.assert_not_called()


class ImmutableExecutionFailureTests(AuditEvidence):
    def test_input_drift_with_successful_controller_and_cleanup_is_irrevocable(self):
        DRIVER.write_json(self.evidence / "result.json", {
            "status": "failed", "controller_exit_code": 0,
            "problems": ["Execution inputs changed during emulator execution; expected old, observed new"]})
        self.reject_before_classify()

    def test_failure_receipt_is_checked_even_when_result_looks_passing(self):
        for error in ("Execution input identity could not be revalidated: missing source",
                      "Windows process job cleanup failed"):
            with self.subTest(error=error):
                DRIVER.write_json(self.evidence / "failure.json", {
                    "status": "failed", "controller_exit_code": 0, "error": error})
                self.reject_before_classify()

    def test_later_empty_cleanup_cannot_erase_original_owned_process_failure(self):
        DRIVER.write_json(self.evidence / "result.json", {
            "status": "failed", "controller_exit_code": 0, "problems": ["Run-owned processes remain"]})
        self.reject_before_classify()

    def test_structured_integrity_failure_does_not_depend_on_human_wording(self):
        DRIVER.write_json(self.evidence / "result.json", {
            "status": "failed", "controller_exit_code": 0,
            "execution_integrity_failures": ["cleanup-incomplete"], "problems": ["translated text"]})
        self.reject_before_classify()

    def test_malformed_problem_receipt_cannot_hide_a_legacy_integrity_error(self):
        DRIVER.write_json(self.evidence / "result.json", {
            "status": "failed", "controller_exit_code": 0,
            "problems": "Execution inputs changed during emulator execution"})
        self.reject_before_classify()

    def test_unknown_nonzero_and_missing_controller_exits_are_rejected(self):
        for value in (None, 1, False, "0"):
            with self.subTest(exit=value):
                DRIVER.write_json(self.evidence / "result.json", {"controller_exit_code": value})
                self.reject_before_classify()
        DRIVER.write_json(self.evidence / "result.json", {"status": "passed"})
        self.reject_before_classify()

    def test_missing_or_incomplete_cleanup_remains_failed(self):
        (self.evidence / "cleanup.json").unlink()
        self.assertTrue(DRIVER.execution_integrity_failures(self.evidence))
        for value in ({"remaining": ["owned"]}, {"remaining": [], "complete": False}, {"remaining": None}):
            with self.subTest(cleanup=value):
                DRIVER.write_json(self.evidence / "cleanup.json", value)
                self.assertTrue(DRIVER.execution_integrity_failures(self.evidence))

    def test_ordinary_policy_failure_can_be_reviewed_but_ct_failure_still_fails(self):
        DRIVER.write_json(self.evidence / "result.json", {
            "status": "failed", "controller_exit_code": 0, "problems": ["Nonpassing totals or unaudited skips"]})
        code, report = self.audit()
        self.assertEqual(77, code)
        self.assertEqual("not_applicable", report["status"])
        self.write_case("failed")
        with self.assertRaisesRegex(RuntimeError, "remains nonpassing"):
            self.audit()


class ExecutionSourceAndPolicyTests(AuditEvidence):
    def test_matching_current_inputs_and_original_policy_are_eligible(self):
        self.write_case("passed")
        originals = {name: (self.evidence / name).read_bytes() for name in
                     ("invocation.json", "result.json", "cleanup.json", "execution-skip-policy.json")}
        code, report = self.audit()
        self.assertEqual(0, code)
        self.assertFalse(report["historical"])
        self.assertTrue(report["qualification_eligible"])
        self.assertFalse(report["historical_reclassification"])
        for name, data in originals.items():
            self.assertEqual(data, (self.evidence / name).read_bytes())

    def test_legacy_cleanup_without_new_completion_or_timing_fields_remains_eligible(self):
        self.write_case("passed")
        DRIVER.write_json(self.evidence / "cleanup.json", {"remaining": []})
        code, report = self.audit()
        self.assertEqual(0, code)
        self.assertTrue(report["qualification_eligible"])

    def test_captured_execution_policy_tampering_is_rejected_before_classification(self):
        with (self.evidence / "execution-skip-policy.json").open("ab") as stream:
            stream.write(b" ")
        self.reject_before_classify("Captured execution skip policy differs")

    def test_original_policy_upstream_pin_must_match_execution(self):
        captured = self.evidence / "execution-skip-policy.json"
        altered = copy.deepcopy(self.policy)
        altered["upstream_commit"] = "2" * 40
        DRIVER.write_json(captured, altered)
        self.identity["inputs"][str(self.policy_path.resolve())] = DRIVER.sha(captured)
        with self.assertRaisesRegex(RuntimeError, "upstream pin differs"):
            DRIVER.execution_policy_identity(self.source, self.evidence, self.identity)

    def test_changed_source_and_matching_new_policy_cannot_reinterpret_old_events(self):
        self.guard.write_bytes(b"all() -> [one].\none(_) -> {skip, altered}.\n")
        altered = copy.deepcopy(self.policy)
        altered["rules"][0].update(source_sha256=DRIVER.sha(self.guard), guard="{skip, altered}")
        DRIVER.write_json(self.policy_path, altered)
        self.current_identity["fingerprint"] = "b" * 64
        with self.assertRaisesRegex(RuntimeError, "guard source differs from execution identity"):
            self.audit()

    def test_new_rule_with_original_guard_is_historical_and_cannot_qualify(self):
        original = copy.deepcopy(self.policy)
        original["rules"] = []
        DRIVER.write_json(self.policy_path, original)
        self.identity["inputs"][str(self.policy_path.resolve())] = DRIVER.sha(self.policy_path)
        self.freeze_identity()
        DRIVER.write_json(self.policy_path, self.policy)
        self.current_identity["fingerprint"] = "b" * 64
        code, report = self.audit()
        self.assertEqual(77, code)
        self.assertEqual("not_applicable", report["status"])
        self.assertTrue(report["historical"])
        self.assertTrue(report["historical_reclassification"])
        self.assertFalse(report["qualification_eligible"])
        self.assertEqual(1, len(report["accepted_skips"]))

    def test_current_fingerprint_drift_without_policy_change_is_historical(self):
        self.current_identity["fingerprint"] = "b" * 64
        code, report = self.audit()
        self.assertEqual(77, code)
        self.assertTrue(report["historical"])
        self.assertFalse(report["qualification_eligible"])

    def test_missing_or_ambiguous_original_guard_input_fails_closed(self):
        for ambiguous in (False, True):
            identity = copy.deepcopy(self.identity)
            if ambiguous:
                identity["inputs"][str(self.guard.parent) + os.sep + "." + os.sep + self.guard.name] = DRIVER.sha(self.guard)
            else:
                del identity["inputs"][str(self.guard.resolve())]
            with self.subTest(ambiguous=ambiguous), self.assertRaisesRegex(RuntimeError, "missing or ambiguous"):
                DRIVER.guard_identity(self.source, self.rule, identity)

    def test_crlf_policy_canonicalization_keeps_exact_raw_execution_binding(self):
        report = DRIVER.guard_identity(self.source, self.rule, self.identity)
        self.assertEqual("crlf-to-lf", report["checkout_conversion"])
        self.assertEqual(DRIVER.sha(self.guard), report["execution_input"]["sha256"])
        self.guard.write_bytes(self.guard.read_bytes().replace(b"\r\n", b"\n"))
        with self.assertRaisesRegex(RuntimeError, "guard source differs from execution identity"):
            DRIVER.guard_identity(self.source, self.rule, self.identity)

    def test_empty_suite_acceptance_binds_its_original_raw_guard_source(self):
        self.guard.write_bytes(b"all() -> [].\r\n")
        identity = copy.deepcopy(self.identity)
        identity["inputs"][str(self.guard.resolve())] = DRIVER.sha(self.guard)
        rule = dict(self.rule, id="empty-one", inventory_function="all", inventory_result=[],
                    source_sha256=hashlib.sha256(b"all() -> [].\n").hexdigest(), guard="all() -> [].")
        policy = {"empty_suite_rules": [rule]}
        selection = DRIVER.requested_selection("probe_SUITE")
        focus = {"inventory": {"empty_suites": ["probe_SUITE"], "execution_suites": ["probe_SUITE"],
                                "expected_cases": [], "expected_groups": []}}
        facts = {"suite_all_empty": ["probe_SUITE"]}
        accepted = DRIVER.audited_empty_suites(self.args, policy, facts, selection, focus, [], identity)
        self.assertIn("execution_input", accepted["probe_SUITE"]["source_identity"])
        self.guard.write_bytes(b"all() -> []. % changed after execution\r\n")
        rule["source_sha256"] = hashlib.sha256(self.guard.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        with self.assertRaisesRegex(RuntimeError, "guard source differs from execution identity"):
            DRIVER.audited_empty_suites(self.args, policy, facts, selection, focus, [], identity)

    def test_policy_declared_vm_and_empty_suite_guard_sources_join_input_closure(self):
        native = self.source / "erts/emulator/beam/erl_mseg.c"
        native.parent.mkdir(parents=True)
        native.write_bytes(b"#ifndef HAVE_ERTS_MSEG\r\nreturn 0;\r\n#endif\r\n")
        empty = self.source / "erts/include/internal/empty_guard.h"
        empty.parent.mkdir(parents=True)
        empty.write_bytes(b"/* pinned empty-suite guard */\n")
        policy = copy.deepcopy(self.policy)
        policy["rules"].append({"source": "erts/emulator/beam/erl_mseg.c"})
        policy["empty_suite_rules"].append({"source": "erts/include/internal/empty_guard.h"})
        DRIVER.write_json(self.policy_path, policy)
        manifest = self.evidence / "native-manifest.json"
        DRIVER.write_json(manifest, {"artifacts": []})
        args = SimpleNamespace(source_root=self.source, native_root=self.evidence,
            native_manifest=manifest, runtime_bin=self.evidence, process_tool=self.evidence / "process.exe",
            support_files=self.evidence / "support-files.txt", flavor="emu", build_type="opt")
        actual_sha = DRIVER.sha
        with mock.patch.object(DRIVER, "support_files", return_value=[]), \
                mock.patch.object(DRIVER, "compiler_variables", return_value=(self.evidence / "variables", "compiler")), \
                mock.patch.object(DRIVER, "helper_aliases", return_value={}), \
                mock.patch.object(DRIVER, "scope_manifest", return_value=(self.scope_path, {"exclusions": []})), \
                mock.patch.object(DRIVER, "check_scope_policy"), \
                mock.patch.object(DRIVER, "sha", side_effect=lambda path: actual_sha(path) if Path(path).is_file() else "0" * 64):
            identity = DRIVER.input_files(args)
        self.assertEqual(actual_sha(native), identity["inputs"][str(native.resolve())])
        self.assertEqual(actual_sha(empty), identity["inputs"][str(empty.resolve())])


class CleanupAndTimingTests(AuditEvidence):
    def cleanup(self, *, close_error=None, remaining=None, write_error=False, wait_error=None):
        controller = mock.Mock()
        controller.poll.return_value = 0
        controller.wait.side_effect = wait_error
        summary = {"status": "passed", "qualification_eligible": True, "problems": []}
        original_write = DRIVER.write_json
        failed_once = False
        def write(path, value):
            nonlocal failed_once
            if write_error and Path(path).name == "cleanup.json" and not failed_once:
                failed_once = True
                raise OSError("cleanup persistence failed")
            return original_write(path, value)
        with mock.patch.object(DRIVER, "WINDOWS", True), \
                mock.patch.object(DRIVER, "owned_processes"), \
                mock.patch.object(DRIVER, "close_job", side_effect=close_error), \
                mock.patch.object(DRIVER, "cleanup", return_value=[] if remaining is None else remaining), \
                mock.patch.object(DRIVER, "write_json", side_effect=write):
            DRIVER.finish_execution_cleanup(self.evidence, None, self.image, None, "nonce", {},
                                             controller, None, None, self.selection, summary)
        return summary

    def test_cleanup_completion_requires_job_close_process_checks_and_persistence(self):
        for options in (dict(close_error=RuntimeError("Windows process job cleanup failed")),
                        dict(remaining=[{"pid": 10}]), dict(remaining="unknown"),
                        dict(wait_error=DRIVER.subprocess.TimeoutExpired("controller", 5)), dict(write_error=True)):
            with self.subTest(options=options), self.assertRaises(DRIVER.ExecutionIntegrityError):
                self.cleanup(**options)
            failure = DRIVER.read_json(self.evidence / "failure.json")
            self.assertIn("cleanup-incomplete", failure["execution_integrity_failures"])
            self.assertFalse(DRIVER.read_json(self.evidence / "cleanup.json")["complete"])
            # A later successful cleanup receipt cannot erase this failure.
            DRIVER.write_json(self.evidence / "cleanup.json", {"remaining": [], "complete": True})
            self.assertTrue(DRIVER.execution_integrity_failures(self.evidence))
            (self.evidence / "failure.json").unlink()

    def test_successful_cleanup_writes_completion_without_timing_requirement(self):
        summary = self.cleanup()
        self.assertTrue(DRIVER.read_json(self.evidence / "cleanup.json")["complete"])
        self.assertEqual("passed", summary["status"])
        self.assertFalse((self.evidence / "timing.json").exists())
        self.assertEqual([], DRIVER.execution_integrity_failures(self.evidence))

    def test_input_validation_records_structured_codes_with_legacy_messages(self):
        args = SimpleNamespace(binary_root=self.root)
        for changed, code, message in (({"fingerprint": "different"}, "execution-input-drift", "Execution inputs changed"),
                                      (OSError("missing"), "execution-input-unverifiable", "could not be revalidated")):
            with self.subTest(code=code), mock.patch.object(DRIVER, "input_files", **(
                    {"side_effect": changed} if isinstance(changed, Exception) else {"return_value": changed})):
                with self.assertRaisesRegex(DRIVER.ExecutionIntegrityError, message) as raised:
                    DRIVER.validate_execution_inputs(args, self.identity)
                self.assertEqual(code, raised.exception.code)

    def test_failed_attempt_records_measured_partial_phase_and_total(self):
        @DRIVER.timed_attempt
        def failing(args):
            args._timing_evidence = self.evidence
            DRIVER.timing_phase(args, "setup", start=True)
            raise RuntimeError("setup failed")
        with mock.patch.object(DRIVER.time, "monotonic", side_effect=[10, 11, 13, 15]):
            with self.assertRaisesRegex(RuntimeError, "setup failed"):
                failing(SimpleNamespace())
        timing = DRIVER.read_json(self.evidence / "timing.json")
        self.assertEqual(5, timing["total_wall_seconds"])
        self.assertEqual(2, timing["phases"]["setup_wall_seconds"])

    def test_supervised_prepare_command_duration_is_measured_on_failure(self):
        args = SimpleNamespace(runtime_bin=self.evidence, _attempt_timing={"commands": []})
        (self.evidence / "work").mkdir()
        with mock.patch.object(DRIVER, "WINDOWS", False), \
                mock.patch.object(DRIVER, "environment", return_value={}), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=RuntimeError("compile failed")), \
                mock.patch.object(DRIVER.time, "monotonic", side_effect=[1.25, 3.75]):
            with self.assertRaisesRegex(RuntimeError, "compile failed"):
                DRIVER.boot(args, self.image, self.evidence, "", [], self.evidence / "compile.log")
        self.assertEqual([{"log": "compile.log", "supervised_wall_seconds": 2.5}], args._attempt_timing["commands"])


class AttemptDirectoryTests(AuditEvidence):
    def setUp(self):
        super().setUp()
        self.attempt_args = SimpleNamespace(source_root=self.source, binary_root=self.root / "build",
            full_root=self.root / "build/tests/emulator/full", flavor="emu", build_type="opt",
            _attempt_timing={"phases": {}, "commands": []})
        self.attempt_args.full_root.mkdir(parents=True)

    def test_windows_short_attempt_retries_directory_and_file_collisions_without_overwrite(self):
        parent = self.source.parent / "tmp"
        directory = parent / "beam-full-11111111"
        directory.mkdir()
        (directory / "owner-receipt").write_bytes(b"another run owns this")
        file = parent / "beam-full-22222222"
        file.write_bytes(b"another owner file")
        values = [SimpleNamespace(hex=prefix + "0" * 24) for prefix in ("11111111", "22222222", "33333333")]
        with mock.patch.object(DRIVER, "WINDOWS", True), \
                mock.patch.object(DRIVER.uuid, "uuid4", side_effect=values):
            created = DRIVER.attempt(self.attempt_args, "focused-run", self.identity)
        self.assertRegex(created.name, r"^beam-full-[0-9a-f]{8}$")
        self.assertEqual(18, len(created.name))
        self.assertEqual("beam-full-33333333", created.name)
        self.assertEqual({"owner-receipt"}, {path.name for path in directory.iterdir()})
        self.assertEqual(b"another run owns this", (directory / "owner-receipt").read_bytes())
        self.assertEqual(b"another owner file", file.read_bytes())
        self.assertEqual({"home", "work", "logs", "attempt.json"}, {path.name for path in created.iterdir()})
        self.assertEqual(str(created), (self.attempt_args.full_root / "latest-focused-run.txt").read_text().strip())
        metadata = DRIVER.read_json(created / "attempt.json")
        self.assertEqual("focused-run", metadata["kind"])
        self.assertEqual(str(self.source), metadata["source_root"])
        self.assertEqual(self.identity["fingerprint"], metadata["input_fingerprint"])
        self.assertEqual(self.pin, metadata["upstream_commit"])
        self.assertRegex(metadata["created_utc"], r"^\d{8}T\d{6}Z$")
        self.assertEqual(metadata, self.attempt_args._attempt_timing["attempt"])
        self.assertEqual(created, self.attempt_args._timing_evidence)
        # The established audit ownership prefix accepts the compact namespace.
        with mock.patch.object(DRIVER, "read_json", side_effect=RuntimeError("reached invocation")):
            with self.assertRaisesRegex(RuntimeError, "reached invocation"):
                DRIVER.audit(SimpleNamespace(attempt=created, source_root=self.source))

    def test_exhausted_collisions_leave_existing_owner_and_latest_pointer_unchanged(self):
        directory = self.source.parent / "tmp/beam-full-deadbeef"
        directory.mkdir()
        (directory / "owner-receipt").write_bytes(b"original owner")
        pointer = self.attempt_args.full_root / "latest-focused-run.txt"
        pointer.write_text("original pointer\n")
        with mock.patch.object(DRIVER, "WINDOWS", True), \
                mock.patch.object(DRIVER.uuid, "uuid4", return_value=SimpleNamespace(hex="deadbeef" + "0" * 24)) as tokens:
            with self.assertRaisesRegex(RuntimeError, "fresh owned emulator attempt"):
                DRIVER.attempt(self.attempt_args, "focused-run", self.identity)
            self.assertGreater(tokens.call_count, 1)
        self.assertEqual({"owner-receipt"}, {path.name for path in directory.iterdir()})
        self.assertEqual(b"original owner", (directory / "owner-receipt").read_bytes())
        self.assertEqual("original pointer\n", pointer.read_text())
        self.assertFalse(hasattr(self.attempt_args, "_timing_evidence"))

    def test_unix_historical_name_pattern_is_unchanged(self):
        stamp = "20260102T030405Z"
        tag = hashlib.sha256(str(self.attempt_args.binary_root).encode()).hexdigest()[:12]
        with mock.patch.object(DRIVER, "WINDOWS", False), \
                mock.patch.object(DRIVER.uuid, "uuid4", return_value=SimpleNamespace(hex="abcdef" + "0" * 26)), \
                mock.patch.object(DRIVER.datetime, "datetime") as clock:
            clock.now.return_value.strftime.return_value = stamp
            created = DRIVER.attempt(self.attempt_args, "prepare", self.identity)
        self.assertEqual("beam-full-" + tag + "-prepare-" + stamp + "-abcdef", created.name)
        metadata = DRIVER.read_json(created / "attempt.json")
        self.assertEqual("prepare", metadata["kind"])
        self.assertEqual(stamp, metadata["created_utc"])


if __name__ == "__main__":
    unittest.main()
