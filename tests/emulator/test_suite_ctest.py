"""Exercise suite discovery with real CTest and a harmless recording launcher."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
import xml.etree.ElementTree as ET


SOURCE_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_TMP = SOURCE_ROOT.parent / "tmp"
MODULE_SPEC = importlib.util.spec_from_file_location(
    "full_emulator_suite_ctest", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(DRIVER)
CTEST = shutil.which("ctest")


class PreparedSuiteNamesTests(unittest.TestCase):
    def setUp(self):
        self.corpus = {"suites": ["z_SUITE", "bits_SUITE", "a_SUITE"],
                       "no_opt": ["bits"], "stripped_types": ["bits"]}

    def test_generated_variants_and_ordinary_suites_share_one_sorted_inventory(self):
        self.assertEqual(["a_SUITE", "bits_SUITE", "bits_no_opt_SUITE",
                          "bits_stripped_types_SUITE", "z_SUITE"],
                         DRIVER.prepared_suite_names(self.corpus))
        self.assertEqual(["z_SUITE", "bits_SUITE", "a_SUITE"], self.corpus["suites"])

    def test_duplicate_or_colliding_identities_are_rejected(self):
        for change in ({"suites": self.corpus["suites"] + ["bits_SUITE"]},
                       {"no_opt": ["bits", "bits"]},
                       {"suites": self.corpus["suites"] + ["bits_no_opt_SUITE"]}):
            with self.subTest(change=change):
                with self.assertRaisesRegex(RuntimeError, "Duplicate"):
                    DRIVER.prepared_suite_names(dict(self.corpus, **change))

    def test_invalid_names_shapes_and_orphaned_variants_are_rejected(self):
        invalid = [None, {}, dict(self.corpus, suites=[]),
                   dict(self.corpus, suites="bits_SUITE"),
                   dict(self.corpus, suites=["../bits_SUITE"]),
                   dict(self.corpus, no_opt=["bits_SUITE"]),
                   dict(self.corpus, no_opt=["missing"]),
                   dict(self.corpus, stripped_types=[42])]
        for corpus in invalid:
            with self.subTest(corpus=corpus):
                with self.assertRaises(RuntimeError):
                    DRIVER.prepared_suite_names(corpus)


@unittest.skipUnless(CTEST, "CTest is required for suite catalog integration checks")
class SuiteCTestTests(unittest.TestCase):
    def setUp(self):
        WORKSPACE_TMP.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-suite-ctest-unit-",
                                                     dir=WORKSPACE_TMP)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        # Bracket quoting must keep these CMake metacharacters literal.
        self.full_root = self.root / 'build "quotes" ; ${unexpanded} ]=] ]]' / "full"
        self.full_root.mkdir(parents=True)
        self.log = self.root / "invocations.jsonl"
        self.launcher = self.full_root / "run.py"
        self.launcher.write_text(
            "import json,sys\nfrom pathlib import Path\n"
            "with Path(" + repr(str(self.log)) + ").open('a') as log:\n"
            "    log.write(json.dumps(sys.argv[1:]) + '\\n')\n")
        self.args = SimpleNamespace(full_root=self.full_root, flavor="jit")
        self.corpus = {"suites": ["beta_SUITE", "alpha_SUITE"],
                       "no_opt": [], "stripped_types": []}
        self.catalog = DRIVER.write_suite_ctest(self.args, self.corpus)

    def ctest(self, directory, *arguments):
        result = subprocess.run([CTEST, "--test-dir", str(directory), *arguments],
                                capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        return result.stdout

    def invocations(self):
        if not self.log.exists():
            return []
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_catalog_has_only_suite_diagnostics_and_literal_bound_arguments(self):
        discovery = json.loads(self.ctest(self.catalog.parent, "--show-only=json-v1"))
        self.assertEqual(["emulator.suite.alpha_SUITE", "emulator.suite.beta_SUITE"],
                         [test["name"] for test in discovery["tests"]])
        for test in discovery["tests"]:
            suite = test["name"].split(".")[-1]
            self.assertEqual([sys.executable, "-B", str(self.launcher), "--suite", suite],
                             test["command"])
            properties = {item["name"]: item["value"] for item in test["properties"]}
            self.assertEqual({"emulator-suite", "jit"}, set(properties["LABELS"]))
            self.assertTrue(properties["RUN_SERIAL"])
            self.assertEqual(21660, properties["TIMEOUT"])
            self.assertEqual(77, properties["SKIP_RETURN_CODE"])
        self.assertEqual([], self.invocations())

    def test_ctest_filter_runs_only_the_requested_suite(self):
        self.ctest(self.catalog.parent, "-R", "^emulator.suite.beta_SUITE$",
                   "--output-on-failure", "--no-tests=error", "-j1")
        self.assertEqual([["--suite", "beta_SUITE"]], self.invocations())

    def test_bracket_arguments_preserve_newlines_backslashes_and_expansion_text(self):
        literal = '\n\\literal ${unexpanded} ; "quotes" ]] ]=]\n'
        values = ["literal.argument", sys.executable, "-B", str(self.launcher), "--echo", literal]
        (self.root / "CTestTestfile.cmake").write_text(
            "add_test(" + " ".join(DRIVER.cmake_bracket(value) for value in values) + ")\n")
        self.ctest(self.root, "--output-on-failure", "--no-tests=error", "-j1")
        self.assertEqual([["--echo", literal]], self.invocations())

    def test_unfiltered_root_run_does_not_traverse_the_diagnostic_tree(self):
        values = ["emulator.full", sys.executable, "-B", str(self.launcher), "--full"]
        (self.root / "CTestTestfile.cmake").write_text(
            "add_test(" + " ".join(DRIVER.cmake_bracket(value) for value in values) + ")\n")
        discovery = json.loads(self.ctest(self.root, "--show-only=json-v1"))
        self.assertEqual(["emulator.full"], [test["name"] for test in discovery["tests"]])
        self.ctest(self.root, "--output-on-failure", "--no-tests=error", "-j1")
        self.assertEqual([["--full"]], self.invocations())

    def test_unfiltered_diagnostic_tree_never_runs_a_full_campaign(self):
        self.ctest(self.catalog.parent, "--output-on-failure", "--no-tests=error", "-j1")
        self.assertEqual([["--suite", "alpha_SUITE"], ["--suite", "beta_SUITE"]],
                         self.invocations())

    def test_invalid_inventory_cannot_replace_a_valid_catalog(self):
        before = self.catalog.read_bytes()
        corpus = copy.deepcopy(self.corpus)
        corpus["suites"].append("alpha_SUITE")
        with self.assertRaisesRegex(RuntimeError, "Duplicate"):
            DRIVER.write_suite_ctest(self.args, corpus)
        self.assertEqual(before, self.catalog.read_bytes())
        self.assertEqual([], list(self.catalog.parent.glob(".CTestTestfile-*.tmp")))

    def test_exit_77_is_skipped_only_for_diagnostic_catalog_entries(self):
        self.launcher.write_text("raise SystemExit(77)\n")
        diagnostic_report = self.root / "diagnostic.xml"
        self.ctest(self.catalog.parent, "-R", "^emulator.suite.alpha_SUITE$",
                   "--output-junit", str(diagnostic_report), "--output-on-failure", "--no-tests=error")
        diagnostic_case = ET.parse(diagnostic_report).find(".//testcase")
        self.assertIsNotNone(diagnostic_case.find("skipped"))
        self.assertIsNone(diagnostic_case.find("failure"))

        # The ordinary full-campaign entry has no diagnostic skip-return rule.
        values = ["emulator.full", sys.executable, "-B", str(self.launcher)]
        (self.root / "CTestTestfile.cmake").write_text(
            "add_test(" + " ".join(DRIVER.cmake_bracket(value) for value in values) + ")\n")
        full_report = self.root / "full.xml"
        result = subprocess.run([CTEST, "--test-dir", str(self.root), "--output-junit", str(full_report),
                                 "--output-on-failure", "--no-tests=error"], capture_output=True, text=True)
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
        full_case = ET.parse(full_report).find(".//testcase")
        self.assertIsNotNone(full_case.find("failure"))
        self.assertIsNone(full_case.find("skipped"))


if __name__ == "__main__":
    unittest.main()
