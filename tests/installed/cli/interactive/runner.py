#!/usr/bin/env python3
"""Installed BEAM shell and evaluation without OTP applications or compiler tooling."""

from pathlib import Path
from support.scratch import scratch
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

from ..scenarios import terminal, provenance


CLI_MODULES = {"beam_run_io", "error_handler", "io", "io_lib", "io_lib_pretty", "erl_scan",
               "binary", "io_lib_format", "io_lib_fread", "records", "string", "unicode",
               "unicode_util", "lists", "maps"}
INTERACTIVE_MODULES = {"beam_interactive", "shell", "shell_default", "erl_eval", "erl_parse",
                       "erl_pp", "erl_features", "erl_lint", "erl_expand_records", "dict",
                       "digraph", "digraph_utils", "erl_anno", "erl_bits", "erl_error",
                       "erl_internal", "erl_stdlib_errors", "erl_posix_msg", "ets", "eval_bits", "math",
                       "gb_sets", "gb_trees", "orddict", "ordsets", "otp_internal", "proplists",
                       "queue", "sets", "sofs", "prim_eval"}


def run(command, cwd, env, status=0, stdin=None, timeout=60):
    result = capture(command, cwd=cwd, env=env, timeout=timeout, status=status, input_data=stdin)
    return result.stdout, result.stderr


def text(value):
    return json.dumps(str(value), ensure_ascii=False)


def binary(value):
    return f"<<{text(value)}/utf8>>"


def evaluate(prefix, expression, work, env, status=0):
    return run([prefix / "bin/beam", "eval", expression], work, env, status=status)


def language_expression():
    return (
        'F = fun(N) when is_integer(N) -> N + 1 end, 42 = F(41), '
        '150.0 = 1.5e2, 0.01 = 1.0e-2, 255 = 16#ff, 3.0 = math:sqrt(9), '
        'Fac = fun Loop(0) -> 1; Loop(N) -> N * Loop(N-1) end, 120 = Fac(5), '
        '#{answer := 42} = #{answer => F(41)}, <<1,2,3>> = << <<N>> || N <- [1,2,3] >>, '
        '[2,4,6] = [N*2 || <<N>> <= <<1,2,3>>], '
        'caught = try throw(expected) catch throw:expected -> caught after put(after_ran,true) end, '
        'true = erase(after_ran), '
        'Parent = self(), {Child,Ref} = spawn_monitor(fun() -> Parent ! {answer,F(41)} end), '
        'self() ! preserved, 42 = receive {answer,V} -> V after 3000 -> timeout end, '
        'receive {\'DOWN\',Ref,process,Child,normal} -> ok after 3000 -> error(child_timeout) end, '
        'preserved = receive preserved -> preserved after 0 -> missing end, '
        'timeout = receive impossible -> wrong after 5 -> timeout end.'
    )


def service_expression(path):
    return (
        f'Path = {binary(path)}, '
        'ok = beam_file:write_file(Path,<<"file 日本語"/utf8>>,infinity), '
        '{ok,<<"file 日本語"/utf8>>} = beam_file:read_file(Path,infinity), '
        '{ok,F} = beam_file:open(Path,infinity,#{mode => read}), '
        '{ok,<<"file">>} = beam_file:read(F,4,infinity), ok = beam_file:close(F,infinity), '
        '{error,#{kind := invalid_data}} = beam_file:read_file(<<Path/binary,0>>,infinity), '
        '{error,#{kind := invalid_data}} = beam_file:read_file(<<Path/binary,255>>,infinity), '
        'D = beam_time:deadline_after(3000), '
        '{ok,L} = beam_socket:listen(#{addr => {127,0,0,1},port => 0},D), '
        '{ok,#{port := Port}} = beam_socket:sockname(L), '
        '{ok,C} = beam_socket:connect(#{addr => {127,0,0,1},port => Port},D), '
        '{ok,S} = beam_socket:accept(L,D), ok = beam_socket:send(C,<<"ping">>,D), '
        'Collect = fun Loop(Socket,0,Acc) -> Acc; Loop(Socket,N,Acc) -> '
        '{ok,B} = beam_socket:recv(Socket,N,D), Loop(Socket,N-byte_size(B),<<Acc/binary,B/binary>>) end, '
        '<<"ping">> = Collect(S,4,<<>>), ok = beam_socket:send(S,<<"pong">>,D), '
        '<<"pong">> = Collect(C,4,<<>>), '
        'ok = beam_socket:close(C,D), ok = beam_socket:close(S,D), ok = beam_socket:close(L,D), '
        '<<"zip 日本語"/utf8>> = zlib:uncompress(zlib:compress(<<"zip 日本語"/utf8>>)), '
        'P = erlang:open_port({spawn_executable,"/bin/sh"}, '
        '[binary,use_stdio,exit_status,{args,["-c","printf program-ok"]}]), '
        'Wait = fun Loop(Acc) -> receive {P,{data,B}} -> Loop(<<Acc/binary,B/binary>>); '
        '{P,{exit_status,0}} -> Acc after 3000 -> error(program_timeout) end end, '
        '<<"program-ok">> = Wait(<<>>).'
    )


def code_expression(fixture, directory):
    return (
        'false = erlang:module_loaded(interactive_probe), '
        'undef = try interactive_probe:value() catch error:undef -> undef end, '
        'false = erlang:module_loaded(interactive_probe), '
        f'Origin = {binary(fixture)}, '
        '{ok,Bytes} = beam_code:fetch(Origin,interactive_probe,infinity), '
        'ok = beam_code:load(interactive_probe,Bytes,Origin), 42 = interactive_probe:value(), '
        '{interactive_probe,Origin} = lists:keyfind(interactive_probe,1,persistent_term:get(beam_boot_origins)), '
        '{error,#{kind := invalid_data}} = beam_code:load(interactive_probe,<<"bad module">>,<<"bad-origin">>), '
        '42 = interactive_probe:value(), '
        '{interactive_probe,Origin} = lists:keyfind(interactive_probe,1,persistent_term:get(beam_boot_origins)), '
        '{error,#{kind := invalid_data}} = beam_code:fetch(<<"relative.beam">>,interactive_probe,infinity), '
        '{error,#{kind := invalid_data}} = beam_code:fetch(<<Origin/binary,0>>,interactive_probe,infinity), '
        '{error,#{kind := invalid_data}} = beam_code:fetch(<<Origin/binary,255>>,interactive_probe,infinity), '
        f'{{error,#{{kind := invalid_data}}}} = beam_code:fetch({binary(directory)},interactive_probe,infinity).'
    )


def collector_case(prefix, work, env):
    expression = (
        '{error,enotsup} = io:request({get_until,unicode,"",interactive_probe,value,[]}), '
        '{ok,{first,"λ"}} = io:read(""), {ok,second} = io:read(""), io:put_chars("terms\\n"), '
        '{ok,[17,"word"]} = io:fread("","~d ~s"), io:put_chars("fread\\n"), '
        '{ok,Tokens,_} = io:scan_erl_exprs(""), {ok,Exprs} = erl_parse:parse_exprs(Tokens), '
        '{value,5,_} = erl_eval:exprs(Exprs,erl_eval:new_bindings()), io:put_chars("scan\\n"), '
        '{ok,{function,_,value,0,_},_} = io:parse_erl_form(""), io:put_chars("form\\n"), '
        '{error,{collector,{error,collector_failed}}} = io:scan_erl_exprs(standard_io,"",1,'
        '[{reserved_word_fun,fun(_) -> error(collector_failed) end}]), io:put_chars("collector failed\\n"), '
        '{ok,{recovered,ok}} = io:read(""), io:put_chars("recovered\\n"), '
        'ok = io:setopts([binary,{encoding,latin1}]), '
        '{ok,"é"} = io:read(""), io:put_chars("latin term\\n"), '
        '<<"é"/utf8>> = io:get_chars("",1), <<3>> = io:get_chars("",1), '
        'ok = io:setopts([list,{encoding,unicode}]), io:put_chars("raw characters\\n"), '
        'eof = io:read(""), eof = io:fread("","~d"), {eof,_} = io:scan_erl_exprs(""), '
        'io:put_chars("collectors ok\\n").'
    )
    command = [str(prefix / "bin/beam"), "eval", expression]
    with child_process(command, cwd=work, env=env, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        observed = b""

        def send(data):
            child.stdin.write(data)
            child.stdin.flush()

        def expect(marker):
            nonlocal observed
            deadline = time.monotonic() + 30
            while not observed.endswith(marker):
                remaining = deadline - time.monotonic()
                check(remaining > 0, f"interactive collector timeout: {observed!r}")
                ready, _, _ = select.select([child.stdout], [], [], remaining)
                check(ready, f"interactive collector timeout: {observed!r}")
                part = os.read(child.stdout.fileno(), 4096)
                check(part, f"interactive collector exited early: {observed!r}")
                observed += part

        send(b'{first,"\xce')
        ready, _, _ = select.select([child.stdout], [], [], 0.05)
        check(not ready, "incomplete term/Unicode input produced a result")
        send(b'\xbb"}. second. ')
        expect(b"terms\n")
        send(b"17 word\n")
        expect(b"fread\n")
        send(b"2 + 3. ")
        expect(b"scan\n")
        send(b"value() -> 42. ")
        expect(b"form\n")
        send(b"token. ")
        expect(b"collector failed\n")
        send(b"{recovered,ok}. ")
        expect(b"recovered\n")
        send(b'"\xe9". ')
        expect(b"latin term\n")
        send(b"\xe9\x03")
        expect(b"raw characters\n")
        child.stdin.close()
        child.stdin = None
        tail, err = child.communicate(timeout=30)
        check(child.returncode == 0 and not err and
              observed + tail == b"terms\nfread\nscan\nform\ncollector failed\nrecovered\nlatin term\nraw characters\ncollectors ok\n",
              f"interactive collection failed: status={child.returncode} out={observed + tail!r} err={err!r}")


def split_invalid_shell_case(prefix, work, env):
    command = [prefix / "bin/beam", "shell"]
    with child_process(command, cwd=work, env=env, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        observed = b""

        def expect(marker):
            nonlocal observed
            deadline = time.monotonic() + 30
            while marker not in observed:
                remaining = deadline - time.monotonic()
                check(remaining > 0, f"split malformed shell input timeout: {observed!r}")
                ready, _, _ = select.select([child.stdout], [], [], remaining)
                check(ready, f"split malformed shell input timeout: {observed!r}")
                part = os.read(child.stdout.fileno(), 4096)
                check(part, f"shell exited during malformed input recovery: {observed!r}")
                observed += part

        child.stdin.write(b"X=41.\n")
        child.stdin.flush()
        expect(b"41\n")
        child.stdin.write(b"\xffdiscarded")
        child.stdin.flush()
        expect(b"invalid_unicode")
        tail, err = child.communicate(b" fragment\nX+1.\nq().\n", timeout=30)
        out = observed + tail
        check(child.returncode == 0 and not err and
              re.search(rb"(?:^|\n)(?:\d+> )?42\n", out),
              f"split malformed line did not recover: status={child.returncode} out={out!r} err={err!r}")


def prefix_cases(prefix, fixture, work):
    work.mkdir()
    env = isolated_environment(work)
    for name, expected in (("cli", CLI_MODULES), ("interactive", INTERACTIVE_MODULES)):
        inventory = prefix / "lib/beam/runtime/beam" / name
        modules = (inventory / "modules.txt").read_text().splitlines()
        check(len(modules) == len(expected) and set(modules) == expected,
              f"unexpected {name} module inventory")
        check({path.name for path in (inventory / "ebin").iterdir()} ==
              {module + ".beam" for module in expected}, f"unexpected {name} installed artifacts")
    # A same-named local artifact is not permission to autoload it.
    local = work / "interactive_probe.beam"
    shutil.copy2(fixture, local)
    directory = work / "directory.beam"
    directory.mkdir()
    for label, expression in (
            ("provenance", provenance.beam_provenance_expression(prefix)),
            ("interpreted language", language_expression()),
            ("native services", service_expression(work / "payload 日本語")),
            ("explicit code", code_expression(local, directory))):
        out, err = evaluate(prefix, expression, work, env)
        check(not out and not err, f"{label} emitted unexpected output: {out!r} {err!r}")
    out, err = evaluate(prefix, 'io:format("value ~p ~ts~n",[42,<<"λ 日本語"/utf8>>]), '
                        'io:put_chars(standard_error,"diagnostic é\\n").', work, env)
    check(out == "value 42 λ 日本語\n".encode() and err == "diagnostic é\n".encode(),
          f"Unicode evaluation I/O: {out!r} {err!r}")
    for expression in ('1 + .', 'erlang:error(eval_failure).', 'unknown_interactive_helper:missing().',
                       'compile:forms([]).', 'code:which(erlang).', 'file:read_file("missing").',
                       'os:cmd("must not execute").', 'inet:gethostbyname("localhost").'):
        out, err = evaluate(prefix, expression, work, env, status=1)
        check(not out and err, f"failed evaluation omitted its diagnostic: {expression}")
    evaluate(prefix, 'erlang:halt(7).', work, env, status=7)
    collector_case(prefix, work, env)
    split_invalid_shell_case(prefix, work, env)
    for arguments in (["shell"], []):
        terminal.check_terminal(prefix / "bin/beam", arguments, work, env, prefix)
        out, err = run([prefix / "bin/beam", *arguments], work, env,
                       stdin=b'io:format("PIPE ~p~n", [6*7]).\n')
        check(b"PIPE 42\n" in out and not err, f"piped shell/EOF: {out!r} {err!r}")
        out, err = run([prefix / "bin/beam", *arguments], work, env,
                       stdin=b'X=41.\n\xff\nX+1.\nq().\n')
        check(b"invalid_unicode" in out and re.search(rb"(?:^|\n)(?:\d+> )?42\n", out),
              f"piped shell did not recover after malformed input: {out!r} {err!r}")
    for ebin in (prefix / "lib/beam/runtime/beam/cli/ebin", prefix / "lib/beam/runtime/beam/interactive/ebin"):
        extra = ebin / "unrelated_corrupt.beam"
        extra.write_bytes(b"not bytecode")
        try:
            out, err = evaluate(prefix, 'false = erlang:module_loaded(unrelated_corrupt).', work, env)
            check(not out and not err, "unlisted helper was acquired")
        finally:
            extra.unlink()
    check({path.name for path in work.glob("*.beam") if path.is_file()} == {"interactive_probe.beam"},
          "shell/eval published unexpected bytecode")
    return env


def reduce_prefix(prefix):
    for relative in ("lib/beam/ebin",):
        shutil.rmtree(prefix / relative)
    for path in (prefix / "lib/beam/runtime/beam/ebin").iterdir():
        path.unlink()


def command_family_case(prefix, source, work, hello_fixture):
    work.mkdir()
    env = isolated_environment(work)
    output = work / "compiled"
    output.mkdir()
    hello = work / "hello_world.erl"
    shutil.copy2(hello_fixture, hello)
    out, err = run([prefix / "bin/beam", "compile", "-o", output,
                    "--", source / "interactive_probe.erl"], work, env)
    check(not out and not err and (output / "interactive_probe.beam").is_file(),
          f"BEAM-only compiler failed: {out!r} {err!r}")
    out, err = run([prefix / "bin/beam", "run", hello], work, env)
    check(out == b"Hello, World!\n" and not err and not (work / "hello_world.beam").exists(),
          f"BEAM-only unchanged source fixture failed: {out!r} {err!r}")
    out, err = evaluate(prefix, '42 = 6 * 7.', work, env)
    check(not out and not err, "BEAM-only eval failed")
    out, err = run([prefix / "bin/beam", "shell"], work, env,
                   stdin=b'io:put_chars("family shell ok\\n").\nq().\n')
    check(b"family shell ok\n" in out and not err, "BEAM-only shell failed")


def missing_assets(prefix, fixture, work, env):
    root = prefix / "lib/beam/runtime/beam"
    assets = [root / "cli", root / "interactive",
              prefix / "libexec/beam/erl_child_setup",
               *(prefix / "lib" / name for name in
              ("libbeam-file.so", "libbeam-network.so", "libbeam-zlib.so"))]
    assets.extend(prefix / relative for relative in RETURNING_ASSETS)
    assets.extend(root.joinpath("interactive/ebin").glob("*.beam"))
    assets.extend(path for path in root.joinpath("cli/ebin").glob("*.beam") if path.stem not in {"lists", "maps"})
    for asset in assets:
        with hidden(asset):
            try:
                out, err = evaluate(prefix, 'io:put_chars("must not run").', work, env, status=1)
            except AssertionError as error:
                raise AssertionError(f"missing selected asset {asset}: {error}") from error
            check(not out and err, f"missing {asset} did not fail before evaluation")
            run([prefix / "bin/beam", "version"], work, env)
    for name in ("beam_interactive", "prim_eval"):
        target = root / "interactive/ebin" / f"{name}.beam"
        original = target.read_bytes()
        try:
            for malformed in (b"", b"not bytecode", fixture.read_bytes()):
                target.write_bytes(malformed)
                out, err = evaluate(prefix, 'io:put_chars("must not run").', work, env, status=1)
                check(not out and err, f"invalid {name} was accepted")
        finally:
            target.write_bytes(original)


def host_case(prefix, fixture, work, env, minimal=False):
    out, err = run([prefix / "bin/interactive_acceptance", prefix, fixture,
                    "minimal" if minimal else work / "retained 日本語"], work, env, timeout=180)
    check(not err and re.fullmatch(rb"interactive lifetimes ok: threads=[1-9][0-9]* descriptors=[0-9]+ files=0 sockets=0 streams=0\n", out),
          f"interactive lifecycle failed: {out!r} {err!r}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--project-source-dir", type=Path, required=True)
    parser.add_argument("--hello-fixture", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args(), private=True)
    root, source, build = args.test_dir.resolve(), args.source_dir.resolve(), args.build_dir.resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)
    with scratch(root):
        env = isolated_environment(root)
        prefix = root / "prefix"
        stage_installation(args, prefix)
        fixtures = root / "fixture bytes 日本語"
        fixtures.mkdir()
        out, err = run([prefix / "bin/beam", "compile", "--deterministic", "-o", fixtures,
                        "--", source / "interactive_probe.erl",
                        source / "interactive_prestart_rejected.erl"], root, env)
        check(not out and not err, f"fixture compilation: {out!r} {err!r}")
        fixture = fixtures / "interactive_probe.beam"
        client = root / "client"
        run([args.cmake, "-S", source, "-B", client, f"-DCMAKE_PREFIX_PATH={prefix}", f"-DPRIVATE_HOST_BUILD={args.build_dir}",
             f"-DBEAM_TEST_SUPPORT_DIR={args.project_source_dir / 'tests/support'}",
             f"-DBEAM_PROJECT_SOURCE_DIR={args.project_source_dir}"], root, env)
        run([args.cmake, "--build", client, "--parallel", "2"], root, env)
        shutil.copy2(client / "interactive_acceptance", prefix / "bin/interactive_acceptance")
        for phase in ("fresh", "reduced", "relocated"):
            if phase == "reduced":
                reduce_prefix(prefix)
                command_family_case(prefix, source, root / "whole BEAM family work 日本語", args.hello_fixture)
                shutil.rmtree(prefix / "lib/beam/tools/compiler")
                shutil.rmtree(prefix / "lib/beam/runtime/beam/run")
            elif phase == "relocated":
                relocated = root / "relocated prefix λ 日本語"
                prefix.rename(relocated)
                prefix = relocated
            print(f"Checking {phase} interactive installation", flush=True)
            work = root / f"{phase} work 日本語"
            phase_env = prefix_cases(prefix, fixture, work)
            host_case(prefix, fixture, work, phase_env)
        missing_assets(prefix, fixture, work, phase_env)
        minimal = root / "minimal output prefix"
        (minimal / "bin").mkdir(parents=True)
        (minimal / "lib").mkdir()
        copy_bytecode(prefix, minimal)
        shutil.copy2(prefix / "bin/interactive_acceptance", minimal / "bin/interactive_acceptance")
        for name in ("libbeam.so",):
            shutil.copy2(prefix / "lib" / name, minimal / "lib" / name)
        for name in ("cli", "interactive"):
            relative = Path("lib/beam/runtime/beam") / name
            shutil.copytree(prefix / relative, minimal / relative)
        expected = set(RETURNING_ASSETS) | {"bin/interactive_acceptance", "lib/libbeam.so"} | {
            str(Path("lib/beam/runtime/beam") / name / path.relative_to(prefix / "lib/beam/runtime/beam" / name))
            for name in ("cli", "interactive")
            for path in (prefix / "lib/beam/runtime/beam" / name).rglob("*") if path.is_file()}
        check({str(path.relative_to(minimal)) for path in minimal.rglob("*") if path.is_file()} == expected,
              "output-only deployment contains extra runtime files")
        isolated = dict(env, PATH=str(root / "empty path"))
        host_case(minimal, fixture, root, isolated, minimal=True)
        for name in ("lists", "maps"):
            with hidden(minimal / "lib/beam/runtime/beam/cli/ebin" / f"{name}.beam"):
                _, err = run([minimal / "bin/interactive_acceptance", minimal, fixture, "minimal"], root, isolated, status=1)
                check(err, f"missing output-only {name} fallback was ignored")
        print("installed interactive: fresh, reduced, relocated, minimal, and returning lifetimes passed")


if __name__ == "__main__":
    main()
