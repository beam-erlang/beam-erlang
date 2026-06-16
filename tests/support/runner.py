"""Run harness and checked-decoder regressions without initializing the VM."""
from pathlib import Path
import argparse
import unittest

from . import test_process
from vm import test_ethread
from .process import isolated_environment, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--etf-check', type=Path, required=True)
    parser.add_argument('--test-dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.test_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    test_process.HarnessTests.test_root = root
    test_ethread.EthreadRunnerTests.test_root = root
    tests = unittest.defaultTestLoader.loadTestsFromModule(test_process)
    tests.addTests(unittest.defaultTestLoader.loadTestsFromModule(test_ethread))
    if not unittest.TextTestRunner(verbosity=2).run(tests).wasSuccessful():
        raise SystemExit(1)
    run([args.etf_check], cwd=root, env=isolated_environment(root), timeout=10)


if __name__ == '__main__':
    main()
