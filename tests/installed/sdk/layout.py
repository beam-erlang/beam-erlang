"""Public SDK assets and explicitly selected runtime inventories."""
import hashlib
import json
import os
import re

from support.process import check
from support.artifacts import relative as artifact_relative


def check_installation(prefix, bundled=True):
    required = (
        "bin/beam", "bin/beam-launch", artifact_relative(prefix,"runtime"), artifact_relative(prefix,"runtime.static"), "libexec/beam/erl_child_setup",
        "include/beam/beam.h", "include/beam/runtime.h", "include/beam/boot.h",
        "include/beam/native.h", "include/beam/zlib.h", "include/beam/sdk/bytecode.h", "include/beam/sdk/inventories.h",
        "share/beam/sdk/beam_bytecode_verify.c", "lib/cmake/BEAM/BEAMConfig.cmake",
    )
    for relative in required:
        check((prefix / relative).is_file(), f"missing public SDK asset: {relative}")
    for retired in ("beam.h", "beam_bytecode.h", "beam_base_inventory.h",
                    "beam_boot_inventory.h", "beam_services_inventory.h", "beam/nif/beam_native.h"):
        check(not (prefix / "include" / retired).exists(), f"retired header installed: {retired}")
    check(os.access(prefix / "libexec/beam/erl_child_setup", os.X_OK),
          "installed native helper is not executable")
    check_runtime_artifacts(prefix)
    tools = prefix / "lib/beam/tools"
    if bundled:
        launcher = prefix / "bin/beam-pkg"
        check(launcher.is_file() and not launcher.is_symlink(), "package command must be a generic process launcher")
        config = (prefix / "bin/beam-pkg.beam-launcher").read_text().splitlines()
        check(config[:3] == ["beam-launcher-v1", "relative", "beam"], "invalid bundled runtime binding")
        descriptor = (prefix / "bin" / config[3]).resolve()
        check(descriptor.is_relative_to(prefix.resolve()), "bundled descriptor escapes installation")
        import importlib.util
        spec = importlib.util.spec_from_file_location("descriptor", prefix / "share/beam/tools/beam-application.py")
        codec = importlib.util.module_from_spec(spec); spec.loader.exec_module(codec)
        application = codec.validate(descriptor)
        check(application[2] == b"beam-pkg" and application[5][0] == "command", "invalid bundled package application")
        modules = (descriptor.parent / "modules.txt").read_text().splitlines()
        check({m[1].decode() for m in application[8]} == set(modules), "package descriptor/inventory disagreement")
        check({p.name for p in (descriptor.parent / "ebin").iterdir()} == {m + ".beam" for m in modules}, "package artifacts disagree with inventory")
    else:
        check(not (prefix / "bin/beam-pkg").exists(), "SDK-only configuration installed package policy")
    compiler = tools / "compiler"
    modules = (compiler / "modules.txt").read_text().splitlines()
    check(len(modules) == len(set(modules)) and {"compile", "beam_compiler_engine"} <= set(modules),
          "invalid compiler module inventory")
    check({path.name for path in (compiler / "ebin").iterdir()} ==
          {f"{module}.beam" for module in modules}, "compiler artifacts disagree with inventory")
    check(not (prefix / "lib/beam/apps").exists() or {p.name for p in (prefix / "lib/beam/apps").iterdir()} == {"beam-pkg"}, "unexpected bundled applications remain in BEAM")
    check({p.name for p in (prefix / "include/beam/nif").iterdir()} == {"erl_nif.h", "erl_driver.h", "erl_drv_nif.h", "erl_nif_api_funcs.h", "erl_int_sizes_config.h"}, "wrong public native header inventory")
    check({path.name for path in (prefix / "lib/beam/ebin").iterdir()} ==
          {"beam_host.beam"},
          "installation contains duplicated component module artifacts")
    inventories = {
        "library": ["beam", "beam_list", "beam_map", "beam_bytes", "beam_text", "beam_format", "beam_time"],
        "boot": ["beam_return_boot", "beam_boot"],
        "services": ["beam_startup", "beam_host", "beam_output", "beam_code"],
        "startup": ["beam_startup"], "host": ["beam_host"],
        "output": ["beam_output"], "code": ["beam_code"],
    }
    for name, modules in inventories.items():
        inventory = prefix / "lib/beam" / name
        check((inventory / "modules.txt").read_text() == "".join(module + "\n" for module in modules),
              f"wrong ordinary {name} manifest")
        check({p.relative_to(inventory).as_posix() for p in inventory.rglob("*") if p.is_file()} ==
              {"modules.txt"} | {f"ebin/{module}.beam" for module in modules}, f"wrong ordinary {name} files")
    for name in ("base", "boot", "services"):
        check(not (prefix / f"lib/libbeam-{name}.so").exists(), f"ordinary {name} has a native wrapper")
    root = prefix / "lib/beam/runtime/beam"
    modules = {"prim_zip", "erl_tracer"}
    inventories = {
        "run": {"beam_source_run"},
        "application": {"beam_application_run"},
        "cli": {"beam_run_io", "error_handler", "io", "io_lib", "io_lib_pretty", "erl_scan",
                "binary", "io_lib_format", "io_lib_fread", "records", "string", "unicode",
                "unicode_util", "lists", "maps"},
        "interactive": {"beam_interactive", "shell", "shell_default", "erl_eval", "erl_parse",
            "erl_pp", "erl_features", "erl_lint", "erl_expand_records", "dict", "digraph",
            "digraph_utils", "erl_anno", "erl_bits", "erl_error", "erl_internal", "erl_stdlib_errors",
            "erl_posix_msg", "ets", "eval_bits", "math", "gb_sets", "gb_trees", "orddict", "ordsets",
            "otp_internal", "proplists", "queue", "sets", "sofs", "prim_eval"},
    }
    expected = ({"modules.txt"} | {f"ebin/{m}.beam" for m in modules} |
                {"priv/erl_tracer.so"})
    for name, module_names in inventories.items():
        inventory = (root / name / "modules.txt").read_text().splitlines()
        check(len(inventory) == len(module_names) and set(inventory) == module_names,
              f"unexpected {name} support inventory")
        expected |= {f"{name}/modules.txt"} | {f"{name}/ebin/{m}.beam" for m in module_names}
    accepted = (expected,)
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    check(actual in accepted, f"unexpected installed BEAM component artifacts: {sorted(actual)}")
    # check_runtime_artifacts has already verified the exact nine source IDs and hashes.
    artifacts = json.loads((prefix / "lib/beam/runtime-artifacts.json").read_text())["artifacts"]
    inspection_sources = {entry["path"] for entry in artifacts.values() if entry["kind"] == "core-source"}
    for path in prefix.rglob("*"):
        check(path in {prefix / artifact_relative(prefix, name) for name in ("runtime.static", "host.static", "library.file.static", "library.network.static", "library.zlib.static")}
              or path.suffix not in {".o", ".obj", ".a"},
              f"installation contains development native objects: {path}")
        relative = path.relative_to(prefix)
        check(path.suffix not in {".c", ".cc", ".cpp", ".erl"} or
              relative.as_posix() in inspection_sources | {"share/beam/sdk/beam_bytecode_verify.c", "share/beam/sdk/inventories.c",
                  "share/doc/beam/examples/first-call/main.c", "share/doc/beam/examples/first-call/answer.erl"},
              f"installation exposes implementation sources: {relative}")
    for name in ("beam_component_internal.h", "beam_composition_internal.h", "erl_vm.h"):
        check(not list((prefix / "include").rglob(name)), f"private VM header installed: {name}")
    # Public imported targets are exercised by the consumer CMake projects.
    # Generator-specific export filenames are not a distribution contract.



def check_runtime_artifacts(prefix):
    metadata = (prefix / "lib/beam/sdk.toml").read_bytes()
    sdk = dict(re.findall(r'^([a-z-]+) = "([^"]+)"$', metadata.decode(), re.MULTILINE))
    manifest = json.loads((prefix / "lib/beam/runtime-artifacts.json").read_text())
    check(set(manifest) == {"format", "version", "flavor", "build-id", "architecture",
                           "triple", "sdk-metadata-sha256", "artifacts"},
          "unexpected runtime artifact metadata schema")
    check(manifest["format"] == 1, "unsupported runtime artifact metadata format")
    for field in ("flavor", "build-id", "architecture", "triple"):
        check(manifest[field] == sdk[field], f"runtime artifact {field} disagrees with SDK")
    check(manifest["version"] == sdk["identity"], "runtime artifact version disagrees with SDK")
    check(manifest["sdk-metadata-sha256"] == hashlib.sha256(metadata).hexdigest(),
          "runtime artifact inventory is not bound to the installed SDK metadata")
    configurations = list((prefix / "lib/cmake/BEAM").glob("BEAMStaticTargets-*.cmake"))
    check(len(configurations) == 1, "expected one installed static linkage configuration")
    host_configurations = list((prefix / "lib/cmake/BEAM").glob("BEAMHostTargets-*.cmake"))
    check(len(host_configurations) == 1, "expected one installed Host linkage configuration")
    expected = {
        "sdk.pkgconfig": ("consumer-metadata", "lib/pkgconfig/beam.pc", []),
        "api.beam": ("embedding-header", "include/beam/beam.h", ["api.runtime"]),
        "api.zlib": ("embedding-header", "include/beam/zlib.h", []),
        "api.runtime": ("embedding-header", "include/beam/runtime.h", []),
        "api.boot": ("embedding-header", "include/beam/boot.h", ["api.host"]),
        "api.host": ("embedding-header", "include/beam/host.h", ["api.library", "api.native_image"]),
        "api.library": ("embedding-header", "include/beam/library.h", ["api.runtime"]),
        "api.native_image": ("embedding-header", "include/beam/native_image.h", ["api.runtime"]),
        "api.bytecode": ("embedding-header", "include/beam/sdk/bytecode.h", ["api.runtime"]),
        "api.inventories": ("embedding-header", "include/beam/sdk/inventories.h", ["api.bytecode"]),
        "sdk.inventories": ("embedding-source", "share/beam/sdk/inventories.c", ["api.inventories"]),
        "sdk.bytecode.private": ("embedding-source", "share/beam/sdk/beam_bytecode_internal.h", ["api.bytecode"]),
        "api.verifier": ("embedding-source", "share/beam/sdk/beam_bytecode_verify.c", ["api.bytecode", "sdk.bytecode.private"]),
        "sdk.native": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMNif.cmake", []),
        "sdk.library": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMLibrary.cmake", []),
        "sdk.library.author": ("consumer-tool", "share/beam/tools/beam-library.py", []),
        "sdk.target.check": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMTarget.cmake", []),
        "sdk.application": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMiOS.cmake", []),
        "sdk.android": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMAndroid.cmake", []),
        "sdk.android.native": ("consumer-metadata", "lib/cmake/BEAM/support/BEAMVerifyAndroidNative.cmake", []),
        "sdk.android.assembly": ("consumer-tool", "share/beam/tools/beam-android.py", []),
        "sdk.target": ("target-metadata", "lib/beam/target.toml", ["compiler.generation"]),
        "compiler.generation": ("compiler-provenance", "lib/beam/compiler-generation.json", []),
        "command.beam": ("executable-helper", "bin/beam", ["runtime"]),
        "command.launcher": ("executable-helper", "bin/beam-launch", []),
        "runtime": ("runtime", artifact_relative(prefix,"runtime"), []),
        "runtime.static": ("runtime-static", artifact_relative(prefix,"runtime.static"),
                           ["link.static", "link.static.configuration", "link.static.exports"]),
        "link.static": ("link-support", "lib/cmake/BEAM/BEAMStaticTargets.cmake", []),
        "link.static.configuration": ("link-support", configurations[0].relative_to(prefix).as_posix(), []),
        "link.static.exports": ("link-support", artifact_relative(prefix,"link.static.exports"), []),
        "host.static": ("host-static", "lib/libbeam-host.a",
                        ["api.boot", "api.host", "api.library", "api.native_image", "link.host", "link.host.configuration"]),
        "link.host": ("link-support", "lib/cmake/BEAM/BEAMHostTargets.cmake", []),
        "link.host.configuration": ("link-support", host_configurations[0].relative_to(prefix).as_posix(), []),
        "helper.child_setup": ("executable-helper", "libexec/beam/erl_child_setup", []),
    }
    core = {"erts_code_purger", "erlang", "erts_internal", "erts_literal_area_collector",
            "erts_trace_cleaner", "erts_dirty_process_signal_handler",
            "atomics", "counters", "persistent_term"}
    check({p.relative_to(prefix / "lib/beam/core").as_posix()
           for p in (prefix / "lib/beam/core").rglob("*") if p.is_file()} ==
          {f"ebin/{name}.beam" for name in core} | {f"src/{name}.erl" for name in core}, "unexpected embedded-core inventory")
    for name in core:
        expected[f"core.{name}"] = ("core-module", f"lib/beam/core/ebin/{name}.beam", [])
        expected[f"core-source.{name}"] = ("core-source", f"lib/beam/core/src/{name}.erl", [])
    for name, dependencies in {
        "erl_nif": ["header.erl_drv_nif", "header.erl_nif_api_funcs"],
        "erl_driver": ["header.erl_drv_nif"],
        "erl_drv_nif": ["header.erl_int_sizes_config"],
        "erl_nif_api_funcs": [], "erl_int_sizes_config": [],
    }.items():
        expected[f"header.{name}"] = ("native-header", f"include/beam/nif/{name}.h", dependencies)
    expected["header.beam_native"] = ("native-header", "include/beam/native.h", [])
    for name in ("file", "network", "zlib"):
        expected[f"library.{name}.shared"] = ("native-library", artifact_relative(prefix,f"library.{name}.shared"), ["runtime"])
        expected[f"library.{name}.static"] = ("native-library-static", f"lib/libbeam-{name}.a",
            [f"library.{name}.header", f"link.library.{name}.static", f"link.library.{name}.static.configuration"])
        expected[f"library.{name}.header"] = ("embedding-header", f"include/beam/libraries/{name}.h", ["api.library"])
        for form in ("shared", "static"):
            stem = f"BEAM{name}{form.title()}Targets"
            configs = list((prefix / "lib/cmake/BEAM").glob(stem + "-*.cmake"))
            check(len(configs) == 1, f"expected one {name} {form} configuration")
            expected[f"link.library.{name}.{form}"] = ("link-support", f"lib/cmake/BEAM/{stem}.cmake", [])
            expected[f"link.library.{name}.{form}.configuration"] = ("link-support", configs[0].relative_to(prefix).as_posix(), [])
    licenses = {"license.BEAM": "LICENSE.txt", "license.authors": "AUTHORS", "license.zlib": "zlib/LICENSE.txt"}
    for name in ("asmjit/LICENSE.md", "ryu/LICENSE-Apache2", "ryu/LICENSE-Boost", "ryu/xcharconv_ryu.h.sha.license", "pcre/LICENCE"):
        licenses["license."+name] = name
    root_notices = ['BSD-2-Clause.txt', 'BSD-3-Clause.txt', 'LicenseRef-IETF-MIB.txt', 'LicenseRef-RSA-PKCS3.txt', 'LicenseRef-RSA-PKCS5v2-0.txt', 'LicenseRef-scancode-ietf-trust.txt', 'LicenseRef-scancode-ietf.txt', 'LicenseRef-scancode-w3c-docs-19990405.txt', 'LicenseRef-scancode-wxwindows-free-doc-3.txt', 'MIT.txt', 'SGI-B-2.0.txt', 'W3C-19980720.txt']
    for name in root_notices:
        licenses["license.root."+name] = "LICENSES/"+name
    for name,path in licenses.items():
        expected[name] = ("license", "share/licenses/BEAM/"+path, [])
    check({x.relative_to(prefix/"share/licenses/BEAM").as_posix() for x in (prefix/"share/licenses/BEAM").rglob("*") if x.is_file()} == set(licenses.values()), "unexpected installed license inventory")
    check(set(manifest["artifacts"]) == set(expected), "unexpected runtime artifact IDs")
    for name, (kind, path, dependencies) in expected.items():
        artifact = manifest["artifacts"][name]
        check(set(artifact) == {"kind", "path", "sha256", "requires"},
              f"invalid runtime artifact schema: {name}")
        check((artifact["kind"], artifact["path"], artifact["requires"]) ==
              (kind, path, dependencies), f"wrong runtime artifact declaration: {name}")
        check(artifact["sha256"] == hashlib.sha256((prefix / path).read_bytes()).hexdigest(),
              f"runtime artifact hash mismatch: {name}")
