"""Compiler integration with Make, Ninja and CMake."""


import os
import shlex
import time

from support.process import check

from .common import dependencies, erl_string, module, run, write


def cmake_case(c, cmake, generator):
    name = "ninja" if generator == "Ninja" else "make"
    root = c.work / f"{name} client κ"
    # CMake's depfile conversion supports different filename punctuation per
    # generator. Direct Make acceptance above covers all emitted escaping.
    header_name = "odd: header.hrl" if generator == "Ninja" else "odd #$ header.hrl"
    header = write(root / header_name, "-define(VALUE, first).\n")
    source = module(root / "cmake_payload.erl", "?VALUE",
                    f'-include({erl_string(header_name)}).\n')
    runtime = ""
    write(root / "CMakeLists.txt", f'''cmake_minimum_required(VERSION 3.25)
project(compile_client LANGUAGES NONE)
find_package(BEAM CONFIG REQUIRED)
set(output "${{CMAKE_CURRENT_BINARY_DIR}}/cmake_payload.beam")
set(depfile "${{CMAKE_CURRENT_BINARY_DIR}}/cmake_payload.d")
add_custom_command(OUTPUT "${{output}}"
    COMMAND BEAM::CLI {runtime} compile --deterministic
        --depfile "${{depfile}}" -o "${{CMAKE_CURRENT_BINARY_DIR}}"
        "${{CMAKE_CURRENT_SOURCE_DIR}}/cmake_payload.erl"
    DEPENDS "${{CMAKE_CURRENT_SOURCE_DIR}}/cmake_payload.erl" BEAM::CLI
    DEPFILE "${{depfile}}" VERBATIM)
add_custom_target(payload ALL DEPENDS "${{output}}")
''')
    build = root / "build"
    run([cmake, "-S", root, "-B", build, "-G", generator,
         f"-DCMAKE_PREFIX_PATH={c.prefix}", f"-DCMAKE_MAKE_PROGRAM={c.make if generator == 'Unix Makefiles' else c.ninja}"], c.work, c.env, timeout=60)
    run([cmake, "--build", build], c.work, c.env, timeout=60)
    beam = build / "cmake_payload.beam"
    c.value(beam, "first")
    original_time = beam.stat().st_mtime_ns
    run([cmake, "--build", build], c.work, c.env, timeout=60)
    check(beam.stat().st_mtime_ns == original_time, f"{generator}: clean rebuild recompiled payload")
    time.sleep(1.05)  # Make's timestamp granularity can be one second.
    write(header, "-define(VALUE, second).\n")
    run([cmake, "--build", build], c.work, c.env, timeout=60)
    c.value(beam, "second")
    check(beam.stat().st_mtime_ns != original_time, f"{generator}: header change did not rebuild")
    check(dependencies(build / "cmake_payload.d", beam) ==
          {path.resolve() for path in (source, header)},
          f"{generator}: generated dependency inventory is incorrect")


def literal_make_case(c):
    """Make must track literal names, including when wildcard decoys exist."""
    root = c.work / "literal Make paths"
    names = ["star*.hrl", "question?.hrl", "bracket[ab].hrl", "tail\\", "tail-two\\\\",
             "semi;colon.hrl", "order|only.hrl", "back\\;semi.hrl"]
    headers = [write(root / name, f"-define(HEADER_{index}, {index}).\n")
               for index, name in enumerate(names)]
    decoys = [write(root / name, "unread dependency\n") for name in
              ["star-decoy.hrl", "questionx.hrl", "bracketa.hrl"]]
    source = module(root / "literal_make.erl", "literal_make",
                    "".join(f"-include({erl_string(name)}).\n" for name in names))
    out = root / "out%dir;pipe|"
    depfile, beam = root / "literal=make.d", out / "literal_make.beam"
    c.compile("--depfile", depfile, "-o", out, source)
    check(dependencies(depfile, beam) == {path.resolve() for path in [source, *headers]},
          "literal Make dependency names changed during escaping")

    def make_word(path, target=False):
        # The output exercises target-only percent and semicolon quoting.
        word = str(path).replace("$", "$$").replace(" ", "\\ ")
        return word.replace("%", "\\%").replace(";", "\\\\\\;") if target else word

    def recipe_arg(value):
        return shlex.quote(str(value)).replace("$", "$$")

    marker = root / "rebuilds"
    command = " ".join(recipe_arg(arg) for arg in
                       [*c.command, "compile", "--depfile", depfile, "-o", out, source])
    makefile = write(root / "Makefile", f"include {make_word(depfile)}\n"
                     f"{make_word(beam, target=True)}:\n"
                     f"\t@printf x >> {recipe_arg(marker)}\n\t@{command}\n")
    make = [c.make, "-f", makefile, beam]
    run(make, c.work, c.env)
    check(not marker.exists(), "literal dependencies trigger an unnecessary rebuild")

    # Control timestamps instead of relying on sleeps or filesystem granularity.
    def older_inputs():
        before = time.time() - 20
        for path in [source, *headers, *decoys]:
            os.utime(path, (before, before))
        os.utime(beam, (before + 10, before + 10))

    older_inputs()
    for decoy in decoys:
        decoy.touch()
    run(make, c.work, c.env)
    check(not marker.exists(), "Make expanded a literal dependency into wildcard matches")
    for index, header in enumerate(headers):
        older_inputs()
        header.touch()
        run(make, c.work, c.env)
        check(marker.read_text() == "x" * (index + 1),
              f"Make failed to rebuild after literal header changed: {header}")
        run(make, c.work, c.env)
        check(marker.read_text() == "x" * (index + 1), "Make did not settle after rebuilding")
    c.value(beam, "literal_make")

    equal_header = write(root / "equals=header.hrl", "-define(VALUE, equal_header).\n")
    header_source = module(root / "equal_header.erl", "?VALUE",
                           f'-include({erl_string(equal_header.name)}).\n')
    equal_source = module(root / "source=term.erl", "equal_source")
    for label, input_source, output in [("output", source, root / "equals=out"),
                                       ("header", header_source, root / "equal-header-out"),
                                       ("source", equal_source, root / "equal-source-out")]:
        c.compile("-o", output, input_source)
        bytecode = output / f"{input_source.stem}.beam"
        previous = bytecode.read_bytes()
        rejected_dep = write(root / f"rejected-{label}.d", "previous dependency file\n")
        diagnostic = c.compile("--depfile", rejected_dep, "-o", output, input_source, status=1)
        check("'='" in diagnostic and "--depfile" in diagnostic,
              "unrepresentable dependency path lacks an actionable diagnostic")
        check(bytecode.read_bytes() == previous and
              rejected_dep.read_text() == "previous dependency file\n",
              "dependency path rejection modified existing outputs")
        check(not list(output.glob("*.tmp*")), "dependency rejection left temporary output")
