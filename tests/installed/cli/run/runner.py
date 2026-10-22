#!/usr/bin/env python3
"""Installed BEAM source execution without OTP applications or shell tooling."""

from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import json
import os
import re
import select
import shutil
import subprocess
import time

from support.bytecode import RETURNING_ASSETS, copy_bytecode
from support.files import hidden
from support.process import child_process, run as capture, isolated_environment, check


def run(command, cwd, env, status=0, timeout=60, stdin=None):
    result = capture(command, cwd=cwd, env=env, timeout=timeout, status=status, input_data=stdin, text=True)
    return result.stdout, result.stderr


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def erl(value):
    return json.dumps(str(value), ensure_ascii=False)


def compile_sources(prefix, sources, output, work, env):
    output.mkdir(parents=True, exist_ok=True)
    stdout, stderr = run([prefix / "bin/beam", "compile", "--deterministic",
                          "-o", output, "--", *sources], work, env)
    check(not stdout and not stderr, f"fixture compilation diagnostics: {stdout!r} {stderr!r}")


def source_run(prefix, source, args, work, env, status=0):
    return run([prefix / "bin/beam", "run", source, "--", *args],
               work, env, status=status)


def input_case(prefix, source, work, env):
    command = [str(prefix / "bin/beam"), "run", str(source), "--", "input"]
    with child_process(command, cwd=work, env=env, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        observed = b""
        deadline = time.monotonic() + 30
        while b"reader live\n" not in observed:
            remaining = deadline - time.monotonic()
            check(remaining > 0, "blocked source input prevented concurrent child output")
            ready, _, _ = select.select([child.stdout], [], [], remaining)
            check(ready, "blocked source input prevented concurrent child output")
            part = os.read(child.stdout.fileno(), 4096)
            check(part, f"source input exited before child output: {observed!r}")
            observed += part
        tail, error = child.communicate("日本語\nabc\n".encode(), timeout=30)
        check(child.returncode == 0 and not error and
              observed + tail == b"prompt>reader live\ninput ok\n",
              f"source input/EOF: exit={child.returncode} out={observed + tail!r} err={error!r}")


def input_chunks_case(prefix, source, work, env):
    command = [str(prefix / "bin/beam"), "run", str(source), "--", "input_chunks"]
    with child_process(command, cwd=work, env=env, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        observed = b""

        def send(data):
            child.stdin.write(data)
            child.stdin.flush()

        def expect(expected):
            nonlocal observed
            deadline = time.monotonic() + 30
            while len(observed) < len(expected):
                remaining = deadline - time.monotonic()
                check(remaining > 0, f"input chunk acknowledgement timed out: {observed!r}")
                ready, _, _ = select.select([child.stdout], [], [], remaining)
                check(ready, f"input chunk acknowledgement timed out: {observed!r}")
                part = os.read(child.stdout.fileno(), 4096)
                check(part, f"input chunk reader exited early: {observed!r}")
                observed += part
            check(observed == expected, f"input chunk ordering/decoding: {observed!r}, expected {expected!r}")

        # The pipe remains open: a character read must not wait for a line.
        send(b"A")
        expect(b"ascii\n")
        send(b"\x03")
        expect(b"ascii\ncontrol\n")
        send(b"\xce")
        ready, _, _ = select.select([child.stdout], [], [], 0.05)
        check(not ready, "incomplete UTF-8 produced an early acknowledgement")
        send(b"\xbbXY")
        expect(b"ascii\ncontrol\nunicode\nleftover\n")
        send(b"line\ncrlf\r\n")
        expect(b"ascii\ncontrol\nunicode\nleftover\nlines\n")
        # Valid characters preceding malformed input remain consumable.
        send(b"Z\xff")
        expect(b"ascii\ncontrol\nunicode\nleftover\nlines\nprefix\ninvalid\n")
        send(b"Q\xe2\x82")
        expect(b"ascii\ncontrol\nunicode\nleftover\nlines\nprefix\ninvalid\ntail prefix\n")
        child.stdin.close()
        child.stdin = None
        tail, stderr = child.communicate(timeout=30)
        check(child.returncode == 0 and not stderr and tail == b"chunks ok\n",
              f"incomplete UTF-8/EOF: exit={child.returncode} out={tail!r} err={stderr!r}")


def probe_origins(stdout, prefix, source):
    check(stdout.endswith("probe ok\n"), f"source inventory probe did not finish: {stdout!r}")
    runner = prefix / "lib/beam/runtime/beam/run/ebin"
    common = prefix / "lib/beam/runtime/beam/cli/ebin"
    core = {"erlang", "erts_internal", "erts_code_purger", "erts_literal_area_collector",
            "erts_trace_cleaner", "erts_dirty_process_signal_handler", "atomics", "counters", "persistent_term"}
    base = {"beam", "beam_list", "beam_map", "beam_bytes", "beam_text", "beam_format", "beam_time"}
    components = {
        "file": {"beam_file", "prim_file", "prim_buffer"},
        "network": {"beam_socket", "socket", "socket_registry", "prim_socket", "lists", "maps"},
    }
    expected = {name: f"{prefix}/lib/beam/library/ebin/{name}.beam" for name in base}
    expected.update({p.stem: str(p) for ebin in (runner, common) for p in ebin.glob("*.beam")})
    ordinary = {
        "boot": {"beam_return_boot", "beam_boot"},
        "services": {"beam_component_services", "beam_host", "beam_output", "beam_code"},
    }
    for inventory, names in ordinary.items():
        expected.update({name: f"{prefix}/lib/beam/{inventory}/ebin/{name}.beam" for name in names})
    for unit, names in components.items():
        expected.update({name: f"{prefix}/lib/libbeam-{unit}.so#{name}" for name in names})
    expected.update(zlib=str(prefix / "lib/libbeam-zlib.so") + "#zlib", run_probe=str(source))
    actual = {}
    for line in stdout.splitlines()[:-1]:
        check(line.startswith("origin "), f"unexpected probe output: {line!r}")
        _, name, origin = line.split(" ", 2)
        check(name not in actual, f"duplicate loaded origin for {name}")
        actual[name] = origin
        check(name not in core and expected.get(name) == origin,
              f"unexpected module origin: {name} -> {origin}; expected {expected.get(name)!r}")
    check({"run_probe", "beam_source_run", "io", "beam_run_io"} <= actual.keys(),
          "runner inventory omitted source or I/O modules")


def compiler_boundary(prefix, work, env):
    marker = work / "transform effects"
    transform = write(work / "run_boundary_transform.erl", f'''
-module(run_boundary_transform).
-export([parse_transform/2]).
parse_transform(Forms, _) ->
    undefined = whereis(code_server),
    false = erlang:module_loaded(beam_source_run),
    persistent_term:put(run_compiler_state, compiler),
    Parent = self(),
    true = register(run_compiler_worker, spawn(fun() ->
        Table = ets:new(run_compiler_table, [public,named_table]),
        true = ets:insert(Table, {{state,compiler}}),
        Parent ! compiler_ready,
        receive stop -> ok end
    end)),
    receive compiler_ready -> ok after 3000 -> error(compiler_timeout) end,
    ok = file:write_file({erl(marker)}, <<"called\\n">>, [append]),
    Forms.
''')
    compile_sources(prefix, [transform], work, work, env)
    source = write(work / "run_boundary.erl", f'''
-module(run_boundary).
-compile({{parse_transform,run_boundary_transform}}).
-export([main/1]).
main([]) ->
    absent = persistent_term:get(run_compiler_state, absent),
    undefined = whereis(run_compiler_worker),
    undefined = ets:whereis(run_compiler_table),
    false = erlang:module_loaded(run_boundary_transform),
    false = erlang:module_loaded(beam_compiler_cli),
    {{ok,<<"called\\n">>}} = beam_file:read_file(<<{erl(marker)}/utf8>>, infinity),
    io:put_chars("compiler boundary ok\\n").
''')
    stdout, stderr = source_run(prefix, source, [], work, env)
    check(stdout == "compiler boundary ok\n" and not stderr, "compiler state crossed into source execution")
    check(marker.read_text() == "called\n", "run transform executed more than once")
    check(not source.with_suffix(".beam").exists(), "source run published bytecode")


def code_cases(prefix, work, env):
    helper = write(work / "run_explicit_helper.erl", '''
-module(run_explicit_helper).
-export([value/0]).
value() -> explicit.
''')
    onload_marker = work / "onload-must-not-run"
    onload = write(work / "run_onload_helper.erl", f'''
-module(run_onload_helper).
-on_load(activate/0).
-export([value/0]).
activate() -> beam_file:write_file(<<{erl(onload_marker)}/utf8>>, <<"loaded">>, infinity).
value() -> onload.
''')
    compile_sources(prefix, [helper, onload], work, work, env)
    directory = work / "directory.beam"
    directory.mkdir()
    source = write(work / "run_explicit.erl", f'''
-module(run_explicit).
-export([main/1]).
main([]) ->
    undef = try run_explicit_helper:value() catch error:undef -> undef end,
    false = erlang:module_loaded(run_explicit_helper),
    Origin = <<{erl(helper.with_suffix('.beam'))}/utf8>>,
    {{error,#{{kind := invalid_data}}}} = beam_code:fetch(<<"relative.beam">>,run_explicit_helper,infinity),
    {{error,#{{kind := invalid_data}}}} = beam_code:fetch(<<Origin/binary,0>>,run_explicit_helper,infinity),
    {{error,#{{kind := invalid_data}}}} = beam_code:fetch(<<"/",16#ff>>,run_explicit_helper,infinity),
    {{error,#{{kind := invalid_data}}}} = beam_code:fetch(<<{erl(directory)}/utf8>>,run_explicit_helper,infinity),
    {{ok,Bytes}} = beam_code:fetch(Origin,run_explicit_helper,infinity),
    ok = beam_code:load(run_explicit_helper,Bytes,Origin),
    explicit = run_explicit_helper:value(),
    {{error,#{{kind := invalid_data}}}} = beam_code:load(wrong_identity,Bytes,Origin),
    explicit = run_explicit_helper:value(),
    {{ok,OnLoad}} = beam_code:fetch(<<{erl(onload.with_suffix('.beam'))}/utf8>>,run_onload_helper,infinity),
    {{error,#{{reason := on_load}}}} = beam_code:load(run_onload_helper,OnLoad,Origin),
    false = erlang:module_loaded(run_onload_helper),
    {{error,#{{kind := not_found}}}} = beam_code:fetch(<<{erl(work / 'missing.beam')}/utf8>>,missing,infinity),
    io:put_chars("explicit code ok\\n").
''')
    stdout, stderr = source_run(prefix, source, [], work, env)
    check(stdout == "explicit code ok\n" and not stderr, f"explicit code behavior: {stdout!r} {stderr!r}")
    check(not onload_marker.exists(), "explicit code loading executed rejected on_load")


def failure_cases(prefix, source, work, env):
    stdout, stderr = source_run(prefix, source, ["exception"], work, env, status=1)
    check(not stdout and "source_run_exception" in stderr and str(source) in stderr,
          "source exception lost context or destination")
    for mode, status in (("halt", 7), ("finish", 6)):
        stdout, stderr = source_run(prefix, source, [mode], work, env, status)
        check(not stdout and not stderr, f"{mode} produced diagnostics")
    missing = work / "missing.erl"
    stdout, stderr = source_run(prefix, missing, [], work, env, status=1)
    check(stdout == f"{missing}: no such file or directory\n" and not stderr,
          "missing-source diagnostic changed")
    no_main = write(work / "run_no_main.erl", "-module(run_no_main).\n-export([value/0]).\nvalue() -> ok.\n")
    stdout, stderr = source_run(prefix, no_main, [], work, env, status=1)
    check(not stdout and "undef" in stderr, "missing main did not report undef")
    marker = work / "source-onload-must-not-run"
    onload = write(work / "run_source_onload.erl", f'''
-module(run_source_onload).
-on_load(activate/0).
-export([main/1]).
activate() -> beam_file:write_file(<<{erl(marker)}/utf8>>, <<"loaded">>, infinity).
main(_) -> io:put_chars("must not run\\n").
''')
    stdout, stderr = source_run(prefix, onload, [], work, env, status=1)
    check(not stdout and "on_load" in stderr and not marker.exists(), "source on_load was not rejected")
    collision = write(work / "io.erl", '-module(io).\n-export([main/1]).\nmain(_) -> erlang:halt(23).\n')
    stdout, stderr = source_run(prefix, collision, [], work, env, status=1)
    check(not stdout and stderr, "source replaced runner io module")


def prefix_cases(prefix, fixture, phase, root, hello_fixture):
    work = root / f"{phase} unrelated work λ"
    work.mkdir()
    env = isolated_environment(work)
    hello = work / "hello_world.erl"
    shutil.copyfile(hello_fixture, hello)
    stdout, stderr = source_run(prefix, hello, [], work, env)
    check(stdout == "Hello, World!\n" and not stderr,
          "protected Hello World changed in the source-run composition")
    source = work / "run_probe.erl"
    shutil.copyfile(fixture, source)
    stdout, stderr = source_run(prefix, source, ["probe", str(prefix), str(source)], work, env)
    check(not stderr, f"probe stderr: {stderr}")
    probe_origins(stdout, prefix, source)
    stdout, stderr = source_run(prefix, source, ["io"], work, env)
    check(stdout == "Unicode 日本語 λ 42 {ok,[1,2]}\nchild output\n" and stderr == "error é\n",
          f"source I/O changed: {stdout!r} {stderr!r}")
    input_case(prefix, source, work, env)
    input_chunks_case(prefix, source, work, env)
    stdout, stderr = run([prefix / "bin/beam", "run", source,
                          "--", "io_options"], work, env, stdin=b"\xe9\n")
    check(stdout == "é\noptions ok\n" and not stderr,
          f"I/O options or invalid request behavior changed: {stdout!r} {stderr!r}")
    literal = ["literal", "-eval", "erlang:halt(42).", "--", "", "two words", "a'b", "+S", "日本語"]
    stdout, stderr = source_run(prefix, source, literal, work, env)
    check(stdout == "literal ok\n" and not stderr, "source arguments were interpreted as options")
    payload = work / "service payload 日本語"
    stdout, stderr = source_run(prefix, source, ["services", str(payload)], work, env)
    check(stdout == "services ok\n" and not stderr, f"source services failed: {stdout!r} {stderr!r}")
    check(payload.read_bytes() == "file 日本語\n".encode(), "file service did not persist exact bytes")
    failure_cases(prefix, source, work, env)
    check(not list(work.glob("*.beam")), "source run published .beam files")
    compiler_boundary(prefix, work, env)
    code_cases(prefix, work, env)
    return source, work, env


def reduce_prefix(prefix):
    for suffix in ("lib/beam/ebin",):
        shutil.rmtree(prefix / suffix)
    shutil.rmtree(prefix / "lib/beam/runtime/beam/interactive")
    ebin = prefix / "lib/beam/runtime/beam/ebin"
    for path in ebin.iterdir():
        path.unlink()
    check({path.name for path in ebin.iterdir()} == set(), "transitional runtime files remain")


def missing_assets(prefix, source, work, env):
    runner = prefix / "lib/beam/runtime/beam/run"
    common = prefix / "lib/beam/runtime/beam/cli"
    paths = [runner, common, prefix / "lib/beam/tools/compiler", prefix / "lib/beam/boot",
             prefix / "lib/libbeam-file.so", prefix / "lib/libbeam-network.so",
             prefix / "lib/libbeam-zlib.so"]
    paths.extend(sorted((runner / "ebin").glob("*.beam")))
    paths.extend(sorted((common / "ebin").glob("*.beam")))
    # lists/maps are supplied by the network component in the full service recipe.
    for path in paths:
        if path.name in {"lists.beam", "maps.beam"}:
            continue
        with hidden(path):
            stdout, stderr = source_run(prefix, source, ["io"], work, env, status=1)
            check(not stdout and stderr, f"missing {path} did not fail before source execution")
    bytecode = runner / "ebin/beam_source_run.beam"
    saved = bytecode.read_bytes()
    try:
        for label, data in (("corrupt bytes", b"not a BEAM module"),
                            ("mismatched module identity", (work / "run_explicit_helper.beam").read_bytes())):
            bytecode.write_bytes(data)
            stdout, stderr = source_run(prefix, source, ["io"], work, env, status=1)
            check(not stdout and stderr, f"runner accepted {label}")
    finally:
        bytecode.write_bytes(saved)


def lifecycle(args, prefix, root, env):
    fixtures = root / "host fixtures"
    compile_sources(prefix, [args.source_dir / "run_probe.erl",
                             args.source_dir / "run_prestart_rejected.erl"], fixtures, root, env)
    client = root / "client"
    run([args.cmake, "-S", args.source_dir, "-B", client,
         f"-DCMAKE_PREFIX_PATH={prefix}", f"-DPRIVATE_HOST_BUILD={args.build_dir}",
         f"-DBEAM_TEST_SUPPORT_DIR={args.project_source_dir / 'tests/support'}",
         f"-DBEAM_PROJECT_SOURCE_DIR={args.project_source_dir}"], root, env)
    run([args.cmake, "--build", client, "--parallel", "2"], root, env)
    # The host is linked with a relocatable RPATH and copied into each deployment.
    host = prefix / "bin/source_run_acceptance"
    shutil.copy2(client / "source_run_acceptance", host)
    command = [host, prefix, fixtures / "run_probe.beam", str(fixtures / "run_probe.erl"), root / "retained-file"]
    stdout, stderr = run(command, root, env, timeout=180)
    check(not stderr and re.fullmatch(r"source runner lifetimes ok: threads=[1-9][0-9]* descriptors=[0-9]+ files=0 sockets=0 streams=0\n", stdout),
          f"runner lifecycle failed: {stdout!r} {stderr!r}")
    minimal = root / "minimal output prefix"
    (minimal / "bin").mkdir(parents=True)
    (minimal / "lib").mkdir()
    copy_bytecode(prefix, minimal)
    shutil.copy2(host, minimal / "bin/source_run_acceptance")
    for name in ("libbeam.so",):
        shutil.copy2(prefix / "lib" / name, minimal / "lib" / name)
    runner = Path("lib/beam/runtime/beam/run")
    common = Path("lib/beam/runtime/beam/cli")
    for inventory in (runner, common):
        shutil.copytree(prefix / inventory, minimal / inventory)
    expected = set(RETURNING_ASSETS) | {"bin/source_run_acceptance", "lib/libbeam.so"} | {
        str(inventory / path.relative_to(prefix / inventory))
        for inventory in (runner, common) for path in (prefix / inventory).rglob("*") if path.is_file()}
    actual = {str(path.relative_to(minimal)) for path in minimal.rglob("*") if path.is_file()}
    check(actual == expected, "output-only deployment contains extra runtime assets")
    isolated = env.copy()
    isolated["PATH"] = str(root / "empty path")
    command[0], command[1], command[-1] = minimal / "bin/source_run_acceptance", minimal, "minimal"
    stdout, stderr = run(command, root, isolated)
    check(not stderr and stdout.startswith("source runner lifetimes ok:"), "output-only runner failed")
    for name in ("lists", "maps"):
        with hidden(minimal / common / "ebin" / f"{name}.beam"):
            _, stderr = run(command, root, isolated, status=1)
            check(stderr, f"missing output-only {name} fallback was ignored")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--project-source-dir", type=Path, required=True)
    parser.add_argument("--hello-fixture", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args(), private=True)
    args.test_dir = args.test_dir.resolve()
    args.source_dir = args.source_dir.resolve()
    if args.test_dir.exists():
        shutil.rmtree(args.test_dir)
    args.test_dir.mkdir(parents=True)
    env = isolated_environment(args.test_dir)
    stage = args.test_dir / "stage"
    stage_installation(args, stage)
    fixture = args.source_dir / "run_probe.erl"
    prefix_cases(stage, fixture, "fresh", args.test_dir, args.hello_fixture)
    reduce_prefix(stage)
    prefix_cases(stage, fixture, "fresh reduced", args.test_dir, args.hello_fixture)
    prefix = args.test_dir / "relocated reduced prefix κ"
    stage.rename(prefix)
    source, work, isolated = prefix_cases(prefix, fixture, "relocated", args.test_dir, args.hello_fixture)
    missing_assets(prefix, source, work, isolated)
    lifecycle(args, prefix, args.test_dir, env)
    print("installed source run: fresh, relocated, reduced, and returning lifetimes passed")


if __name__ == "__main__":
    main()
