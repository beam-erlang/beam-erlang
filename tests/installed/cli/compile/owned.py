"""Owned compiler isolation, resolution and repeated VM lifetimes."""

import re
import shutil

from support.files import hidden_paths
from support.artifacts import artifact
from support.bytecode import TEST_SUPPORT_DIR
from support.process import isolated_environment, check

from .common import Compiler, dependencies, erl_string, module, run, write


def owned_compiler_cases(prefix, work):
    """The BEAM compiler owns its dependencies and completes before execution."""
    c = Compiler(prefix, work / "standalone", "beam")
    compiler_root = prefix / "lib/beam/tools/compiler"
    compiler_ebin = compiler_root / "ebin"
    check((compiler_root / "modules.txt").is_file(), "compiler inventory is missing")
    marker = c.work / "compiler-context-calls"
    transform = write(c.work / "compiler_context_transform.erl", f'''
-module(compiler_context_transform).
-export([parse_transform/2]).
parse_transform(Forms, _) ->
    Root = {erl_string(compiler_ebin)},
    lists:foreach(fun(Module) ->
        Expected = Root ++ "/" ++ atom_to_list(Module) ++ ".beam",
        Expected = code:which(Module)
    end, [compile,epp,file,io,code,beam_compiler_cli,beam_compiler_engine]),
    lists:foreach(fun(Name) -> undefined = whereis(Name) end,
        [code_server,file_server_2,application_controller,inet_db,user,standard_error]),
    undefined = ets:whereis(ac_tab),
    Core = lists:sort([atomics,counters,persistent_term,erlang,erts_internal,
        erts_code_purger,erts_literal_area_collector,erts_trace_cleaner,
        erts_dirty_process_signal_handler]),
    Core = lists:sort(erlang:pre_loaded()),
    lists:foreach(fun
        ({{Module,preloaded}}) -> true = lists:member(Module,Core);
        ({{compiler_context_transform,Origin}}) ->
            {erl_string(c.work / "compiler_context_transform.beam")} = Origin;
        ({{_,Origin}}) -> true = lists:prefix(Root ++ "/",Origin)
    end, code:all_loaded()),
    lists:foreach(fun(Module) -> false = erlang:module_loaded(Module) end,
        [beam_cli,beam_return_boot,shell,prim_file,prim_buffer,prim_socket]),
    [] = erlang:ports(),
    ExplicitForms = [{{attribute,1,module,compiler_forms_fixture}},
        {{attribute,1,export,[{{value,0}}]}},
        {{function,1,value,0,[{{clause,1,[],[],
            [{{tuple,1,[{{atom,1,forms}},{{integer,1,42}}]}}]}}]}}],
    {{ok,#{{module := compiler_forms_fixture,bytecode := Bytes,
            warnings := [],dependencies := []}}}} =
        beam_compiler_engine:forms(ExplicitForms,[deterministic],#{{}}),
    true = is_binary(Bytes),
    ok = file:write_file({erl_string(c.work / "compiler_forms_fixture.beam")},Bytes),
    InvalidForms = [{{attribute,1,module,invalid_forms_fixture}},
        {{attribute,1,export,[{{missing,0}}]}}],
    {{error,[_|_],_}} = beam_compiler_engine:forms(InvalidForms,[],#{{}}),
    persistent_term:put({{compiler_context_transform,state}}, compiler_state),
    Parent = self(),
    true = register(compiler_context_worker, spawn(fun() ->
        Table = ets:new(compiler_context_table, [named_table,public]),
        true = ets:insert(Table, {{compiler_state,retained}}),
        Parent ! compiler_table_ready,
        receive stop -> ok end
    end)),
    receive compiler_table_ready -> ok after 3000 -> error(compiler_table_timeout) end,
    ok = file:write_file({erl_string(marker)}, <<"called\\n">>, [append]),
    Forms.
''')
    c.compile("--deterministic", transform)
    header = c.work / "explicit headers/file.hrl"
    header.parent.mkdir()
    header.write_text("-record(file_info, {size}).\n")
    source = module(c.work / "standalone_compiled.erl", "{standalone,#file_info{size=17}}",
                    '-include("file.hrl").\n'
                    '-compile({parse_transform,compiler_context_transform}).\n')
    output = c.work / "output"
    depfile = output / "standalone_compiled.d"
    language_source = write(c.work / "compiler_language_fixture.erl", '''
-module(compiler_language_fixture).
-export([value/0]).
-record(packet, {tag = sample, payload}).
value() ->
    Packet = #packet{payload = <<1,2,3>>},
    Map = #{tag => Packet#packet.tag, payload => Packet#packet.payload},
    Pairs = [{N,N*N} || N <- [1,2,3,4], N rem 2 =:= 0],
    Incremented = << <<(N+1)>> || <<N>> <= Packet#packet.payload >>,
    self() ! {selected,Map},
    Received = receive
        {selected,#{tag := sample,payload := <<Head,Rest/binary>>}} -> {Head,Rest}
    after 1000 -> erlang:error(receive_timeout)
    end,
    Exception = try erlang:error(sample) catch error:sample -> caught end,
    {Packet#packet.tag,Incremented,Pairs,Received,Exception}.
''')
    # The header above is an explicit client input. No application bytecode,
    # startup artifact, optional native component or helper can supply execution.
    with hidden_paths([prefix / "lib/beam/runtime",
                       prefix / "lib/beam/ebin", prefix / "libexec/beam",
                       *(prefix / "lib/beam" / inventory for inventory in ("library", "boot", "services")),
                       *(artifact(prefix, "library." + unit + ".shared")
                         for unit in ("file", "network", "zlib"))]):
        c.compile("--deterministic", "--depfile", depfile, "-I", header.parent,
                  "-o", output, source)
        check(marker.read_text() == "called\n", "standalone compiler probe did not run once")
        check(dependencies(depfile, output / "standalone_compiled.beam") ==
              {source.resolve(), header.resolve()}, "standalone compiler lost explicit dependencies")
        c.compile("--deterministic", "-o", output, language_source)
        language_bytes = (output / "compiler_language_fixture.beam").read_bytes()
        c.compile("--deterministic", "-o", output, language_source)
        check((output / "compiler_language_fixture.beam").read_bytes() == language_bytes,
              "owned compiler is not deterministic")
    for runtime in ("beam",):
        execution = Compiler(prefix, work / f"execute-{runtime}", runtime)
        execution.eval(
            execution.load(output / "standalone_compiled.beam") +
            '{standalone,Info} = standalone_compiled:value(), '
            'file_info = element(1,Info), 17 = element(2,Info).')
        execution.value(output / "compiler_language_fixture.beam",
                        "{sample,<<2,3,4>>,[{2,4},{4,16}],{1,<<2,3>>},caught}")
        execution.value(c.work / "compiler_forms_fixture.beam", "{forms,42}")

    marker.unlink()
    run_source = write(c.work / "compiler_run_isolation.erl", '''
-module(compiler_run_isolation).
-compile({parse_transform,compiler_context_transform}).
-export([main/1]).
main(["literal; argument"]) ->
    absent = persistent_term:get({compiler_context_transform,state}, absent),
    undefined = whereis(compiler_context_worker),
    undefined = ets:whereis(compiler_context_table),
    false = erlang:module_loaded(compiler_context_transform),
    io:format("compiler isolation ok~n"),
    erlang:halt(0).
''')
    stdout, stderr = run([*c.command, "run", run_source, "--", "literal; argument"], c.work, c.env)
    check(stdout == "compiler isolation ok\n" and not stderr,
          f"compile/run isolation changed output: {stdout!r} {stderr!r}")
    check(marker.read_text() == "called\n", "source execution did not use the owned compiler once")
    check(not run_source.with_suffix(".beam").exists(), "source execution published bytecode")
    sequential_compiler_case(c)
    compiler_failure_cases(c)
    compiler_resolution_cases(c)

    declared = compiler_ebin / "compile.beam"
    declared_bytes = declared.read_bytes()
    try:
        declared.write_bytes((output / "compiler_language_fixture.beam").read_bytes())
        identity_source = module(c.work / "compiler_identity_probe.erl", "must_not_compile")
        diagnostic = c.compile(identity_source, status=1)
        check("compiler module identity mismatch" in diagnostic,
              f"compiler inventory accepted wrong module identity: {diagnostic!r}")
        check(not identity_source.with_suffix(".beam").exists(), "wrong compiler identity produced output")
    finally:
        declared.write_bytes(declared_bytes)

    with hidden_paths([compiler_root]):
        missing = module(c.work / "missing_compiler.erl", "must_not_compile")
        diagnostic = c.compile(missing, status=1)
        check("compiler" in diagnostic.lower(), "missing compiler diagnostic does not identify the tooling")
        check(not missing.with_suffix(".beam").exists(), "missing compiler fell back to OTP")
        stdout, stderr = run([*c.command, "run", run_source, "--", "literal; argument"],
                             c.work, c.env, status=1)
        check(not stdout and "compiler" in stderr.lower(), "source execution bypassed a missing compiler")
        check(marker.read_text() == "called\n", "missing compiler still ran the source transform")


def sequential_compiler_case(c):
    marker = c.work / "sequential-transform-calls"
    transform = write(c.work / "compiler_sequence_transform.erl", f'''
-module(compiler_sequence_transform).
-export([parse_transform/2]).
parse_transform(Forms, _) ->
    Key = {{compiler_sequence_transform,count}},
    Count = persistent_term:get(Key,0) + 1,
    persistent_term:put(Key,Count),
    ok = file:write_file({erl_string(marker)}, integer_to_binary(Count), [append]),
    Forms.
''')
    c.compile(transform)
    sources = [module(c.work / f"sequential_{name}.erl", name,
                      '-compile({parse_transform,compiler_sequence_transform}).\n')
               for name in ("one", "two")]
    c.compile(*sources)
    check(marker.read_text() == "12", "one compiler command did not retain state between sources")
    for source, expected in zip(sources, ("one", "two")):
        c.value(source.with_suffix(".beam"), expected)


def compiler_failure_cases(c):
    output = c.work / "failure output"
    app_marker = c.work / "failed-application-must-not-run"
    source = c.work / "compiler_failure_probe.erl"
    header = '-module(compiler_failure_probe).\n-export([main/1,value/0]).\n'
    body = (f'main(_) -> ok = beam_file:write_file(<<{erl_string(app_marker)}/utf8>>, <<"ran">>, infinity), '
            'erlang:halt(0).\nvalue() -> retained.\n')
    write(source, header + body)
    depfile = output / "compiler_failure_probe.d"
    bytecode = output / "compiler_failure_probe.beam"
    c.compile("--deterministic", "--depfile", depfile, "-o", output, source)
    previous = bytecode.read_bytes(), depfile.read_bytes()
    transform = write(c.work / "compiler_throw_transform.erl", '''
-module(compiler_throw_transform).
-export([parse_transform/2]).
parse_transform(_, _) -> throw(compiler_transform_failed).
''')
    c.compile(transform)
    failures = [
        (header + 'main(_) -> malformed(.\nvalue() -> retained.\n', "compiler_failure_probe.erl"),
        (header + '-include("missing_compiler_header.hrl").\n' + body, "missing_compiler_header.hrl"),
        (header + '-compile({parse_transform,compiler_throw_transform}).\n' + body,
         "compiler_throw_transform"),
        (header + '-compile({parse_transform,compiler_missing_transform}).\n' + body,
         "compiler_missing_transform"),
    ]
    for contents, expected in failures:
        write(source, contents)
        diagnostic = c.compile("--deterministic", "--depfile", depfile, "-o", output,
                               source, status=1)
        check(expected in diagnostic, f"compiler failure lost context: {diagnostic!r}")
        check((bytecode.read_bytes(), depfile.read_bytes()) == previous,
              "failed owned compilation changed prior bytecode or dependencies")
        check(not list(output.glob("*.tmp*")), "failed owned compilation left temporary output")
        stdout, stderr = run([*c.command, "run", source], c.work, c.env, status=1)
        check(expected in stdout and not stderr,
              f"source failure lost its stdout diagnostics: {stdout!r} {stderr!r}")
        check(not app_marker.exists(), "failed source compilation still executed its application")
        check(not source.with_suffix(".beam").exists(), "failed source execution published bytecode")
    c.value(bytecode, "retained")


def compiler_resolution_cases(c):
    helper = module(c.work / "compiler_resolution_helper.erl", "resolved")
    transform = c.work / "compiler_resolution_transform.erl"
    consumer = c.work / "compiler_resolution_probe.erl"
    bytecode = consumer.with_suffix(".beam")
    depfile = consumer.with_suffix(".d")
    transform_header = '-module(compiler_resolution_transform).\n-export([parse_transform/2]).\n'
    for directory in (c.work, c.prefix / "lib/beam/ebin"):
        installed_helper = directory / "compiler_resolution_helper.beam"
        try:
            c.compile("-o", directory, helper)
            module(consumer, "retained")
            c.compile("--depfile", depfile, consumer)
            previous = bytecode.read_bytes(), depfile.read_bytes()
            write(transform, transform_header +
                  'parse_transform(Forms, _) -> resolved = compiler_resolution_helper:value(), Forms.\n')
            c.compile(transform)
            module(consumer, "explicit",
                   '-compile({parse_transform,compiler_resolution_transform}).\n')
            diagnostic = c.compile("--depfile", depfile, consumer, status=1)
            check("compiler_resolution_helper" in diagnostic,
                  "implicit helper acquisition did not report the unresolved call")
            check((bytecode.read_bytes(), depfile.read_bytes()) == previous,
                  "implicit acquisition failure changed prior artifacts")
            write(transform, transform_header +
                  'parse_transform(Forms, _) -> '
                  '{module,compiler_resolution_helper} = code:ensure_loaded(compiler_resolution_helper), '
                  'resolved = compiler_resolution_helper:value(), Forms.\n')
            c.compile(transform)
            c.compile("--depfile", depfile, consumer)
            c.value(bytecode, "explicit")
        finally:
            installed_helper.unlink(missing_ok=True)


def compiler_lifetime_case(prefix, work, cmake, build, source, project):
    work.mkdir(parents=True)
    env = isolated_environment(work)
    client = work / "client"
    shutil.copytree(source / "client", client)
    compiler_sources = project / "src/beam/compiler"
    copied_compiler = client / "host/beam/compiler"
    copied_compiler.mkdir(parents=True)
    for name in ("beam_compiler_host.c", "beam_compiler_host.h", "beam_compiler_files.h"):
        shutil.copyfile(compiler_sources / name, copied_compiler / name)
    for name in ("beam_platform.h", "beam_windows_dll.h"):
        shutil.copyfile(project / "src" / name, client / "host" / name)
    shutil.copyfile(build / "compiler/include/beam_compiler_inventory.h",
                    copied_compiler / "beam_compiler_inventory.h")
    client_build = work / "build"
    run([cmake, "-S", client, "-B", client_build, f"-DCMAKE_PREFIX_PATH={prefix}", f"-DTEST_SUPPORT_DIR={TEST_SUPPORT_DIR}"],
        work, env, timeout=60)
    run([cmake, "--build", client_build, "--parallel", "2"], work, env, timeout=60)
    stdout, stderr = run([prefix / "bin/beam", "compile", "--deterministic",
                          "-o", client, "--", client / "compiler_lifetime_transform.erl"],
                         client, env)
    check(not stdout and not stderr, "compiler lifetime transform compilation produced diagnostics")
    with hidden_paths([prefix / "lib/beam/runtime",
                       prefix / "lib/beam/ebin", prefix / "libexec/beam",
                       *(prefix / "lib/beam" / inventory for inventory in ("library", "boot", "services")),
                       *(artifact(prefix, "library." + unit + ".shared")
                         for unit in ("file", "network", "zlib"))]):
        stdout, stderr = run([client_build / "compiler_lifetime", prefix,
                              client / "compiler_lifetime_fixture.erl"], client, env, timeout=60)
    check(not stderr and re.fullmatch(
        r"compiler lifetimes ok: threads=[1-9][0-9]* descriptors=[0-9]+ "
        r"allocations=0 mappings=0 files=0 sockets=0 streams=0\n", stdout),
        f"compiler lifetime observations failed: {stdout!r} {stderr!r}")
