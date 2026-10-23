#!/usr/bin/env python3
"""Compile-command acceptance using only a fresh installed distribution."""

from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil

from support.files import hidden, hidden_paths
from support.process import isolated_environment, check

from .basic import basic_cases
from .build_tools import cmake_case, literal_make_case
from .common import Compiler, module, run
from .dependencies import dependency_cases, transform_case
from .owned import compiler_lifetime_case, owned_compiler_cases


def check_prefix(prefix, work, cmake, build, source, project):
    for runtime in ("beam",):
        print(f"installed compile: {work.name}, runtime={runtime}", flush=True)
        c = Compiler(prefix, work / runtime, runtime)
        basic_cases(c)
        dependency_cases(c)
        literal_make_case(c)
        transform_case(c)
        for generator in ("Ninja", "Unix Makefiles"):
            cmake_case(c, cmake, generator)
    owned_compiler_cases(prefix, work / "owned-compiler")
    compiler_lifetime_case(prefix, work / "compiler-lifetimes", cmake, build, source, project)


def inactive_and_help(prefix, work):
    assets = [prefix / "lib/beam/runtime", prefix / "lib/beam/tools/compiler", prefix / "lib/beam/ebin",
              prefix / "libexec/beam",
                       *(prefix / "lib/beam" / inventory for inventory in ("library", "boot", "services")),
              *(prefix / "lib" / f"libbeam-{unit}.so"
                for unit in ("file", "network", "zlib"))]
    with hidden_paths(assets):
        for runtime in ("beam",):
            c = Compiler(prefix, work / f"help-{runtime}", runtime)
            stdout, stderr = run([*c.command, "help", "compile"], c.work, c.env)
            check("compile" in stdout and "--depfile" in stdout and not stderr,
                  f"command help requires runtime files: {stdout!r} {stderr!r}")


from support.scratch import scratch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", required=True, type=Path)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--project-source-dir", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args(), private=True)
    build, root = args.build_dir.resolve(), args.test_dir.resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    with scratch(root):
        env = isolated_environment(root)
        stage = root / "stage"
        stage_installation(args, stage)
        check_prefix(stage, root / "fresh", args.cmake, build, args.source_dir, args.project_source_dir)
        inactive_and_help(stage, root / "fresh-independent")
        # Reinstallation preserves the independently owned compiler inventory.
        owned_compiler = stage / "lib/beam/tools/compiler"
        owned_expected = {path.relative_to(owned_compiler): path.read_bytes()
                          for path in owned_compiler.rglob("*") if path.is_file()}
        check(owned_expected, "owned compiler installation is empty")
        stage_installation(args, stage)
        check({path.relative_to(owned_compiler): path.read_bytes()
               for path in owned_compiler.rglob("*") if path.is_file()} == owned_expected,
              "incremental installation changed the owned compiler inventory")
        for runtime in ("beam",):
            c = Compiler(stage, root / "incremental" / runtime, runtime)
            source = module(c.work / "incremental.erl", "incremental")
            c.compile("--deterministic", "--depfile", c.work / "incremental.d", source)
            c.value(c.work / "incremental.beam", "incremental")
        relocated = root / "relocated prefix κ"
        stage.rename(relocated)
        check_prefix(relocated, root / "relocated", args.cmake, build, args.source_dir, args.project_source_dir)
        inactive_and_help(relocated, root / "relocated-independent")
        print("installed compile acceptance passed", flush=True)


if __name__ == "__main__":
    main()
