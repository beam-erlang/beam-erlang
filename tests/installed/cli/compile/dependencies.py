"""Compiler dependencies, preprocessing and transform execution."""


import shutil

from support.process import check

from .common import dependencies, erl_string, module, run, write


def dependency_cases(c):
    root = c.work / "dependencies κ #$"
    source = root / "dep_test.erl"
    one, two = root / "one", root / "two"
    write(one / "choice.hrl", '-define(CHOICE, first).\n-include("nested.hrl").\n')
    write(two / "choice.hrl", "-define(CHOICE, second).\n")
    write(one / "nested.hrl", '-include("../a/same.hrl").\n-include("../b/same.hrl").\n')
    write(root / "a/same.hrl", "-define(A, a).\n")
    write(root / "b/same.hrl", "-define(B, b).\n")
    write(root / "odd #$ header.hrl", "-define(ODD, odd).\n")
    write(root / "colon:header.hrl", "-define(COLON, colon).\n")
    write(root / "back\\slash.hrl", "-define(BACKSLASH, backslash).\n")
    write(root / "selected.hrl", "-define(SELECTED, selected).\n")
    write(root / "unused.hrl", "-define(SELECTED, unused).\n")
    write(root / "module doc.md", "# Dependency documentation\n\nUnicode: κόσμος.\n")
    module(source, "{?CHOICE,?A,?B,?ODD,?SELECTED,#file_info{size=3}}",
           '-include("choice.hrl").\n-include("odd #$ header.hrl").\n'
           '-include("colon:header.hrl").\n-include("back\\\\slash.hrl").\n'
           '-include_lib("sample/include/file.hrl").\n'
           '-ifdef(SELECT).\n-include("selected.hrl").\n'
           '-else.\n-include("unused.hrl").\n-endif.\n'
           '-moduledoc({file,"module doc.md"}).\n')
    write(root / "sample/include/file.hrl", "-record(file_info, {size}).\n")
    out = root / "out"
    depfile = out / "dep_test.d"
    expected = {path.resolve() for path in [source, one / "choice.hrl", one / "nested.hrl",
                root / "a/same.hrl", root / "b/same.hrl", root / "odd #$ header.hrl",
                root / "colon:header.hrl", root / "back\\slash.hrl",
                root / "selected.hrl", root / "module doc.md",
                root / "sample/include/file.hrl"]}
    c.compile("--deterministic", "--depfile", depfile, "-o", out,
              "-I", one, f"-I{two}", "-DSELECT", source)
    check(dependencies(depfile, out / "dep_test.beam") == expected,
          f"incorrect dependencies: {depfile.read_text()}")
    first_dep = depfile.read_bytes()
    c.compile("--deterministic", "--depfile", depfile, "-o", out,
              "-I", one, f"-I{two}", "-DSELECT", source)
    check(depfile.read_bytes() == first_dep, "dependency ordering is unstable")
    run([c.make, "-f", depfile, "-q", out / "dep_test.beam"], c.work, c.env)
    previous = (out / "dep_test.beam").read_bytes()
    doc = root / "module doc.md"
    documentation = doc.read_text()
    doc.unlink()
    c.compile("--warnings-as-errors", "--deterministic", "--depfile", depfile,
              "-o", out, "-I", one, "-DSELECT", source, status=1)
    check((out / "dep_test.beam").read_bytes() == previous and
          depfile.read_bytes() == first_dep, "missing documentation damaged prior outputs")
    write(doc, documentation)
    header = one / "choice.hrl"
    previous_header = header.read_bytes()
    c.compile("--depfile", header, "-o", out, "-I", one, "-DSELECT", source, status=1)
    check(header.read_bytes() == previous_header, "depfile alias overwrote an input header")
    check((out / "dep_test.beam").read_bytes() == previous,
          "depfile alias replaced previous bytecode")
    # Check include precedence independently of record syntax in an eval expression.
    c.value(out / "dep_test.beam", "{first,a,b,odd,selected,_}")

    # ?FILE is deliberately different in deterministic mode. Dependency scanning
    # must take the same branch as the compiler while retaining physical paths.
    conditional = root / "file_branch.erl"
    write(root / "short.hrl", "-define(VALUE, short).\n")
    write(root / "full.hrl", "-define(VALUE, full).\n")
    module(conditional, "?VALUE", '-if(?FILE =:= "file_branch.erl").\n'
           '-include("short.hrl").\n-else.\n-include("full.hrl").\n-endif.\n')
    for flags, branch in [([], "full"), (["--deterministic"], "short")]:
        dep = out / f"{branch}.d"
        c.compile(*flags, "--depfile", dep, "-o", out, conditional)
        check(dependencies(dep, out / "file_branch.beam") ==
              {conditional.resolve(), (root / f"{branch}.hrl").resolve()},
              f"?FILE dependency scan diverged from compilation: {dep.read_text()}")
        c.value(out / "file_branch.beam", branch)

    # Physical input names cannot be reconstructed from deterministic -file forms.
    probe_opts = ('[extra,file_dependencies,{deterministic,true},'
                  '{source_name,"file_branch.erl"}]')
    c.compiler_eval(f'{{ok, Default}} = epp:parse_file({erl_string(conditional)}, []), '
           f'{{ok, Default, Ordinary}} = epp:parse_file({erl_string(conditional)}, [extra]), '
           'false = lists:keymember(file_dependencies, 1, Ordinary), '
           f'{{ok, Default, _}} = epp:parse_file({erl_string(conditional)}, [extra,file_dependencies]), '
           f'{{ok, _, Extra}} = epp:parse_file({erl_string(conditional)}, {probe_opts}), '
           'Deps = proplists:get_value(file_dependencies, Extra), '
           f'true = lists:member({erl_string(conditional)}, Deps), '
           f'true = lists:member({erl_string(root / "short.hrl")}, Deps).')

    copies = []
    for name in ("relocation one", "relocation two"):
        copied = c.work / name
        shutil.copytree(root, copied, ignore=shutil.ignore_patterns("out"))
        target = copied / "out"
        c.compile("--deterministic", "--depfile", target / "dep_test.d", "-o", target,
                  "-I", copied / "one", "-I", copied / "two", "-DSELECT", copied / "dep_test.erl")
        copies.append((target / "dep_test.beam").read_bytes())
    check(copies[0] == copies[1] == (out / "dep_test.beam").read_bytes(),
          "deterministic bytecode changes after source relocation")
    newline = root / "line\nbreak.hrl"
    write(newline, "-define(VALUE, newline).\n")
    newline_source = module(root / "newline_test.erl", "?VALUE", '-include("line\\nbreak.hrl").\n')
    c.compile("--depfile", out / "newline.d", "-o", out, newline_source, status=1)
    check(not (out / "newline_test.beam").exists(), "newline dependency published bytecode")


def transform_case(c):
    marker = c.work / "transform-calls"
    transform = write(c.work / "compile_acceptance_transform.erl",
                      '-module(compile_acceptance_transform).\n-export([parse_transform/2]).\n'
                      f'parse_transform(Forms, _) -> ok = file:write_file({erl_string(marker)}, '
                      '<<"called\\n">>, [append]), Forms.\n')
    # This client-supplied transform is placed on the ordinary installed code path
    # of the test-owned prefix; no source/build runtime modules are substituted.
    installed = c.prefix / "lib/beam/ebin/compile_acceptance_transform.beam"
    try:
        c.compile("-o", installed.parent, transform)
        source = module(c.work / "transformed.erl", "transformed",
                        '-compile({parse_transform,compile_acceptance_transform}).\n')
        c.compile("--deterministic", "--depfile", c.work / "transformed.d", source)
        check(marker.read_text() == "called\n", "dependency scan executed a parse transform")
        c.value(c.work / "transformed.beam", "transformed")
    finally:
        installed.unlink(missing_ok=True)
