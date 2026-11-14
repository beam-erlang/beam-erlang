"""Linux private UTF-8 prerequisites; never launch a candidate VM."""
import importlib.util
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import unittest
from unittest import mock


SOURCE_ROOT = Path(os.environ.get("BEAM_LOCALE_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
SPEC = importlib.util.spec_from_file_location("full_emulator_locale", Path(__file__).with_name("full_emulator.py"))
DRIVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DRIVER)


class LinuxLocaleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="beam-locale-unit-", dir=SOURCE_ROOT.parent / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for name in ("home", "work", "bin"):
            (self.root / name).mkdir()
        self.env = {"PATH": "/usr/bin:/bin", "HOME": str(self.root / "home")}

    @staticmethod
    def completed(stdout):
        return subprocess.CompletedProcess([], 0, stdout)

    def test_prefers_installed_c_utf8_and_verifies_actual_charmap(self):
        with mock.patch.object(DRIVER.shutil, "which", return_value="/usr/bin/locale"), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=[
                    self.completed("C\nC.utf8\nC.UTF-8\nen_US.utf8\n"), self.completed("UTF-8\n")]) as probe:
            selected = DRIVER.linux_utf8_locale(self.env)
        self.assertEqual("C.UTF-8", selected)
        self.assertEqual("C", probe.call_args_list[0].kwargs["env"]["LC_ALL"])
        self.assertEqual(selected, probe.call_args_list[1].kwargs["env"]["LC_ALL"])
        self.assertEqual(["/usr/bin/locale", "charmap"], probe.call_args_list[1].args[0])
        self.assertNotIn("LC_ALL", self.env)

    def test_accepts_installed_c_utf8_alias(self):
        with mock.patch.object(DRIVER.shutil, "which", return_value="/usr/bin/locale"), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=[
                    self.completed("C\nC.utf8\nPOSIX\n"), self.completed("UTF-8\n")]):
            self.assertEqual("C.utf8", DRIVER.linux_utf8_locale(self.env))

    def test_missing_required_locale_is_preparation_failure(self):
        with mock.patch.object(DRIVER.shutil, "which", return_value="/usr/bin/locale"), \
                mock.patch.object(DRIVER.subprocess, "run", return_value=self.completed("C\nPOSIX\n")):
            with self.assertRaisesRegex(RuntimeError, "require an available C.UTF-8/C.utf8"):
                DRIVER.linux_utf8_locale(self.env)

    def test_probe_failures_and_non_utf8_charmap_cannot_silently_fall_back(self):
        with mock.patch.object(DRIVER.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "locale is unavailable"):
                DRIVER.linux_utf8_locale(self.env)
        with mock.patch.object(DRIVER.shutil, "which", return_value="/usr/bin/locale"), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=subprocess.CalledProcessError(1, ["locale", "-a"])):
            with self.assertRaisesRegex(RuntimeError, "Cannot inspect"):
                DRIVER.linux_utf8_locale(self.env)
        with mock.patch.object(DRIVER.shutil, "which", return_value="/usr/bin/locale"), \
                mock.patch.object(DRIVER.subprocess, "run", side_effect=[
                    self.completed("C.utf8\n"), self.completed("ANSI_X3.4-1968\n")]):
            with self.assertRaisesRegex(RuntimeError, "did not produce UTF-8"):
                DRIVER.linux_utf8_locale(self.env)

    def test_linux_private_locale_does_not_inherit_or_change_host_locale(self):
        with mock.patch.object(DRIVER, "WINDOWS", False), \
                mock.patch.object(DRIVER.platform, "system", return_value="Linux"), \
                mock.patch.object(DRIVER, "linux_utf8_locale", return_value="C.utf8"), \
                mock.patch.dict(os.environ, {"LC_ALL": "C", "LANG": "C", "ERL_FLAGS": "external"}):
            env = DRIVER.environment(self.root, self.root / "bin")
            self.assertEqual("C", os.environ["LC_ALL"])
        self.assertEqual("C.utf8", env["LC_ALL"])
        self.assertNotIn("LANG", env)
        self.assertNotIn("ERL_FLAGS", env)

    def test_macos_environment_does_not_run_linux_locale_probes(self):
        with mock.patch.object(DRIVER, "WINDOWS", False), \
                mock.patch.object(DRIVER.platform, "system", return_value="Darwin"), \
                mock.patch.object(DRIVER, "linux_utf8_locale") as probe:
            env = DRIVER.environment(self.root, self.root / "bin")
        probe.assert_not_called()
        self.assertNotIn("LC_ALL", env)
        self.assertEqual("Europe/Stockholm", env["TZ"])

    @unittest.skipUnless(platform.system() == "Linux", "Native Linux locale probe")
    def test_real_private_linux_environment_records_utf8_without_host_change(self):
        original = dict(os.environ)
        env = DRIVER.environment(self.root, self.root / "bin")
        DRIVER.capture_environment(self.root, env)
        facts = DRIVER.read_json(self.root / "execution-environment-facts.json")
        self.assertEqual("Linux", facts["platform"])
        self.assertEqual(env["LC_ALL"], facts["lc_all"])
        self.assertEqual("UTF8", facts["locale_charmap"].upper().replace("-", ""))
        self.assertEqual(original, dict(os.environ))


if __name__ == "__main__":
    unittest.main()
