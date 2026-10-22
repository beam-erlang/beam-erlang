#!/usr/bin/env python3
"""Exercise the installed bare C API without external Erlang runtime files."""
from functools import partial
from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import shutil

from support.bytecode import TEST_SUPPORT_DIR, BASE_ASSETS, copy_base
from support.process import run as capture, isolated_environment, check
from support.terminal import capture_terminal


def run(*args, env, cwd):
    return capture(args, env=env, cwd=cwd, timeout=60, text=True)


def require_host(executable, *args, env, cwd):
    result = run(str(executable), *args, env=env, cwd=cwd)
    if result.stdout != "Bare VM stopped; host continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected bare host output: {result.stdout!r} {result.stderr!r}")


def run_terminal(executable, env, cwd):
    result = capture_terminal([str(executable)], cwd=cwd, env=env, timeout=60)
    expected = b"Bare VM stopped; host continued.\r\n"
    check(result.returncode == 0 and result.stdout == expected,
          f"bare terminal host: exit={result.returncode}, output={result.stdout!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--linkage", choices=("shared", "static"), default="shared")
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    args.test_dir = args.test_dir.resolve()
    args.source_dir = args.source_dir.resolve()
    if args.test_dir.exists():
        shutil.rmtree(args.test_dir)
    args.test_dir.mkdir(parents=True)
    build_work = args.test_dir / "build-work"
    build_work.mkdir()
    env = isolated_environment(build_work)
    execute = partial(run, env=env, cwd=build_work)
    stage = args.test_dir / "stage"
    prefix = args.test_dir / "relocated bare prefix"
    stage_installation(args, stage)
    stage.rename(prefix)

    generated = args.test_dir / "fixtures"
    generated.mkdir()
    header = ["/* Compiled acceptance fixtures; generated in the test build directory. */"]
    for name in ("bare_fixture", "bare_second", "bare_on_load", "bare_bridge",
                 "bare_observe", "bare_io", "bare_management", "bare_scheduler_stress",
                 "bare_prim_eval", "base_contract", "base_conflict"):
        source = args.source_dir / "fixtures" / f"{name}.erl"
        module = "beam_map" if name == "base_conflict" else name
        if name == "base_conflict":
            source = generated / "beam_map.erl"
            shutil.copyfile(args.source_dir / "fixtures" / "base_conflict.erl", source)
        execute(str(prefix / "bin/beam"), "compile",
            "-o", str(generated), "--", str(source))
        data = (generated / f"{module}.beam").read_bytes()
        header.append(f"static const unsigned char {name}[] = {{")
        for offset in range(0, len(data), 16):
            header.append(",".join(str(b) for b in data[offset:offset + 16]) + ",")
        header.append("};")
    for name, generation in ((name, generation) for name in ("bare_lifecycle", "bare_managed")
                             for generation in (1, 2)):
        output = generated / f"{name}_{generation}.beam"
        execute(str(prefix / "bin/beam"), "compile",
            "-o", str(generated), f"-DGENERATION={generation}", "--",
            str(args.source_dir / "fixtures" / f"{name}.erl"))
        (generated / f"{name}.beam").rename(output)
        data = output.read_bytes()
        header.append(f"static const unsigned char {name}_{generation}[] = {{")
        for offset in range(0, len(data), 16):
            header.append(",".join(str(b) for b in data[offset:offset + 16]) + ",")
        header.append("};")
    (generated / "fixtures.h").write_text("\n".join(header) + "\n")
    client = args.test_dir / "client"
    execute(args.cmake, "-S", str(args.source_dir), "-B", str(client),
        f"-DCMAKE_PREFIX_PATH={prefix}",
        f"-DRUNTIME_COMPONENT={'RuntimeStatic' if args.linkage == 'static' else 'Runtime'}", f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}", f"-DFIXTURE_INCLUDE={generated}",
        f"-DPRIM_EVAL_MODULE={prefix / 'lib/beam/runtime/beam/interactive/ebin/prim_eval.beam'}",
        f"-DCLI_EBIN_DIR={prefix / 'lib/beam/runtime/beam/cli/ebin'}")
    execute(args.cmake, "--build", str(client))

    # The optional base is a separately supplied ordinary bytecode inventory.
    base_component = args.test_dir / "optional base"
    copy_base(prefix, base_component)

    # The SDK has already linked against the relocated library. Remove every
    # external runtime/boot/helper file before executing the embedded modules.
    shutil.rmtree(prefix / "bin")
    shutil.rmtree(prefix / "libexec")
    shutil.rmtree(prefix / "lib/beam")
    for component in (prefix / "lib").glob("libbeam-*.so"):
        component.unlink()
    if args.linkage == "static":
        shutil.rmtree(prefix)
    for beam in generated.glob("*.beam"):
        beam.unlink()
    unrelated = args.test_dir / "unrelated"
    unrelated.mkdir()
    env = isolated_environment(unrelated)
    env["PATH"] = str(unrelated)
    execute = partial(run, env=env, cwd=unrelated)
    for mode in ("normal", "halt", "background", "background-invalid-call",
                 "background-invalid-load"):
        require_host(client / "bare_host", mode, env=env, cwd=unrelated)
    run_terminal(client / "bare_host", env, unrelated)
    if args.linkage == "shared":
        result = execute(str(client / "bare_cxx"), env=env)
        if result.stdout or result.stderr:
            raise RuntimeError(f"unexpected C++ output: {result.stdout!r} {result.stderr!r}")
    result = execute(str(client / "lifecycle_host"), env=env)
    if result.stdout != "Sequential VM lifetimes completed; C continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected lifecycle output: {result.stdout!r} {result.stderr!r}")
    result = execute(str(client / "management_host"), env=env)
    if result.stdout != "Module and scheduler management completed; C continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected management output: {result.stdout!r} {result.stderr!r}")
    result = execute(str(client / "scheduler_stress_host"), env=env)
    if result.stdout != "Concurrent scheduler controls completed; C continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected scheduler stress output: {result.stdout!r} {result.stderr!r}")
    result = execute(str(client / "base_host"), str(base_component), env=env)
    if result.stdout != "Explicit base library completed; C continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected base output: {result.stdout!r} {result.stderr!r}")
    result = execute(str(client / "prim_eval_host"), env=env)
    if result.stdout != "Explicit prim_eval lifetimes completed; C continued.\n" or result.stderr:
        raise RuntimeError(f"unexpected prim_eval output: {result.stdout!r} {result.stderr!r}")
    for mode in ("normal", "no-handler", "invalid-call", "pending", "pending-stop",
                 "pending-dead", "dead-requester", "fairness", "halt-callback",
                 "background", "io"):
        result = execute(str(client / "bridge_host"), mode, env=env)
        if result.stdout != "Host bridge stopped; C continued.\n" or result.stderr:
            raise RuntimeError(f"unexpected bridge host output ({mode}): "
                               f"{result.stdout!r} {result.stderr!r}")


if __name__ == "__main__":
    main()
