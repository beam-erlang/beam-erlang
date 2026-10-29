#!/usr/bin/env python3
"""Check shared core ownership and the installed native extension boundary."""
from pathlib import Path
import argparse
import collections
import json
import re
import shlex
import sys
import shutil

from support.process import check, isolated_environment, run


CORE_MODULES = {"erlang", "erts_internal", "erts_code_purger",
                "erts_dirty_process_signal_handler", "erts_literal_area_collector",
                "erts_trace_cleaner", "atomics", "counters", "persistent_term"}
COMPONENTS = {"erl_tracer_nif": ("tracing", ["erl_tracer_nif.c"])}
PRIVATE_IMPORTS = {}
DRIVER_API = {"erl_errno_id", "set_busy_port", "set_port_control_flags"}
NATIVE_API = {"beam_native_fail", "beam_native_stop_requested", "beam_native_report_cleanup", "beam_native_request_failure"}


def symbols(args, path, *options):
    if sys.platform == 'darwin':
        options=[option for option in options if option not in ('-D','--defined-only')]
        if '-u' not in options: options.append('-U')
    output = run([args.nm, *options, str(path)], cwd=args.test_dir,
                 env=args.environment, timeout=10, text=True).stdout
    names=[line.split()[-1].split("@",1)[0] for line in output.splitlines() if line.split()]
    return [name[1:] if sys.platform=='darwin' and name.startswith('_') else name for name in names]


def command_paths(command):
    directory = Path(command["directory"])
    source = (directory / command["file"]).resolve()
    output = command.get("output")
    if output is None:
        arguments = command.get("arguments") or shlex.split(command["command"])
        check("-o" in arguments, f"compiler output missing for {source}")
        output = arguments[arguments.index("-o") + 1]
    return source, (directory / output).resolve()


def public_api(name):
    return name.startswith(("enif_", "driver_", "erl_drv_")) or name in DRIVER_API | NATIVE_API


def dynamic_links(args, library):
    if sys.platform == 'darwin':
        output=run([args.otool,'-L',library],cwd=args.test_dir,env=args.environment,timeout=10,text=True).stdout
        identity=run([args.otool,'-D',library],cwd=args.test_dir,env=args.environment,timeout=10,text=True).stdout
        own={line.strip() for line in identity.splitlines()[1:]}
        needed={Path(path).name for path in re.findall(r'^\t(.+?) \(',output,re.M) if path not in own}
        commands=run([args.otool,'-l',library],cwd=args.test_dir,env=args.environment,timeout=10,text=True).stdout
        paths=set(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset',commands))
        return needed,paths
    output = run([args.readelf, "--dynamic", str(library)], cwd=args.test_dir,
                 env=args.environment, timeout=10, text=True).stdout
    needed = set(re.findall(r"\(NEEDED\).*?\[([^]]+)\]", output))
    paths = re.findall(r"\((?:RUNPATH|RPATH)\).*?\[([^]]+)\]", output)
    return needed, {entry for path in paths for entry in path.split(":")}


def check_extension_sources(args, commands, target, directory, owned_sources, owned_objects):
    family, stem = target.split("_", 1)
    component, filenames = COMPONENTS[stem]
    check(directory == f"{family}/{component}",
          f"{target} has wrong component ownership: {directory}")
    expected_sources = {(args.source_dir / "src" / directory / name).resolve()
                        for name in filenames}
    compiled = [(source, obj) for source, obj in commands if f"{target}.dir" in obj.parts]
    check({source for source, _ in compiled} == expected_sources and
          len(compiled) == len(expected_sources),
          f"{target} compilation sources differ: {compiled}")
    owned_sources.update(expected_sources)
    for _, obj in compiled:
        check(obj.is_file(), f"{target} object was not built: {obj}")
        check(obj not in owned_objects, f"{target} reuses another extension's object: {obj}")
        owned_objects.add(obj)


def check_extension_linkage(args, target, library, links, runtime_exports):
    family, stem = target.split("_", 1)
    check(target not in links, f"libbeam still links {target}")
    check(Path(library).is_file(), f"{target} library was not built: {library}")
    needed, search_paths = dynamic_links(args, library)
    check(args.origin+"/../../../.." in search_paths and
          all(path.startswith(args.origin) for path in search_paths),
          f"{target} contains a nonrelocatable native search path: {search_paths}")
    if stem in {"prim_socket_nif", "prim_net_nif"}:
        check(f"lib{family}_socket_backend.so" in needed and "$ORIGIN" in search_paths,
              f"{target} does not retain and locate its family socket backend: {needed}")
    else:
        check(args.runtime_soname in needed, f"{target} lacks its explicit libbeam dependency")
    other_family = "beam" if family == "otp" else "otp"
    check(not any(other_family + "_" in name for name in needed),
          f"{target} links another family's native implementation: {needed}")
    exports = set(symbols(args, library, "-D", "-g", "--defined-only"))
    expected_exports = ({f"{family}_prim_socket_nif_init", f"{family}_prim_net_nif_init"}
                        if stem == "socket_backend" else
                        {"driver_init"} if stem.endswith("_driver") else {"nif_init"})
    check(exports == expected_exports, f"{target} exports {exports}, expected {expected_exports}")
    imports = set(symbols(args, library, "-D", "-u"))
    private = {name for name in imports & runtime_exports if not public_api(name)}
    allowed = PRIVATE_IMPORTS.get(stem, set())
    check(private <= allowed,
          f"{target} imports unreviewed private runtime symbols: {private - allowed}")
    check(not any(name.startswith(("otp_", "beam_")) for name in imports & runtime_exports),
          f"{target} still obtains family implementation from libbeam")
    family_imports = {name for name in imports if name.startswith(("otp_", "beam_"))}
    expected_family = ({f"{family}_{stem}_init"}
                       if stem in {"prim_socket_nif", "prim_net_nif"} else set())
    check(family_imports == expected_family,
          f"{target} imports an unexpected family interface: {family_imports}")


def check_unit_sources(args, commands, target, directory, sources, owned_sources, owned_objects):
    name = target.removeprefix("beam_").removesuffix("_component")
    check(directory == f"beam/{'library' if name == 'base' else name}", f"wrong unit ownership: {directory}")
    expected_sources = {(args.source_dir / "src" / directory / filename).resolve()
                        for filename in sources[name]}
    expected_sources.update((args.compile_commands.parent / "libraries" / name / item).resolve()
                            for item in ("shared.c", "resident.c"))
    compiled = [(source, obj) for source, obj in commands if f"{target}.dir" in obj.parts]
    check({source for source, _ in compiled} == expected_sources and len(compiled) == len(expected_sources),
          f"wrong {name} unit sources: {compiled}")
    owned_sources.update(expected_sources)
    owned_objects.update(obj for _, obj in compiled)
    check(all(obj.is_file() for _, obj in compiled), f"{name} component objects missing")


def check_unit_linkage(args, target, library, links, runtime_exports, expected_exports):
    name = target.removeprefix("beam_").removesuffix("_component")
    check(target not in links, f"libbeam links optional {name} component")
    needed, search_paths = dynamic_links(args, library)
    check(args.runtime_soname in needed and
          not any("beam-" in item or "socket_backend" in item for item in needed),
          f"{name} unit is not independently deployable: {needed}")
    check(search_paths == {args.origin},
          f"{name} unit does not resolve its adjacent runtime library: {search_paths}")
    exports = set(symbols(args, library, "-D", "-g", "--defined-only"))
    check(exports == expected_exports, f"{name} unit exposes unexpected symbols: {exports}")
    imports = set(symbols(args, library, "-D", "-u"))
    private = {symbol for symbol in imports & runtime_exports if not public_api(symbol)}
    check(not private, f"{name} library imports private runtime symbols: {private}")
    check(not any(symbol.startswith(("beam_", "otp_")) and symbol not in NATIVE_API for symbol in imports & runtime_exports),
          f"{name} unit imports family implementation from libbeam")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("--component", action="append", nargs=3, required=True,
                        metavar=("TARGET", "DIRECTORY", "LIBRARY"))
    parser.add_argument("--unit", action="append", nargs=3, required=True,
                        metavar=("TARGET", "DIRECTORY", "LIBRARY"))
    parser.add_argument("--test-file-component", type=Path, required=True)
    parser.add_argument("--core-component", nargs="+", required=True)
    parser.add_argument("--core-preload", nargs="+", required=True)
    parser.add_argument("--base-inventory", type=Path, required=True)
    parser.add_argument("--runtime", action="append", nargs=3, required=True,
                        metavar=("TARGET", "LINK_RECORD", "LIBRARY"))
    parser.add_argument("--test-dir", type=Path, required=True)
    args = parser.parse_args()
    args.otool=shutil.which("otool")
    args.origin="@loader_path" if sys.platform=="darwin" else "$ORIGIN"
    args.runtime_soname=Path(args.runtime[0][2]).name
    args.environment = isolated_environment(args.test_dir)
    check(len(args.runtime) == 1 and args.runtime[0][0] == "libbeam",
          f"unexpected linked runtimes: {args.runtime}")
    _, link_record, runtime_path = args.runtime[0]
    links = Path(link_record).read_text().splitlines()
    runtime = collections.Counter(symbols(args, runtime_path, "-g", "--defined-only"))
    runtime_exports = set(symbols(args, runtime_path, "-D", "-g", "--defined-only"))
    codec_header = (args.source_dir / "include/beam/zlib.h").read_text()
    NATIVE_API.update(re.findall(r"\b(beam_zlib_\w+)\s*\([^;]*?\);", codec_header))
    check(not any(name.startswith(("pcre2_", "_pcre2_")) for name in runtime_exports),
          "libbeam exposes its ABI-incompatible private PCRE to host libraries")
    compression_header = (args.source_dir / "src/erts/emulator/zlib/zlib.h").read_text()
    compression_exports = set(re.findall(r"\bZEXPORT(?:VA)?\s+(\w+)\s*\(", compression_header))
    check("inflate" in compression_exports and "crc32" in compression_exports,
          "compression export inventory did not match the vendored public header")
    check(not runtime_exports & compression_exports,
          "libbeam exposes private compression symbols to host libraries")
    targets = [component[0] for component in args.component]
    check(targets == ["beam_erl_tracer_nif"],
          f"unexpected dynamic targets: {targets}")
    # Registry/loader dispatch functions are VM mechanisms, not NIF implementations.
    nif_dispatch = {"erl_nif_init", "erts_sys_ddll_load_nif_init", "erts_sys_ddll_call_nif_init"}
    product_nifs = {name for name in runtime if
                    (name == "nif_init" or name.endswith("_nif_init")) and name not in nif_dispatch}
    check(not product_nifs, f"VM retains product NIF entries: {product_nifs}")
    retired = {"beam_boot_prepare", "beam_boot_start", "beam_boot_destroy",
               "beam_runtime_lookup", "beam_runtime_bind", "beam_runtime_current",
               "beam_runtime_unbind", "prim_tty_nif_init",
               "ram_file_driver_entry", "prim_file_internal_name2native_1",
               "prim_file_internal_native2name_1", "prim_file_internal_normalize_utf8_1",
               "prim_file_is_translatable_1", "erts_internal_boot_context_0"}
    check(not set(runtime) & retired, f"VM retains OTP/family policy: {set(runtime) & retired}")
    commands = [command_paths(command)
                for command in json.loads(args.compile_commands.read_text())]
    # Returning policy must compile using the public SDK alone. Inspect symbols
    # as well as headers: hand-declared private functions would evade includes.
    public_header = (args.source_dir / "include/beam/runtime.h").read_text()
    public_runtime = set(re.findall(r"\b(beam_[A-Za-z0-9_]+)\s*\(", public_header))
    helper = [(source, obj) for source, obj in commands if "beam_return_helper.dir" in obj.parts]
    check(len(helper) == 1, "returning helper lacks its isolated build target")
    source, obj = helper[0]
    check(source == (args.source_dir / "src/beam/boot/beam_return.c").resolve(),
          "boundary check does not compile the production returning helper")
    imports = set(symbols(args, obj, "-u"))
    definitions = set(symbols(args, obj, "-g", "--defined-only"))
    helper_api = {"beam_return_prepare", "beam_return_discard", "beam_return_run", "beam_outcome_release"}
    check(not helper_api & set(runtime), "runtime image contains optional returning-host policy")
    check(definitions == helper_api, f"returning helper defines private hooks: {definitions - helper_api}")
    check(imports & runtime_exports <= public_runtime,
          f"returning helper imports private runtime symbols: {(imports & runtime_exports) - public_runtime}")
    for command in json.loads(args.compile_commands.read_text()):
        command_source, command_object = command_paths(command)
        if command_object != obj:
            continue
        arguments = command.get("arguments") or shlex.split(command["command"])
        output_index = arguments.index("-o")
        del arguments[output_index:output_index + 2]
        arguments.remove("-c")
        dependencies = run([*arguments, "-M"], cwd=command["directory"],
                           env=args.environment, timeout=10, text=True).stdout
        dependency_paths = [(Path(command["directory"]) / name).resolve()
                            for name in shlex.split(dependencies.replace("\\\n", " "))[1:]]
        private_roots = [(root / directory).resolve()
                         for root in (args.source_dir, args.compile_commands.parent)
                         for directory in ("src", "erts", "components", "emulator", "example", "include",
                                           "lib_src", "preloaded", "test", "tests/vm", "docs")]
        check(not any(path != source and any(path.is_relative_to(root) for root in private_roots)
                      for path in dependency_paths),
              f"returning helper uses private project headers: {dependencies}")
    # Both Host acquisition sources and the delivered libraries use public VM
    # interfaces. Library-owned portability is allowed;
    # emulator headers and hand-declared private imports are not.
    public_headers = (args.source_dir / "include/beam").glob("*.h")
    public_symbols = set()
    for header in public_headers:
        public_symbols.update(re.findall(r"\b(beam_[A-Za-z0-9_]+)\s*\(", header.read_text()))
    public_symbols.update(name for name in runtime_exports if public_api(name))
    isolated_targets = {"beam_host_support.dir", "beam_file_component.dir",
                        "beam_network_component.dir", "beam_zlib_component.dir"}
    for command in json.loads(args.compile_commands.read_text()):
        source, obj = command_paths(command)
        if not isolated_targets.intersection(obj.parts):
            continue
        imports = set(symbols(args, obj, "-u"))
        check(not (imports & runtime_exports) - public_symbols,
              f"optional layer imports private VM symbols: {obj}: {(imports & runtime_exports) - public_symbols}")
        arguments = command.get("arguments") or shlex.split(command["command"])
        output_index = arguments.index("-o")
        del arguments[output_index:output_index + 2]
        arguments.remove("-c")
        dependencies = run([*arguments, "-M"], cwd=command["directory"],
                           env=args.environment, timeout=10, text=True).stdout
        paths = [(Path(command["directory"]) / name).resolve()
                 for name in shlex.split(dependencies.replace("\\\n", " "))[1:]]
        private_roots = [args.source_dir / "src/erts", args.compile_commands.parent / "erts"]
        private = [path for path in paths if path != source and
                   any(path.is_relative_to(root) for root in private_roots)]
        check(not private, f"optional layer uses private VM headers: {source}: {private}")
    for _, kernel in commands:
        if "beam_emulator.dir" in kernel.parts:
            check(not set(symbols(args, kernel, "-u")) & helper_api,
                  f"VM internals depend on returning policy: {kernel}")
    check(not any("beam_boot_helper.dir" in obj.parts for _, obj in commands),
          "OTP boot adapter still compiles")
    owned_objects, owned_sources = set(), set()
    for target, directory, library in args.component:
        check_extension_sources(args, commands, target, directory, owned_sources, owned_objects)
        check_extension_linkage(args, target, library, links, runtime_exports)
    unit_sources = {
        "zlib": ["zlib_nif.c"],
        "file": ["prim_buffer_nif.c", "prim_file_nif.c", "unix/unix_prim_file.c"],
        "network": ["prim_socket_nif.c", "socket_dbg.c", "socket_tarray.c",
                    "socket_util.c", "unix/unix_socket_syncio.c"],
    }
    unit_exports = {
        "zlib": {"beam_library_descriptor_v1", "beam_zlib_stream_count"},
        "file": {"beam_library_descriptor_v1", "beam_file_descriptor_count"},
        "network": {"beam_library_descriptor_v1", "beam_socket_count"},
    }
    check({target for target, _, _ in args.unit} ==
          {f"beam_{name}_component" for name in unit_sources} and len(args.unit) == len(unit_sources),
          "wrong deployable component targets")
    for target, directory, library in args.unit:
        check_unit_sources(args, commands, target, directory, unit_sources, owned_sources, owned_objects)
        name = target.removeprefix("beam_").removesuffix("_component")
        check_unit_linkage(args, target, library, links, runtime_exports, unit_exports[name])
    codec_sources = {(args.source_dir / "src/erts/emulator/zlib" / (name + ".c")).resolve()
                     for name in ("adler32", "compress", "crc32", "uncompr", "deflate",
                                  "trees", "zutil", "inflate", "inftrees", "inffast")}
    codec_objects = [(source, obj) for source, obj in commands if source in codec_sources]
    check(len(codec_objects) == len(codec_sources) and
          {source for source, _ in codec_objects} == codec_sources and
          all("zlib.dir" in obj.parts for _, obj in codec_objects),
          "vendored codec must compile once, in its runtime-owned target")
    # Static delivery compiles the same native inputs and resident wrappers.
    # Its descriptor has a unique accessor and no runtime-linkage selection.
    for name in unit_sources:
        target = f"beam_{name}_component_static"
        compiled = [(source, obj) for source, obj in commands if f"{target}.dir" in obj.parts]
        expected = {(args.source_dir / "src/beam" / name / item).resolve() for item in unit_sources[name]}
        expected.update((args.compile_commands.parent / "libraries" / name / item).resolve()
                        for item in ("static.c", "resident.c"))
        check({source for source, _ in compiled} == expected, f"wrong static {name} sources")
        check(all(obj.is_file() for _, obj in compiled), f"missing static {name} objects")
        owned_objects.update(obj for _, obj in compiled)
    # Fault injection is confined to an uninstalled test library.
    fixture = [(source, obj) for source, obj in commands
               if "beam_file_fault_component.dir" in obj.parts]
    expected_fixture = {(args.source_dir / "src/beam/file" / name).resolve()
                        for name in unit_sources["file"]}
    fixture_native = [(source, obj) for source, obj in fixture if source in expected_fixture]
    check(len(fixture_native) == 3 and {source for source, _ in fixture_native} == expected_fixture,
          "file fault fixture has unexpected native sources")
    for command in json.loads(args.compile_commands.read_text()):
        source, obj = command_paths(command)
        if (source, obj) in fixture_native:
            arguments = command.get("arguments") or shlex.split(command["command"])
            check("-DBEAM_FILE_CONTRACT_TEST" in arguments, "fault fixture lacks test-only controls")
        elif source in expected_fixture:
            arguments = command.get("arguments") or shlex.split(command["command"])
            check("-DBEAM_FILE_CONTRACT_TEST" not in arguments, "production file unit enables test controls")
    check_unit_linkage(args, "beam_file_component", args.test_file_component, links,
                       runtime_exports, unit_exports["file"])
    owned_objects.update(obj for _, obj in fixture_native)
    unowned = [(source, obj) for source, obj in commands
               if source in owned_sources and obj not in owned_objects]
    check(not unowned, f"extension sources also compile outside their owning targets: {unowned}")
    family_native = re.compile(r"(?:otp|beam)_(?:efile_|esock_|essio_|initESock|.*_nif_init|"
                               r"inet_driver_entry|buffer_stack_key|atomics_|counters_|"
                               r"persistent_term_|erts_internal_(?:atomics|counters)|erts_.*persistent)")
    check(not any(family_native.match(name) for name in runtime),
          "libbeam retains a family-owned native implementation")
    check(not any(name.startswith(("otp_preload", "beam_preload", "otp_pre_loaded", "beam_pre_loaded"))
                  for name in runtime), "libbeam retains a family preload table or module payload")
    check(not any("distribution_catalog" in name or "startup_catalog" in name or "cli_catalog" in name for name in runtime),
          "libbeam owns distribution composition")
    check(not any(name.startswith(("beam_distribution_", "beam_cli_recipe_")) for name in runtime),
          "libbeam links private host recipes")
    core_target, *core_objects = args.core_component
    check(core_target == "beam_core_shared_data_native" and links.count(core_target) == 1,
          "libbeam must link shared-data core exactly once")
    expected_core_sources = {(args.source_dir / "src/erts/emulator/beam" / name).resolve()
                            for name in ("erl_bif_atomics.c", "erl_bif_counters.c", "erl_bif_persistent.c")}
    core_compiled = [(source, obj) for source, obj in commands if source in expected_core_sources]
    check(len(core_compiled) == 3 and {source for source, _ in core_compiled} == expected_core_sources and
          {obj for _, obj in core_compiled} == {Path(obj).resolve() for obj in core_objects},
          f"shared-data core does not compile exactly once: {core_compiled}")
    core_definitions = collections.Counter(name for obj in core_objects
                                          for name in symbols(args, obj, "-g", "--defined-only"))
    for entry in ("erts_internal_atomics_new_2", "erts_internal_counters_new_1",
                  "erts_init_bif_persistent_term", "erts_bare_destroy_persistent_terms"):
        check(core_definitions[entry] == runtime[entry] == 1,
              f"shared core entry {entry} must be defined exactly once")
    core = [name for obj in args.core_preload
            for name in symbols(args, obj, "-g", "--defined-only")]
    core_modules = {name.removeprefix("core_preloaded_size_") for name in core
                    if name.startswith("core_preloaded_size_")}
    check(core_modules == CORE_MODULES, f"wrong shared core payloads: {core_modules}")
    check(core.count("core_pre_loaded") == runtime["core_pre_loaded"] == 1,
          "runtime must contain one shared core preload table")
    inventories = {
        "library": ["beam", "beam_list", "beam_map", "beam_bytes", "beam_text", "beam_format", "beam_time"],
        "boot": ["beam_return_boot", "beam_boot"],
        "services": ["beam_startup", "beam_host", "beam_output", "beam_code"],
    }
    for name, modules in inventories.items():
        root = args.base_inventory.parent / name
        check((root / "modules.txt").read_text() == "".join(module + "\n" for module in modules),
              f"wrong ordinary {name} manifest")
        payloads = {path.name for path in (root / "ebin").iterdir()}
        check(payloads == {module + ".beam" for module in modules}, f"wrong ordinary {name} payloads: {payloads}")
    check("beam_bytecode_verify" not in runtime, "libbeam contains the host bytecode verifier")
    check(not any(name.startswith(("bytecode_", "beam_base_")) for name in runtime),
          "libbeam contains ordinary bytecode or its table")
    needed, _ = dynamic_links(args, runtime_path)
    for name in ("base", "boot", "services"):
        check(f"libbeam-{name}.so" not in needed and f"beam_{name}_component" not in links and
              f"beam_{name}_bytecode" not in links, f"libbeam depends on ordinary {name} packaging")
    check("beam_vm_load_base" not in runtime_exports, "VM still exports product-specific base loading")
    check(not any("preloaded_prim_eval" in name for name in runtime),
          "prim_eval remains embedded in libbeam")


if __name__ == "__main__":
    main()
