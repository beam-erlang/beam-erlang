#!/usr/bin/env python3
"""Independent installed components, with neither boot framework deployed."""
from functools import partial
from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil
import os

from support.bytecode import TEST_SUPPORT_DIR, BASE_ASSETS, BOOT_ASSETS, SERVICES_ASSETS, copy_bytecode
from support.process import run as capture, isolated_environment


def run(command, env, cwd, status=0, timeout=120):
    return capture(command, env=env, cwd=cwd, timeout=timeout, status=status, text=True, errors="replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--file-fault-component", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    args.test_dir = args.test_dir.resolve()
    args.source_dir = args.source_dir.resolve()
    shutil.rmtree(args.test_dir, ignore_errors=True)
    args.test_dir.mkdir(parents=True)
    build_work = args.test_dir / "build-work"
    build_work.mkdir()
    env = isolated_environment(build_work)
    execute = partial(run, env=env, cwd=build_work)
    prefix = args.test_dir / "sdk"
    stage_installation(args, prefix)
    bytecode = args.test_dir / "bytecode"
    bytecode.mkdir()
    execute([str(prefix / "bin/beam"), "compile", "-o", str(bytecode),
         "--", str(args.source_dir / "fixtures/component_worker.erl")])
    for name in ("facility_worker", "facility_helper", "contract_worker"):
        execute([str(prefix / "bin/beam"), "compile", "-o", str(bytecode),
                 "--", str(args.source_dir / "fixtures" / f"{name}.erl")])
    client = args.test_dir / "client"
    execute([args.cmake, "-S", str(args.source_dir), "-B", str(client),
         f"-DCMAKE_PREFIX_PATH={prefix}", f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}"])
    execute([args.cmake, "--build", str(client)])
    for profile, units in (("file", ("file",)), ("network", ("network",)),
                           ("combined", ("file", "network"))):
        stage = args.test_dir / "stage"
        (stage / "bin").mkdir(parents=True)
        (stage / "lib").mkdir()
        copy_bytecode(prefix, stage, BASE_ASSETS + SERVICES_ASSETS)
        shutil.copy2(client / "components_acceptance", stage / "bin/host")
        shutil.copy2(prefix / "lib/libbeam.so", stage / "lib/libbeam.so")
        for unit in units:
            name = f"libbeam-{unit}.so"
            shutil.copy2(prefix / "lib" / name, stage / "lib" / name)
        deployed = args.test_dir / f"relocated {profile} λ"
        stage.rename(deployed)
        work = args.test_dir / f"unrelated {profile}"
        work.mkdir()
        env = isolated_environment(work)
        env["PATH"] = str(work)
        for fault in (0, 1):
            result = execute([str(deployed / "bin/host"), str(deployed),
                          str(bytecode / "component_worker.beam"), str(work / "output λ"),
                          profile, str(fault)], env=env, cwd=work)
            print(f"{profile} fault={fault}: {result.stdout.strip()}")

    for profile in ("host", "zlib", "program"):
        deployed = args.test_dir / f"relocated facilities {profile} λ"
        (deployed / "bin").mkdir(parents=True)
        (deployed / "lib").mkdir()
        copy_bytecode(prefix, deployed, BASE_ASSETS + SERVICES_ASSETS)
        shutil.copy2(client / "facilities_acceptance", deployed / "bin/host")
        for unit in ("libbeam.so",) + (("libbeam-zlib.so",) if profile == "zlib" else ()):
            shutil.copy2(prefix / "lib" / unit, deployed / "lib" / unit)
        if profile == "program":
            (deployed / "libexec/beam").mkdir(parents=True)
            shutil.copy2(prefix / "libexec/beam/erl_child_setup", deployed / "libexec/beam/erl_child_setup")
        work = args.test_dir / f"unrelated facilities {profile}"
        work.mkdir()
        env = isolated_environment(work)
        env["PATH"] = str(work)
        result = execute([str(deployed / "bin/host"), str(deployed),
                          str(bytecode / "facility_worker.beam"), str(bytecode / "facility_helper.beam"), profile],
                         env=env, cwd=work)
        print(f"{profile}: {result.stdout.strip()}")

    for mode in ("persistent", "faults", "returning", "returning-faults"):
        deployed = args.test_dir / f"relocated contracts {mode} λ"
        (deployed / "bin").mkdir(parents=True)
        (deployed / "lib").mkdir()
        copy_bytecode(prefix, deployed, BASE_ASSETS + SERVICES_ASSETS)
        shutil.copy2(client / "contracts_acceptance", deployed / "bin/host")
        units = ("libbeam.so", "libbeam-file.so", "libbeam-network.so")
        if mode.startswith("returning"):
            copy_bytecode(prefix, deployed, BOOT_ASSETS)
        for unit in units:
            shutil.copy2(prefix / "lib" / unit, deployed / "lib" / unit)
        fault = deployed / "lib/libbeam-file-contract-test.so"
        if "faults" in mode:
            shutil.copy2(args.file_fault_component, fault)
        work = args.test_dir / f"unrelated contracts {mode}"
        work.mkdir()
        path = work / "contract λ"
        invalid = Path(str(path) + ".invalid-dir")
        invalid.mkdir()
        fd = os.open(os.fsencode(invalid) + b"/invalid-\xff", os.O_CREAT | os.O_WRONLY, 0o600)
        os.close(fd)
        valid = Path(str(path) + ".valid-dir")
        valid.mkdir()
        (valid / "valid λ").write_bytes(b"")
        env = isolated_environment(work)
        env["PATH"] = str(work)
        result = execute([str(deployed / "bin/host"), str(deployed),
                          str(bytecode / "contract_worker.beam"), str(path), mode, str(fault)],
                         env=env, cwd=work)
        print(f"contracts {mode}: {result.stdout.strip()}")


if __name__ == "__main__":
    main()
