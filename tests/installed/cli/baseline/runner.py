#!/usr/bin/env python3
"""Acceptance checks through the installed CLI, including a real terminal."""


from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import re
import shutil

from support.process import run as capture, isolated_environment, check

from ..scenarios.provenance import (
    beam_provenance_expression,
)
from ..scenarios.terminal import (
    check_terminal,
)


TIMEOUT = 10


def run(command, cwd, env, input_data=None, status=0):
    result = capture(command, cwd=cwd, env=env, timeout=TIMEOUT, status=status, input_data=input_data)
    return result.stdout, result.stderr


def check_beam_prefix(prefix, work, fixture, artifacts):
    """Keep the CLI contract while checking the BEAM-owned composition."""
    work.mkdir()
    env = isolated_environment(work)
    beam = prefix / "bin/beam"
    command = [str(beam)]
    selected = prefix / "lib/beam/runtime/beam"
    family_ebin = selected / "ebin"
    service_modules = {"prim_zip", "erl_tracer"}
    expected = {name + ".beam" for name in service_modules}
    check(set(artifacts) == expected, "wrong BEAM artifact recipe")
    check({path.name for path in family_ebin.iterdir()} == expected,
          "wrong installed BEAM module inventory")
    for name, content in artifacts.items():
        check((family_ebin / name).read_bytes() == content,
              f"installed {name} differs from its build artifact")
    interactive = selected / "interactive/ebin"
    check(beam.read_bytes()[:4] == b"\x7fELF", "installed beam is not a native executable")
    shutil.copyfile(fixture, work / "hello_world.erl")
    out, err = run([*command, "run", "hello_world.erl"], work, env)
    check(out == b"Hello, World!\n" and err == b"", "BEAM source contract changed")

    expression = beam_provenance_expression(prefix)
    out, err = run([*command, "eval", expression], work, env)
    check(out == err == b"", f"unexpected BEAM provenance output: {out!r}, {err!r}")
    extra = interactive / "unrelated_corrupt.beam"
    extra.write_bytes(b"not a BEAM module")
    try:
        out, err = run([*command, "eval", "false = erlang:module_loaded(unrelated_corrupt)."], work, env)
        check(out == err == b"", "preparation acquired an unrelated library file")
    finally:
        extra.unlink()
    # Exercise the selected native program service through the CLI.
    expression = (
        'Port = erlang:open_port({spawn_executable,"/bin/sh"}, '
        '[binary,use_stdio,exit_status,{args,["-c","printf program-ok"]}]), '
        'Collect = fun Loop(Acc) -> receive '
        '{Port,{data,Bytes}} -> Loop(<<Acc/binary,Bytes/binary>>); '
        '{Port,{exit_status,0}} -> Acc after 3000 -> error(program_timeout) end end, '
        '<<"program-ok">> = Collect(<<>>).'
    )
    out, err = run([*command, "eval", expression], work, env)
    check(out == err == b"", f"unexpected BEAM program output: {out!r}, {err!r}")
    out, err = run([*command, "help", "shell"], work, env)
    check(out == b"usage: beam shell\n" and err == b"", "shell help changed")
    out, err = run([*command, "help"], work, env)
    check(b"  shell " in out and err == b"", "shell missing from command catalog")
    out, err = run([*command, "shell", "unexpected"], work, env, status=1)
    check(out == b"" and err == b"usage: beam shell\n",
          "extra shell arguments accepted")
    for arguments in (["shell"], []):
        check_terminal(beam, arguments, work, env, prefix)
        out, err = run([*command, *arguments], work, env,
                       input_data=b'io:format("~p~n", [6 * 7]).\n')
        check(re.search(rb"(?:^|\n)(?:\d+> )?42\n", out) is not None,
              f"piped expression was not evaluated: {out!r}, {err!r}")
        run([*command, *arguments], work, env, input_data=b"erlang:halt(7).\n", status=7)
    for arguments in (["--runtime="], ["--runtime=unknown"],
                      ["--runtime=otp", "--runtime=beam", "version"],
                      ["--runtime=beam", "--runtime=beam", "version"],
                      ["version", "--runtime=beam"], ["--runtime", "beam", "version"],
                      ["shell", "--runtime=beam"], ["eval", "ok.", "--runtime=otp"],
                      ["run", "hello_world.erl", "--runtime=beam"],
                      ["--boot=returning", "eval", "ok."],
                      ["--runtime=beam", "--boot=returning", "eval", "ok."],
                      ["--runtime=otp", "--boot=returning", "eval", "ok."]):
        run([str(beam), *arguments], work, env, status=1)
    literal = work / "literal_selector.erl"
    literal.write_text('-module(literal_selector).\n-export([main/1]).\n'
                       'main(["--runtime=beam","--runtime=otp","--boot=returning",""]) -> ok.\n')
    out, err = run([*command, "run", str(literal), "--", "--runtime=beam", "--runtime=otp",
                    "--boot=returning", ""], work, env)
    check(out == err == b"", "selector-looking application data changed")
    check(not (prefix / "lib/beam/runtime/otp").exists(), "OTP assets remain installed")
    out, err = run([*command, "run", "hello_world.erl"], work, env)
    check(out == b"Hello, World!\n" and err == b"", "BEAM requires OTP assets")
    check(not list(work.rglob("*.beam")), "runtime commands wrote compiled BEAM files")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--module-artifacts", type=Path, nargs="+", required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    test_dir = args.test_dir.resolve()
    if test_dir.exists():
        shutil.rmtree(test_dir)
    test_dir.mkdir(parents=True)
    prefix = test_dir / "stage"
    stage_installation(args, prefix)
    print("Checking fresh installation", flush=True)
    artifacts = {p.name: p.read_bytes() for p in args.module_artifacts}
    check_beam_prefix(prefix, test_dir / "fresh-work", args.fixture, artifacts)
    relocated = test_dir / "relocated préfix 日本"
    prefix.rename(relocated)
    print("Checking relocated installation", flush=True)
    check_beam_prefix(relocated, test_dir / "relocated-work", args.fixture, artifacts)
    print("Installed runtime contracts passed", flush=True)


if __name__ == "__main__":
    main()
