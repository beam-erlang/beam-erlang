"""Run harness and checked-decoder regressions without initializing the VM."""
from pathlib import Path
import argparse
import sys
import unittest

from . import test_process
from vm import test_ethread
from .process import isolated_environment, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--etf-check', type=Path, required=True)
    parser.add_argument('--test-dir', type=Path, required=True)
    parser.add_argument('--isolation-probe', type=Path)
    parser.add_argument('--isolation-library', type=Path)
    parser.add_argument('--host-observer', type=Path)
    args = parser.parse_args()
    if bool(args.isolation_probe) != bool(args.isolation_library):
        parser.error('Both isolation fixtures must be supplied')
    if sys.platform == 'darwin' and not args.host_observer:
        parser.error('Darwin acceptance requires the native host observer')
    root = args.test_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    test_process.HarnessTests.test_root = root
    test_process.HarnessTests.host_observer = (args.host_observer.resolve(strict=True)
                                             if args.host_observer else None)
    test_ethread.EthreadRunnerTests.test_root = root
    tests = unittest.defaultTestLoader.loadTestsFromModule(test_process)
    tests.addTests(unittest.defaultTestLoader.loadTestsFromModule(test_ethread))
    if args.isolation_probe:
        from .test_isolation import SeatbeltTests
        SeatbeltTests.test_root = root
        SeatbeltTests.probe = args.isolation_probe.resolve(strict=True)
        SeatbeltTests.library = args.isolation_library.resolve(strict=True)
        tests.addTests(unittest.defaultTestLoader.loadTestsFromTestCase(SeatbeltTests))
    if not unittest.TextTestRunner(verbosity=2).run(tests).wasSuccessful():
        raise SystemExit(1)
    if args.host_observer:
        observation = run([args.host_observer, '--self-test', *([args.isolation_library] if args.isolation_library else [])], cwd=root,
                          env=isolated_environment(root), timeout=10)
        if observation.stdout != b'HOST_OBSERVATIONS_RESTORED\n' or observation.stderr:
            raise AssertionError('Host observation regression failed')
    run([args.etf_check], cwd=root, env=isolated_environment(root), timeout=10)


if __name__ == '__main__':
    main()
