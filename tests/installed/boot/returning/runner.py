#!/usr/bin/env python3
"""Installed returning-boot acceptance. Uses only the shipped prefix."""
from functools import partial
from pathlib import Path
from support.scratch import scratch
from support.artifacts import artifact, relative as artifact_relative
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil

from support.bytecode import BASE_ASSETS, RETURNING_ASSETS, TEST_SUPPORT_DIR, copy_bytecode
from support.process import run as capture, isolated_environment


def run(command, env, cwd, status=0, timeout=120):
    return capture(command, env=env, cwd=cwd, timeout=timeout, status=status, text=True, errors="replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--project-source-dir", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    args.test_dir = args.test_dir.resolve()
    args.source_dir = args.source_dir.resolve()
    if args.test_dir.exists():
        shutil.rmtree(args.test_dir)
    args.test_dir.mkdir(parents=True)
    with scratch(args.test_dir):
        build_work = args.test_dir / "build-work"
        build_work.mkdir()
        env = isolated_environment(build_work)
        execute = partial(run, env=env, cwd=build_work)
        stage = args.test_dir / "stage"
        prefix = args.test_dir / "relocated returning prefix"
        stage_installation(args, stage)
        stage.rename(prefix)
        fixtures = args.test_dir / "fixtures"
        fixtures.mkdir()
        work = args.test_dir / "work"
        work.mkdir()
        env = isolated_environment(work)
        execute = partial(run, env=env, cwd=work)
        for name in ("returning_programs", "kept", "uppercase_transform", "onload_rejected"):
            source = args.source_dir / "fixtures" / f"{name}.erl"
            execute([str(prefix / "bin/beam"), "compile",
                 "-o", str(fixtures), "--", str(source)])
        client = args.test_dir / "client"
        execute([args.cmake, "-S", str(args.source_dir), "-B", str(client),
             f"-DCMAKE_PREFIX_PATH={prefix}", f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}"])
        execute([args.cmake, "--build", str(client)])
        executable = client / "returning_acceptance"
        shutil.copyfile(client / "mismatched-component.so", fixtures / "mismatched-component.so")
        modes = ("pin", "components", "library", "completion", "greeting", "counter", "code",
                 "file", "echo", "record", "zlib", "output")
        for mode in modes:
            for _pass in (1, 2):
                result = execute([str(executable), mode, str(prefix), str(fixtures), str(work)],
                                 timeout=30 if mode in ("counter", "echo", "record") else 180)
                if result.stdout:
                    print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")

        profiles = {
            "base": ((), ("library",)),
            "boot": ((), ("greeting", "counter", "code", "completion", "output")),
            "file": (("file",), ("file", "record")),
            "network": (("network",), ("echo",)),
        }
        for profile, (units, cases) in profiles.items():
            deployment = args.test_dir / f"minimal-{profile}"
            (deployment / "bin").mkdir(parents=True)
            (deployment / "lib").mkdir()
            shutil.copyfile(artifact(prefix,"runtime"), deployment / artifact_relative(prefix,"runtime"))
            host = deployment / "bin/returning_acceptance"
            shutil.copyfile(client / "returning_minimal", host)
            host.chmod(0o755)
            assets = BASE_ASSETS if profile == "base" else RETURNING_ASSETS
            copy_bytecode(prefix, deployment, assets)
            if units:
                directory = deployment / "lib"
                for unit in units:
                    name = artifact(prefix,"library." + unit + ".shared").name
                    shutil.copyfile(prefix / "lib" / name, directory / name)
            expected = {"bin/returning_acceptance", artifact_relative(prefix,"runtime")} | set(assets) | {
                artifact_relative(prefix,"library." + unit + ".shared") for unit in units}
            actual = {path.relative_to(deployment).as_posix()
                      for path in deployment.rglob("*") if path.is_file()}
            if actual != expected:
                raise RuntimeError(f"minimal deployment is not self-contained: {actual}")
            deployed_work = args.test_dir / f"minimal-{profile}-work"
            deployed_work.mkdir()
            deployed_env = isolated_environment(deployed_work)
            deployed_env["PATH"] = str(deployed_work)
            for case in cases:
                result = execute([str(host), case, str(deployment), str(fixtures), str(deployed_work)],
                             env=deployed_env, cwd=deployed_work,
                             timeout=30 if case in ("counter", "echo", "record") else 180)
                print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
