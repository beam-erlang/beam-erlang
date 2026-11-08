"""Compiler command behavior, diagnostics and artifact publication."""


from support.process import check

from .common import erl_string, module, write


def basic_cases(c):
    out = c.work / "new output" / "ebin"
    first = module(c.work / "first.erl", "{7, [a,b], <<\"literal\">>} ")
    c.compile("-o", out, first)
    c.value(out / "first.beam", "{7,[a,b],<<\"literal\">>}")
    # The current directory is the default output destination.
    second = module(c.work / "second.erl", "two")
    c.compile(second)
    c.value(c.work / "second.beam", "two")
    third = module(c.work / "third.erl", "three")
    c.compile("-o", out, second, third)
    c.value(out / "third.beam", "three")
    if c.runtime == "beam":
        include = c.prefix / "lib/beam/tools/compiler/include"
        check((include / "ms_transform.hrl").is_file(), "installed match-spec header missing")
        for namespace in ("beam_ets", "ets"):
            name = "match_spec_" + namespace
            source = write(c.work / (name + ".erl"),
                f'-module({name}).\n-include("ms_transform.hrl").\n'
                '-export([value/0]).\n-record(person,{name,age}).\n'
                f'value() -> Limit = 18, {namespace}:fun2ms('
                'fun(#person{name=Name,age=Age}) when Age >= Limit -> Name end).\n')
            c.compile("-I", include, "-o", out, source)
            c.eval(c.load(out / (name + ".beam")) +
                   f'MS = {name}:value(), [alice] = beam_ets:match_spec_run('
                   '[{person,alice,25},{person,bob,16}],beam_ets:match_spec_compile(MS)).')
        invalid = write(c.work / "invalid_match_spec.erl",
            '-module(invalid_match_spec).\n-include("ms_transform.hrl").\n'
            '-export([value/0]).\nvalue() -> beam_ets:fun2ms(not_a_fun).\n')
        diagnostic = c.compile("-I", include, "-o", out, invalid, status=1)
        check("invalid_match_spec.erl:" in diagnostic, "match-spec error lost source location")
        check(not (out / "invalid_match_spec.beam").exists(), "invalid match spec published bytecode")

    macros = module(c.work / "macros.erl", "{?VALUE,?FLAG}")
    c.compile("-o", out, "-DVALUE={7,[a,b],<<\"literal\">>}", "-D", "FLAG", macros)
    c.value(out / "macros.beam", "{{7,[a,b],<<\"literal\">>},true}")
    for args in [[], ["--unknown"], ["--native-bindings=unknown", first], ["--debug-info", first], ["-o"], ["-I"], ["-D"], ["--depfile"],
                 ["-DVALUE=1+2", macros], ["-DVALUE=1", "-D", "VALUE=2", macros],
                 ["-D=1", macros], ["--depfile", "multi.d", first, second]]:
        c.compile(*args, status=1)
    for args in [["-o", out, "-o", c.work / "elsewhere", first],
                 ["--depfile", "first.d", "--depfile", "second.d", first]]:
        c.compile(*args, status=1)
    if c.runtime == "beam":
        profile = module(c.work / "native_profile.erl", "is_float(1.0)")
        c.compile("--native-bindings=modern", "-o", out, profile)
        c.value(out / "native_profile.beam", "true")
        canonical = c.work / "canonical output"
        c.compile("--native-bindings=canonical", "-o", canonical, profile)
        c.eval(c.load(canonical / "native_profile.beam") +
               'try native_profile:value() of _ -> error(expected_undef) '
               'catch error:undef:Stack -> [{erlang,is_float,[1.0],_}|_] = Stack end.')
        nif = write(c.work / "selected_nif_loader.erl", '''
-module(selected_nif_loader).
-compile({beam_native_bindings,[{vm_load_nif_2,chosen,nif_load,2}]}).
-export([init/0,value/0]).
-nifs([value/0]).
-on_load(init/0).
init() -> chosen:nif_load("missing",0).
value() -> beam:nif_error(undef).
''')
        c.compile("--warnings-as-errors", "-o", out, nif)
        previous = (out / "selected_nif_loader.beam").read_bytes()
        nif.write_text(nif.read_text().replace("vm_load_nif_2", "vm_send_2"))
        diagnostic = c.compile("--warnings-as-errors", "-o", out, nif, status=1)
        check("load_nif" in diagnostic, "NIF lint accepted a different selected descriptor")
        check((out / "selected_nif_loader.beam").read_bytes() == previous,
              "rejected NIF profile replaced the prior artifact")
    marker = c.work / "must-not-execute"
    c.compile("-DVALUE=file:write_file(\"must-not-execute\", <<>>)", macros, status=1)
    check(not marker.exists(), "macro parser evaluated an expression")
    c.compile(c.work / "missing.erl", status=1)
    c.compile(write(c.work / "plain.txt", "not Erlang"), status=1)
    for name, expression in (
            ("guard_bytes", "beam_binary:part(B, 0, 0) =:= 0"),
            ("guard_bytes_find", "beam_binary:match(B, <<0>>) =:= nomatch"),
            ("guard_maps_get", "beam_maps:get(key,B) =:= value"),
            ("guard_internal_qualified", "erlang:'$beam_guard_user'(B)"),
            ("guard_internal_bare", "'$beam_guard_user'(B)")):
        source = write(c.work / f"{name}.erl", f"-module({name}).\n-export([value/1]).\n"
                       f"value(B) when {expression} -> ok; value(_) -> no.\n")
        diagnostic = c.compile("-o", out, source, status=1)
        check("illegal guard expression" in diagnostic,
              f"ordinary byte operation accepted in guard: {diagnostic}")
        check(not (out / f"{name}.beam").exists(), "illegal guard published bytecode")
    marker = module(c.work / "native_marker.erl", "erlang:'$beam_guard_user'(1)")
    c.compile("-o", out, marker)
    c.eval(c.load(out / "native_marker.beam") +
           'try native_marker:value() of X -> error({expected_undef,X}) '
           'catch error:undef:Stack -> [{erlang,\'$beam_guard_user\',[1],_}|_] = Stack end.')

    old = (out / "first.beam").read_bytes()
    module(first, "broken(")
    diagnostic = c.compile("-o", out, first, status=1)
    check("first.erl:" in diagnostic, f"missing source location: {diagnostic}")
    check((out / "first.beam").read_bytes() == old, "compile failure damaged previous bytecode")
    module(first, "restored")
    mismatch = write(c.work / "mismatch.erl", "-module(other).\n-export([value/0]).\nvalue()->ok.\n")
    c.compile("-o", out, mismatch, status=1)
    check(not (out / "mismatch.beam").exists() and not (out / "other.beam").exists(),
          "module mismatch published bytecode")
    duplicate = module(c.work / "other" / "first.erl", "duplicate")
    c.compile("-o", out, first, duplicate, status=1)
    check((out / "first.beam").read_bytes() == old, "collision rejection mutated output")
    previous_third = (out / "third.beam").read_bytes()
    module(third, "changed")
    c.compile("-o", out, first, mismatch, third, status=1)
    c.value(out / "first.beam", "restored")
    check((out / "third.beam").read_bytes() == previous_third,
          "multi-source compilation continued after a failed source")

    warning = write(c.work / "warning_é.erl", "-module('warning_é').\n-export([value/0]).\n"
                    "value() -> Unused = 1, ok.\n")
    diagnostic = c.compile("-o", out, warning, warning=True)
    check("warning_é.erl:" in diagnostic, "warning lost Unicode filename or source location")
    previous = (out / "warning_é.beam").read_bytes()
    c.compile("-o", out, "--warnings-as-errors", warning, status=1)
    check((out / "warning_é.beam").read_bytes() == previous,
          "warnings-as-errors replaced previous output")
    env = c.env.copy()
    env["ERL_COMPILER_OPTIONS"] = "[warnings_as_errors]"
    c.compile("-o", out, warning, warning=True, env=env)
    directive = write(c.work / "directive.erl", '-module(directive).\n'
                      '-compile(nowarn_unused_vars).\n-export([value/0]).\n'
                      'value() -> Unused = 1, directive.\n')
    c.compile("--warnings-as-errors", "-o", out, directive)
    c.value(out / "directive.beam", "directive")
    if c.runtime == "beam":
        c.eval(f'{{ok,File}} = beam_file:open(<<{erl_string(second)}/utf8>>,infinity,#{{mode=>read}}), '
               '{ok,Bytes} = beam_file:read(File,16,infinity), true = byte_size(Bytes) > 0, '
               'ok = beam_file:close(File,infinity).')
    else:
        c.eval(f'{{ok, File}} = file:open({erl_string(second)}, [read,binary]), '
               '{ok, Info} = file:read_file_info(File), true = element(2, Info) > 0, '
               'ok = file:close(File), '
               f'{{ok, Raw}} = file:open({erl_string(second)}, [read,binary,raw]), '
               '{ok, RawInfo} = file:read_file_info(Raw), true = element(2, RawInfo) > 0, '
               'ok = file:close(Raw).')

    literal = module(c.work / "--runtime=beam.erl", "literal")
    c.compile("-o", "--runtime=otp", "--", literal.name)
    c.value(c.work / "--runtime=otp" / "--runtime=beam.beam", "literal")
    includes = c.work / "--runtime=beam"
    write(includes / "value.hrl", "-define(VALUE, literal_include).\n")
    literal_include = module(c.work / "literal_include.erl", "?VALUE", '-include("value.hrl").\n')
    c.compile("-o", out, "-I", includes.name, literal_include)
    c.value(out / "literal_include.beam", "literal_include")
    # A regular file makes the output directory unusable even for privileged callers.
    blocked = write(c.work / "blocked", "file, not a directory")
    previous_second = (out / "second.beam").read_bytes()
    module(second, "changed")
    c.compile("-o", blocked / "child", second, status=1)
    c.compile("-o", out, "--depfile", blocked / "child.d", second, status=1)
    c.compile("-o", out, "--depfile", out / "second.beam", second, status=1)
    check((out / "second.beam").read_bytes() == previous_second,
          "failed dependency publication damaged previous bytecode")
    check(not list(out.glob("*.tmp*")), "failed publication left temporary output")
