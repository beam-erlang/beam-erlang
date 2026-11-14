"""Exact native mseg skip accounting; does not launch any suite VM."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import unittest


SOURCE_ROOT = (Path(os.environ["BEAM_GUARD_SOURCE_ROOT"]) if "BEAM_GUARD_SOURCE_ROOT" in os.environ
               else Path(__file__).resolve().parents[2])
POLICY_PATH = Path(os.environ.get("BEAM_GUARD_POLICY",
                   SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json"))
POLICY = json.loads(POLICY_PATH.read_text())
RULE = next(rule for rule in POLICY["rules"]
            if rule["id"] == "windows-alloc-mseg-clear-cache-unavailable")
SPEC = importlib.util.spec_from_file_location(
    "pending_mseg_driver", SOURCE_ROOT / "tests/emulator/full_emulator.py")
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class NativeMsegGuardTests(unittest.TestCase):
    def facts(self):
        return dict(platform="Windows", os_family="win32")

    def condition(self, facts):
        return DRIVER.condition(RULE, self.facts(), {"guard_facts": facts})

    def test_exact_native_fixture_and_event_scope_are_bound(self):
        self.assertEqual("alloc_SUITE", RULE["suite"])
        self.assertEqual("mseg_clear_cache", RULE["case"])
        self.assertEqual("^No mseg_alloc; nothing to test$", RULE["reason_regex"])
        self.assertEqual("erts/emulator/test/alloc_SUITE_data/mseg_clear_cache.c", RULE["source"])
        raw = (SOURCE_ROOT / RULE["source"]).read_bytes()
        canonical = raw.replace(b"\r\n", b"\n")
        self.assertEqual(RULE["source_sha256"], hashlib.sha256(canonical).hexdigest())
        self.assertIn(RULE["guard"], canonical.decode("utf-8"))

    def test_only_successful_native_unavailable_measurement_qualifies(self):
        self.assertTrue(self.condition(dict(mseg_alloc_probe="ok", mseg_alloc="false")))
        self.assertFalse(self.condition(dict(mseg_alloc_probe="ok", mseg_alloc="true")))
        self.assertFalse(self.condition(dict(mseg_alloc="false")))
        self.assertFalse(self.condition(dict(mseg_alloc_probe="ok")))
        self.assertFalse(self.condition({}))

    def test_query_errors_cannot_be_reinterpreted_as_no_allocator(self):
        for facts in (dict(mseg_alloc_probe="error", mseg_alloc_error="badarg"),
                      dict(mseg_alloc_probe="error", mseg_alloc="false", mseg_alloc_error="badarg"),
                      dict(mseg_alloc_error="badarg")):
            with self.subTest(facts=facts):
                self.assertFalse(self.condition(facts))

    def test_native_platform_must_be_observed(self):
        event = {"guard_facts": dict(mseg_alloc_probe="ok", mseg_alloc="false")}
        for facts in ({}, dict(platform="Windows"), dict(os_family="win32"),
                      dict(platform="Linux", os_family="unix")):
            with self.subTest(facts=facts):
                self.assertFalse(DRIVER.condition(RULE, facts, event))


class NativeDriverGuardTests(unittest.TestCase):
    def rules(self):
        return [rule for rule in POLICY["rules"] if rule["id"].startswith("windows-driver-native-")]

    def test_only_completed_native_guard_cases_are_listed(self):
        self.assertEqual({"io_ready_exit", "ioq_exit_ready_input", "ioq_exit_ready_output",
                          "ioq_exit_ready_input_async", "ioq_exit_ready_output_async",
                          "use_fallback_pollset", "bad_fd_in_pollset", "fd_change", "steal_control",
                          "thread_mseg_alloc_cache_clean"}, {rule["case"] for rule in self.rules()})
        self.assertEqual(10, len(self.rules()))
        # Native timeout/ready_async flushing remains applicable functional work.
        self.assertFalse(any(rule["case"] in ("ioq_exit_timeout", "ioq_exit_ready_async",
                                             "ioq_exit_timeout_async") for rule in self.rules()))
        self.assertTrue(all(rule["suite"] == "driver_SUITE" for rule in self.rules()))

    def test_native_driver_guards_bind_complete_pinned_sources(self):
        for rule in self.rules():
            with self.subTest(rule=rule["id"]):
                canonical = (SOURCE_ROOT / rule["source"]).read_bytes().replace(b"\r\n", b"\n")
                self.assertEqual(rule["source_sha256"], hashlib.sha256(canonical).hexdigest())
                self.assertIn(rule["guard"], canonical.decode("utf-8"))
                self.assertTrue(rule["reason_regex"].startswith("^"))
                self.assertTrue(rule["reason_regex"].endswith("$"))

    def test_native_guard_platform_must_be_present_and_correct(self):
        for rule in self.rules():
            facts = dict(platform="Windows", os_family="win32", mseg_alloc_probe="ok", mseg_alloc="false")
            with self.subTest(rule=rule["id"]):
                self.assertTrue(DRIVER.condition(rule, facts, {}))
                del facts["os_family"]
                self.assertFalse(DRIVER.condition(rule, facts, {}))
                facts.update(platform="Linux", os_family="unix")
                self.assertFalse(DRIVER.condition(rule, facts, {}))

    def test_driver_mseg_guard_rejects_available_missing_and_error_observations(self):
        rule = next(rule for rule in self.rules() if rule["case"] == "thread_mseg_alloc_cache_clean")
        native = dict(platform="Windows", os_family="win32")
        for event_facts in ({}, dict(mseg_alloc_probe="ok"),
                            dict(mseg_alloc_probe="ok", mseg_alloc="true"),
                            dict(mseg_alloc_probe="error", mseg_alloc="false", mseg_alloc_error="badarg")):
            with self.subTest(event_facts=event_facts):
                self.assertFalse(DRIVER.condition(rule, native, {"guard_facts": event_facts}))


if __name__ == "__main__":
    unittest.main()
