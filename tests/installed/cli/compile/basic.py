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

    macros = module(c.work / "macros.erl", "{?VALUE,?FLAG}")
    c.compile("-o", out, "-DVALUE={7,[a,b],<<\"literal\">>}", "-D", "FLAG", macros)
    c.value(out / "macros.beam", "{{7,[a,b],<<\"literal\">>},true}")
    for args in [[], ["--unknown"], ["--debug-info", first], ["-o"], ["-I"], ["-D"], ["--depfile"],
                 ["-DVALUE=1+2", macros], ["-DVALUE=1", "-D", "VALUE=2", macros],
                 ["-D=1", macros], ["--depfile", "multi.d", first, second]]:
        c.compile(*args, status=1)
    for args in [["-o", out, "-o", c.work / "elsewhere", first],
                 ["--depfile", "first.d", "--depfile", "second.d", first]]:
        c.compile(*args, status=1)
    marker = c.work / "must-not-execute"
    c.compile("-DVALUE=file:write_file(\"must-not-execute\", <<>>)", macros, status=1)
    check(not marker.exists(), "macro parser evaluated an expression")
    c.compile(c.work / "missing.erl", status=1)
    c.compile(write(c.work / "plain.txt", "not Erlang"), status=1)

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
