"""Acquire disposable installations without changing the acceptance assertions."""
from pathlib import Path
import shutil
import sys

from .process import isolated_environment, run


def add_installation_arguments(parser):
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--build-dir', type=Path)
    inputs.add_argument('--acquired-prefix', type=Path)
    parser.add_argument('--host-observer', type=Path)
    parser.add_argument('--test-support-build', type=Path,
                        help='Matching generated test inputs; never a runtime/install fallback')


def resolve_installation(parser, args, *, private=False):
    if args.acquired_prefix:
        args.acquired_prefix = args.acquired_prefix.resolve()
        if not (args.acquired_prefix / 'lib/beam/runtime-artifacts.json').is_file():
            parser.error('acquired prefix lacks runtime artifact metadata')
        if private and not args.test_support_build:
            parser.error('acquired private-lifecycle acceptance requires --test-support-build')
        args.build_dir = args.test_support_build
    if args.build_dir:
        args.build_dir = args.build_dir.resolve()
    if sys.platform=='darwin':
        from . import host_observe
        observer=args.host_observer or (args.build_dir/'tests/beam_test_host_observer' if args.build_dir else None)
        if observer is None or not observer.is_file():
            parser.error('Darwin acceptance requires an explicit native --host-observer or matching test-support build')
        host_observe.selected_observer=observer.resolve(strict=True)
    return args


def stage_installation(args, destination):
    destination = Path(destination).resolve()
    if args.acquired_prefix:
        source = Path(args.acquired_prefix).resolve()
        if source == destination or source in destination.parents or destination in source.parents:
            raise ValueError('acceptance scratch must be separate from acquired prefix')
        # Reinstallation is an existing fixture contract. copytree replaces
        # regular files but cannot replace an already-present symbolic link.
        for entry in source.rglob('*'):
            target = destination / entry.relative_to(source)
            if entry.is_symlink() and target.is_symlink():
                target.unlink()
        shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True)
    else:
        run([args.cmake, '--install', args.build_dir, '--prefix', destination],
            cwd=destination.parent, env=isolated_environment(destination.parent), timeout=120)


def expected_version():
    return (Path(__file__).resolve().parents[2] / 'BEAM_VERSION').read_text().strip()
