"""Run complete unchanged VM suites through an attested installed OTP host."""

from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import time
import sys

from support.process import child_process, isolated_environment
from support.scratch import scratch
from .manifest import CASES, INPUTS, CORE_MODULES, SDK_ROOTS


UPSTREAM = "550d7b7898706c7822362c42e7b93120c1d1f29a"
HERE = Path(__file__).resolve().parent
ORIGIN_MODULES = ("ct", "ct_run", "compile", "test_server", "test_server_ctrl", "erl_error",
                  "io", "io_lib", "lists", "ordsets", "re", "rand", "math", "json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        require(name not in result, f"Duplicate JSON key: {name}")
        result[name] = value
    return result


def read_json(path):
    return json.loads(Path(path).read_text(), object_pairs_hook=unique_object)


def inventory(prefix):
    """Match the external OTP format-1 entry, mode and byte contract."""
    entries = {".": {"kind": "directory", "mode": format(stat.S_IMODE(prefix.stat().st_mode), "04o")}}
    for path in sorted(prefix.rglob("*")):
        mode = path.lstat().st_mode
        entry = {"mode": format(stat.S_IMODE(mode), "04o")}
        if stat.S_ISLNK(mode):
            entry.update(kind="symlink", target=str(path.readlink()))
            require(path.resolve().is_relative_to(prefix), f"Installation link escapes selected prefix: {path}")
        elif stat.S_ISREG(mode):
            entry.update(kind="file", sha256=digest(path))
        elif stat.S_ISDIR(mode):
            entry.update(kind="directory")
        else:
            raise ValueError(f"Unsupported installed entry: {path}")
        entries[path.relative_to(prefix).as_posix()] = entry
    return entries


def consumed_artifacts(artifacts):
    require(isinstance(artifacts, dict), "SDK artifacts must be a mapping")
    for prefix in ("core.", "core-source."):
        require({name[len(prefix):] for name in artifacts if name.startswith(prefix)} == CORE_MODULES,
                f"SDK {prefix} inventory differs from the nine VM core modules")
    roots = SDK_ROOTS | {name for name in artifacts if name.startswith(("header.", "link.static"))}
    required, visiting = set(), set()

    def visit(name):
        require(name in artifacts, f"Missing consumed SDK artifact: {name}")
        require(name not in visiting, f"Cyclic SDK artifact dependency: {name}")
        if name in required:
            return
        visiting.add(name)
        artifact = artifacts[name]
        require(isinstance(artifact, dict), f"Malformed SDK artifact: {name}")
        dependencies = artifact.get("requires")
        require(isinstance(dependencies, list) and all(isinstance(item, str) for item in dependencies),
                f"Malformed SDK artifact dependencies: {name}")
        require(len(set(dependencies)) == len(dependencies), f"Repeated SDK artifact dependency: {name}")
        for dependency in dependencies:
            visit(dependency)
        visiting.remove(name)
        required.add(name)

    for name in sorted(roots):
        visit(name)
    return required


def validate_inputs(sdk, prefix, provenance, version, build_id, flavor):
    require(bool(version), "Expected SDK version is required")
    require(bool(re.fullmatch(r"[0-9a-f]{64}", build_id)), "Expected SDK build identity must be SHA-256")
    require(flavor in ("emu", "jit"), "Expected flavor must be emu or jit")
    sdk, prefix, provenance = [Path(p).resolve(strict=True) for p in (sdk, prefix, provenance)]
    require(sdk.is_dir() and prefix.is_dir(), "SDK and OTP prefixes must be directories")
    require(not provenance.is_relative_to(prefix), "OTP provenance must be external to the prefix")
    require(not sdk.is_relative_to(prefix) and not prefix.is_relative_to(sdk),
            "SDK and OTP installations must be separate")
    record = read_json(provenance)
    require(record.get("format") == 1 and record.get("upstream_commit") == UPSTREAM,
            "Unsupported OTP provenance format or upstream pin")
    require(record.get("runtime") == "candidate" and record.get("flavor") == flavor,
            "OTP provenance must select the candidate and expected flavor")
    require(isinstance(record.get("prefix"), str) and Path(record["prefix"]).is_absolute(),
            "OTP provenance must record its original absolute prefix")
    build = record.get("build", {})
    require(build.get("linkage") == "static", "OTP provenance must attest static runtime linkage")
    selection = build.get("sdk_selection", {})
    require((selection.get("version"), selection.get("build_id"), selection.get("flavor"))
            == (version, build_id, flavor), "OTP SDK selection differs from independent expected identity")
    require(selection.get("upstream_commit") == UPSTREAM, "OTP SDK selection has the wrong upstream pin")
    manifest_path = sdk / "lib/beam/runtime-artifacts.json"
    manifest = read_json(manifest_path)
    require(manifest.get("format") == 1, "Unsupported SDK artifact manifest")
    require((manifest.get("version"), manifest.get("build-id"), manifest.get("flavor"))
            == (version, build_id, flavor), "SDK manifest differs from independent expected identity")
    for field in ("architecture", "triple"):
        require(selection.get(field) == manifest.get(field) and bool(manifest.get(field)),
                f"OTP/SDK {field} differs or is absent")
    hashes = selection.get("artifacts", {})
    require(isinstance(hashes, dict) and "runtime.static" in hashes,
            "OTP provenance does not attest the static runtime artifact")
    required = consumed_artifacts(manifest.get("artifacts"))
    require(required <= hashes.keys(),
            f"OTP provenance omits consumed SDK artifacts: {sorted(required - hashes.keys())}")
    for name, expected in hashes.items():
        require(isinstance(expected, str) and bool(re.fullmatch(r"[0-9a-f]{64}", expected)),
                f"Malformed SDK artifact digest: {name}")
        artifact = manifest.get("artifacts", {}).get(name, {})
        relative = Path(artifact.get("path", ""))
        require(str(relative) != "." and not relative.is_absolute() and ".." not in relative.parts,
                f"Invalid SDK artifact path: {name}")
        path = (sdk / relative).resolve(strict=True)
        require(path.is_relative_to(sdk), f"SDK artifact escapes selected prefix: {name}")
        require(artifact.get("sha256") == expected and digest(path) == expected,
                f"SDK artifact differs from OTP selection: {name}")
    require(manifest["artifacts"]["runtime.static"].get("kind") == "runtime-static",
            "Selected static runtime has the wrong artifact kind")
    require(inventory(prefix) == record.get("entries"),
            "OTP installation differs from attested entries, modes or bytes")
    for name in ("erl", "erlc"):
        command = prefix / "bin" / name
        require(command.resolve(strict=True).is_relative_to(prefix) and os.access(command, os.X_OK),
                f"Selected OTP command is missing, external or not executable: {name}")
    return {"sdk": selection, "installation": {"runtime": "candidate", "linkage": "static",
            "flavor": flavor, "prefix": str(prefix), "original_prefix": record["prefix"],
            "provenance_sha256": digest(provenance)},
            "sdk_manifest_sha256": digest(manifest_path), "selected_sdk_prefix": str(sdk)}


def erl_string(value):
    return json.dumps(str(value), ensure_ascii=False)


def expression(prefix, flavor, suite, stage, work, observer=None):
    """Check the live host before compiling and executing test-owned inputs."""
    origins = ",".join(ORIGIN_MODULES)
    sources = [stage / name for name in
               ("ts_install_cth.erl", "beam_vm_ct_event.erl", suite + ".erl")]
    files = "[" + ",".join(map(erl_string, sources)) + "]"
    expected = "[" + ",".join(CASES[suite]) + "]"
    code = (
        f"Prefix={erl_string(prefix)},Prefix=code:root_dir(),"
        f"{flavor}=erlang:system_info(emu_flavor),"
        '"29"=erlang:system_info(otp_release),2=erlang:system_info(schedulers_online),'
        'Host=filename:join([Prefix,"erts-"++erlang:system_info(version),"bin","beam.smp"]),'
        '{ok,Host}=file:read_link("/proc/self/exe"),'
        '{ok,Maps}=file:read_file("/proc/self/maps"),nomatch=binary:match(Maps,<<"libbeam.so">>),'
        f"Origins=maps:from_list([begin {{module,M}}=code:ensure_loaded(M),"
        'P=code:which(M),true=is_list(P),true=lists:prefix(Prefix++"/",P),'
        f"{{atom_to_binary(M),unicode:characters_to_binary(P)}} end || M <- [{origins}]]),"
        "preloaded=code:which(atomics),preloaded=code:which(counters),"
        f"ok=file:write_file({erl_string(work / 'preflight.json')},json:encode("
        '#{prefix=>unicode:characters_to_binary(Prefix),executable=>unicode:characters_to_binary(Host),'
        f'flavor=><<"{flavor}">>,otp_release=><<"29">>,schedulers_online=>2,modules=>Origins}})), '
        f"[begin case compile:file(F,[debug_info,return_errors,return_warnings,{{outdir,{erl_string(stage)}}}]) of "
        '{ok,_,_}->ok;{ok,_}->ok;Error->erlang:error({compile_failed,F,Error}) end end || F <- '
        f"{files}],true=code:add_patha({erl_string(stage)}),"
        f"[begin {{module,M}}=code:ensure_loaded(M),true=lists:prefix({erl_string(stage)}++\"/\","
        f"code:which(M)) end || M <- [ts_install_cth,beam_vm_ct_event,{suite}]],"
        f"Expected={expected},Expected={suite}:all(),"
        f"ok=file:write_file({erl_string(work / 'all.json')},json:encode([atom_to_binary(C)||C<-Expected])),"
        f"Result=ct:run_test([{{dir,{erl_string(stage)}}},{{suite,{suite}}},"
        f"{{auto_compile,false}},{{logdir,{erl_string(work / 'logs')}}},"
        f"{{event_handler,{{beam_vm_ct_event,[{erl_string(work / 'events.log')}]}}}}]),"
        f"ok=file:write_file({erl_string(work / 'ct-result.term')},io_lib:format(\"~tp.~n\",[Result])),"
        f"case Result of {{{len(CASES[suite])},0,{{0,0}}}}->halt(0);_->halt(1) end."
    )

    if sys.platform == 'darwin':
        require(observer is not None, "Darwin Common Test requires a selected native host observer")
        linux = ('{ok,Host}=file:read_link("/proc/self/exe"),'
                 '{ok,Maps}=file:read_file("/proc/self/maps"),nomatch=binary:match(Maps,<<"libbeam.so">>),')
        native = ('OriginPort=open_port({spawn_executable,' + erl_string(observer) + '},'
                  '[binary,exit_status,{args,["--images",os:getpid()]}]),'
                  'OriginRead=fun F(Acc)->receive '
                  '{OriginPort,{data,B}}->F(<<Acc/binary,B/binary>>);'
                  '{OriginPort,{exit_status,0}}->Acc '
                  'after 5000->error(host_observation_timeout) end end,'
                  '#{<<"executable">>:=OriginExe,<<"files">>:=OriginFiles}=json:decode(OriginRead(<<>>)),'
                  'OriginExe=unicode:characters_to_binary(Host),true=lists:member(OriginExe,OriginFiles),'
                  'false=lists:any(fun(Name)->lists:member(filename:basename(Name),'
                  '[<<"libbeam.so">>,<<"libbeam.dylib">>]) end,OriginFiles),')
        require(linux in code, "Missing native origin assertion")
        code = code.replace(linux, native)
    return code


def protocol(suite, all_cases, events, result_text):
    expected = CASES[suite]
    require(all_cases == expected, "Suite all/0 differs from the complete expected case inventory")
    require(events and events[0] == {"event": "handler_start"}
            and events[-1] == {"event": "handler_stop"}, "Common Test event recording is incomplete")
    cases = []
    configurations = set()
    for event in events[1:-1]:
        require(event.get("event") in ("tc_done", "tc_user_skip", "tc_auto_skip"),
                f"Unexpected event record: {event}")
        require(event.get("status") in ("passed", "failed", "user_skipped", "auto_skipped"),
                f"Malformed case status: {event}")
        if event["event"] in ("tc_user_skip", "tc_auto_skip"):
            require(event["status"] == event["event"][3:] + "ped",
                    f"Skip event has a conflicting status: {event}")
        if event.get("name") in ("init_per_suite", "end_per_suite"):
            # CT supplies these callbacks from ct_framework when an unchanged
            # suite has no corresponding function. They are configuration, not
            # cases; every real case must still identify the selected suite.
            require(event.get("suite") in (suite, "ct_framework"),
                    f"Unexpected configuration suite: {event}")
            require(event["status"] == "passed", f"Suite setup/teardown did not pass: {event}")
            require(event["name"] not in configurations, f"Repeated suite configuration event: {event}")
            configurations.add(event["name"])
            continue
        require(event.get("suite") == suite, f"Unexpected event suite: {event}")
        require(event.get("name") in expected, f"Unexpected case: {event}")
        cases.append({key: event.get(key, "") for key in ("name", "status", "reason")})
    require([case["name"] for case in cases] == expected,
            "Common Test events contain missing, repeated or reordered cases")
    match = re.fullmatch(r"\s*\{(\d+),(\d+),\{(\d+),(\d+)\}\}\.\s*", result_text)
    require(match is not None, "Missing or malformed Common Test aggregate result")
    passed, failed, user, auto = map(int, match.groups())
    observed = tuple(sum(c["status"] == status for c in cases)
                     for status in ("passed", "failed", "user_skipped", "auto_skipped"))
    require((passed, failed, user, auto) == observed, "Common Test aggregate and case events disagree")
    counts = dict(passed=passed, failed=failed, skipped=user + auto, user_skipped=user, auto_skipped=auto)
    return cases, counts, observed == (len(expected), 0, 0, 0)


def run_process(command, work, environment, timeout, inspect=None):
    """Keep protocol checks inside cleanup scope, including exited leaders."""
    with (work / "stdout.log").open("wb") as out, (work / "stderr.log").open("wb") as err:
        with child_process(command, cwd=work, env=environment, stdin=subprocess.DEVNULL,
                           stdout=out, stderr=err) as child:
            child.communicate(timeout=timeout)
            if inspect is not None:
                inspect(child.returncode)
            require(child.returncode == 0, f"Installed Common Test process exited {child.returncode}")


def execute(args):
    require(not args.test_dir.is_symlink(), "Test output cannot be a symbolic link")
    suite, work = args.suite, args.test_dir.resolve()
    for source in (args.sdk, args.otp_prefix, args.provenance, HERE.parent):
        source = source.resolve()
        require(not work.is_relative_to(source) and not source.is_relative_to(work),
                "Test output must be separate from selected installations, provenance and test sources")
    work.mkdir(parents=True, exist_ok=True)
    children = list(work.iterdir())
    if children:
        require((work / "cleanup.json").is_file()
                and read_json(work / "cleanup.json").get("payloads_removed") is True,
                "Existing payload must be investigated before test output is reused")
        for child in children:
            require(not child.is_symlink() and (child.is_file() or child.name == "logs"),
                    f"Unrecognized retained test output: {child}")
        for child in children:
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()
    report = {"suite": suite, "expected_cases": len(CASES[suite]), "expected_case_names": CASES[suite],
              "complete": False, "outcome": "failed", "cases": [], "suites": {},
              "normal_schedulers": 2, "timeout_seconds": 600}
    started = time.monotonic()
    with scratch(work):
        try:
            report.update(validate_inputs(args.sdk, args.otp_prefix, args.provenance,
                                          args.sdk_version, args.sdk_build_id, args.flavor))
            stage, logs = work / "suites", work / "logs"
            stage.mkdir()
            logs.mkdir()
            upstream = read_json(HERE / "upstream.json")
            require(upstream.get("upstream_commit") == UPSTREAM, "Test support provenance pin differs")
            hook = HERE.parent / "support/ts_install_cth.erl"
            require(digest(hook) == upstream["files"]["../support/ts_install_cth.erl"]["sha256"],
                    "Pinned Common Test hook changed")
            inputs = [HERE.parent / relative for relative in INPUTS[suite]]
            report["source_sha256"] = {str(p.relative_to(HERE.parent)): digest(p) for p in inputs}
            for source in inputs:
                shutil.copy2(source, stage / source.name)
            environment = isolated_environment(work)
            # Installed launchers and nested compiler invocations retain the explicit bound.
            environment["ERL_FLAGS"] = "+S2:2 +SDcpu2:2 +SDio2"
            observer = None
            if sys.platform == 'darwin':
                require(args.host_observer is not None, "Select --host-observer on Darwin")
                observer = work / 'host-observer'
                shutil.copy2(args.host_observer.resolve(strict=True), observer)
                report['host_observer_sha256'] = digest(observer)
            command = [str(args.otp_prefix.resolve() / "bin/erl"), "+S2:2", "+SDcpu2:2", "+SDio2",
                       "-noshell", "-noinput", "-eval",
                       expression(args.otp_prefix.resolve(), args.flavor, suite, stage, work, observer)]
            report["commands"] = [command]

            def inspect(returncode):
                report["returncode"] = returncode
                report["preflight"] = read_json(work / "preflight.json")
                all_cases = read_json(work / "all.json")
                events = [json.loads(line, object_pairs_hook=unique_object)
                          for line in (work / "events.log").read_text().splitlines()]
                cases, counts, complete = protocol(suite, all_cases, events,
                                                   (work / "ct-result.term").read_text())
                report.update(cases=cases, suites={suite: counts})
                require(complete, "Complete suite did not pass; failures and skips are not accepted")

            run_process(command, work, environment, 600, inspect)
            for source in inputs:
                require(digest(source) == report["source_sha256"][str(source.relative_to(HERE.parent))],
                        f"Original suite/support source changed during execution: {source}")
            # Runtime execution must not mutate the selected read-only installation.
            validate_inputs(args.sdk, args.otp_prefix, args.provenance,
                            args.sdk_version, args.sdk_build_id, args.flavor)
            report.update(complete=True, outcome="passed")
        except BaseException as error:
            report["reason"] = f"{type(error).__name__}: {error}"[:8192]
            raise
        finally:
            report["elapsed_seconds"] = time.monotonic() - started
            (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{suite}: {len(CASES[suite])} cases passed; {work / 'result.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--otp-prefix", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    parser.add_argument("--sdk-version", required=True)
    parser.add_argument("--sdk-build-id", required=True)
    parser.add_argument("--flavor", choices=("emu", "jit"), required=True)
    parser.add_argument("--host-observer", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--suite", choices=CASES)
    parser.add_argument("--test-dir", type=Path)
    args = parser.parse_args()
    if args.validate_only:
        validate_inputs(args.sdk, args.otp_prefix, args.provenance,
                        args.sdk_version, args.sdk_build_id, args.flavor)
    else:
        if args.suite is None or args.test_dir is None:
            parser.error("Execution requires --suite and --test-dir")
        execute(args)


if __name__ == "__main__":
    main()
