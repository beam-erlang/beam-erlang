"""Native Windows preparation, guard and candidate-integrity regressions."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest


SOURCE_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("full_emulator_windows", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class WindowsHarnessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-full-windows-unit-", dir=SOURCE_ROOT.parent / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ("home", "work", "runtime", "native/compiler_config"):
            (self.root / name).mkdir(parents=True)
        self.args = SimpleNamespace(runtime_bin=self.root / "runtime", native_root=self.root / "native",
                                    native_manifest=self.root / "native/manifest.json", flavor="emu", build_type="debug",
                                    source_root=SOURCE_ROOT)
        self.policy = DRIVER.read_json(SOURCE_ROOT / "tests/emulator/emulator_skip_policy.json")

    @unittest.skipUnless(DRIVER.WINDOWS, "Native Windows paths and environment")
    def test_environment_keeps_required_windows_paths_and_private_home(self):
        env = DRIVER.environment(self.root, self.args.runtime_bin)
        self.assertTrue(Path(env["COMSPEC"]).is_file())
        self.assertEqual(str(self.root / "work"), env["TEMP"])
        self.assertEqual(str(self.root / "home"), env["HOMEDRIVE"] + env["HOMEPATH"])
        self.assertNotIn("TZ", env)
        self.assertNotIn("ERL_FLAGS", env)

    @unittest.skipUnless(DRIVER.WINDOWS, "Native Windows path normalization")
    def test_vm_path_matches_erlang_drive_letter_and_separator_normalization(self):
        self.assertEqual("c:/Users/Example/path with spaces", DRIVER.vm_path(Path("C:/Users/Example/path with spaces")))

    @unittest.skipUnless(DRIVER.WINDOWS, "Native Windows compiler path")
    def test_compiler_with_drive_letter_and_spaces_is_bound_to_manifest(self):
        compiler = self.root / "compiler with spaces.exe"
        compiler.write_bytes(b"compiler")
        text = compiler.as_posix()
        (self.args.native_root / "compiler_config/variables").write_text("{'CC', \"" + text + "\"}.\n")
        DRIVER.write_json(self.args.native_manifest, {"toolchain": {"CMAKE_C_COMPILER": text}})
        self.assertEqual(text, DRIVER.compiler_variables(self.args)[1])
        DRIVER.write_json(self.args.native_manifest, {"toolchain": {"CMAKE_C_COMPILER": str(self.root / "other.exe")}})
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            DRIVER.compiler_variables(self.args)

    @unittest.skipUnless(DRIVER.WINDOWS, "Native Windows executable copies")
    def test_candidate_and_debug_copy_tampering_are_rejected(self):
        for name in ("beam.exe", "beam.smp.exe", "beam.emu.exe", "beam.pdb", "beam.smp.pdb", "beam.emu.pdb"):
            (self.args.runtime_bin / name).write_bytes(name.encode())
        image = self.root / "image"
        (image / "erts-17.0/bin").mkdir(parents=True)
        identity = {"build_type": "debug", "flavor": "emu", "child_helper": "windows-no-erl_child_setup",
                    "inputs": {str(path): DRIVER.sha(path) for path in self.args.runtime_bin.iterdir()}}
        DRIVER.stage_candidate(self.args, image)
        DRIVER.verify_execution_image(self.args, image, identity)
        (image / "erts-17.0/bin/beam.emu.pdb").write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "identity changed"):
            DRIVER.verify_execution_image(self.args, image, identity)

    def test_only_crlf_checkout_conversion_can_match_pinned_guard(self):
        path = self.root / "guard.erl"
        original = b"all() ->\n    [].\n"
        path.write_bytes(original.replace(b"\n", b"\r\n"))
        import hashlib
        rule = {"id": "guard", "source": "guard.erl", "source_sha256": hashlib.sha256(original).hexdigest(),
                "guard": original.decode()}
        identity = DRIVER.guard_identity(self.root, rule)
        self.assertEqual("crlf-to-lf", identity["checkout_conversion"])
        path.write_bytes(original.replace(b"    ", b"  "))
        with self.assertRaisesRegex(RuntimeError, "source identity changed"):
            DRIVER.guard_identity(self.root, rule)

    def test_generated_launcher_propagates_driver_failure(self):
        driver = self.root / "failing driver.py"
        driver.write_text("import sys\nsys.exit(7)\n")
        template = (SOURCE_ROOT / "tests/cmake/RunEmulator.py.in").read_text()
        rendered = template.replace("@BEAM_FULL_LAUNCHER_PYTHON@", json.dumps(sys.executable))
        rendered = rendered.replace("@BEAM_FULL_LAUNCHER_DRIVER@", json.dumps(str(driver)))
        rendered = rendered.replace("@BEAM_FULL_LAUNCHER_ARGUMENTS@", "")
        launcher = self.root / "run.py"
        launcher.write_text(rendered)
        result = subprocess.run([sys.executable, "-B", str(launcher), "--suite", "atomics_SUITE"], timeout=10)
        self.assertEqual(7, result.returncode)

    def empty(self, *, family="win32", platform="Windows", cases=(), observed=("os_signal_SUITE",), suite="os_signal_SUITE"):
        selection = DRIVER.requested_selection(suite, cases=",".join(cases) or None)
        focus = {"inventory": {"empty_suites": [suite], "execution_suites": [suite],
                                "expected_cases": [], "expected_groups": []}}
        facts = {"platform": platform, "os_family": family, "suite_all_empty": list(observed)}
        return DRIVER.audited_empty_suites(self.args, self.policy, facts, selection, focus, [])

    def test_exact_native_empty_guard_is_audited_separately(self):
        self.assertEqual("windows-os-signal-empty", self.empty()["os_signal_SUITE"]["rule"])
        outcomes = DRIVER.suite_outcomes([], ["os_signal_SUITE"], [], [], self.empty())
        self.assertEqual("not_applicable", outcomes["os_signal_SUITE"]["status"])
        self.assertEqual("failed", DRIVER.suite_outcomes([], ["other_SUITE"], [], [])["other_SUITE"]["status"])

    def test_empty_guard_rejects_wrong_platform_partial_and_unobserved_inventory(self):
        for args in ({"family": "unix"}, {"platform": "Darwin"}, {"cases": ("t_sighup",)},
                     {"observed": ()}, {"suite": "other_SUITE", "observed": ("other_SUITE",)}):
            with self.subTest(args=args), self.assertRaises(RuntimeError):
                self.empty(**args)

    def test_timezone_guard_requires_successful_event_native_probe(self):
        rule = next(rule for rule in self.policy["rules"] if rule["id"] == "windows-time-univ-to-local-stockholm")
        facts = {"platform": "Windows", "os_family": "win32", "timezone_probe": "native_win32_tzutil",
                 "timezone_probe_succeeded": "true", "stockholm_timezone": "false"}
        self.assertTrue(DRIVER.condition(rule, facts, {}))
        for field in ("os_family", "timezone_probe", "timezone_probe_succeeded", "stockholm_timezone"):
            changed = dict(facts)
            changed.pop(field)
            self.assertFalse(DRIVER.condition(rule, changed, {}), field)
        self.assertFalse(DRIVER.condition(rule, facts, {"guard_facts": {"timezone_probe_succeeded": "false"}}))


if __name__ == "__main__":
    unittest.main()
