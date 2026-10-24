"""Verify access denial against usable files, executables and native libraries."""

from pathlib import Path
import shutil
import sys
import tempfile
import unittest

from .isolation import Seatbelt
from .process import isolated_environment, run


class SeatbeltTests(unittest.TestCase):
    test_root = None
    probe = None
    library = None

    def test_real_inputs_and_descendants(self):
        with tempfile.TemporaryDirectory(prefix='seatbelt-', dir=self.test_root) as directory:
            root = Path(directory)
            original = root / 'original inputs ü'
            original.mkdir()
            data = original / 'payload'
            data.write_text('original input\n')
            executable = original / 'executable'
            library = original / self.library.name
            shutil.copy2(self.probe, executable)
            shutil.copy2(self.library, library)
            work = root / 'copied acceptance'
            work.mkdir()
            env = isolated_environment(work)
            inputs = [data, executable, library]
            # Missing/broken fixtures must not masquerade as access denial.
            allowed = run([self.probe, 'allow', *inputs], cwd=work, env=env, timeout=10)
            self.assertEqual(allowed.stdout, b'INPUTS_USABLE\n')
            self.assertEqual(allowed.stderr, b'')
            sandbox = Seatbelt(work, [original])
            for selected in (original, root / 'alias'):
                if selected != original:
                    selected.symlink_to(original, target_is_directory=True)
                output = run(sandbox.command([self.probe, 'deny',
                             selected / data.name, selected / executable.name,
                             selected / library.name]), cwd=work, env=env, timeout=10)
                self.assertEqual(output.stdout, b'DENIED_IN_CHILD_AND_DESCENDANT\n')
                self.assertEqual(output.stderr, b'')

    def test_work_cannot_be_denied(self):
        with tempfile.TemporaryDirectory(prefix='seatbelt-path-', dir=self.test_root) as directory:
            with self.assertRaises(ValueError):
                Seatbelt(directory, [directory])


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--test-dir', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('This acceptance pilot requires Darwin')
    SeatbeltTests.test_root = args.test_dir.resolve()
    SeatbeltTests.test_root.mkdir(parents=True, exist_ok=True)
    SeatbeltTests.probe = args.probe.resolve(strict=True)
    SeatbeltTests.library = args.library.resolve(strict=True)
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(SeatbeltTests))
    raise SystemExit(not result.wasSuccessful())


if __name__ == '__main__':
    main()
