#!/usr/bin/env python3
"""Prepare and supervise the private pinned ERTS emulator test environment."""
import argparse
from contextlib import contextmanager
import datetime
from functools import wraps
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import time
import uuid


WINDOWS = os.name == "nt"


def program(name):
    return name + ".exe" if WINDOWS else name


def candidate_program():
    return program("candidate")


def vm_path(value):
    """Erlang's Win32 filename module exposes canonical forward-slash paths."""
    text = value.as_posix() if isinstance(value, Path) else str(value)
    if WINDOWS and re.match(r"^[A-Za-z]:/", text):
        text = text[0].lower() + text[1:]
    return text


@contextmanager
def lifecycle_lock(path, exclusive):
    """Keep preparation and execution images stable on both native hosts."""
    with open(path, "a+b") as lock:
        if WINDOWS:
            import msvcrt
            if not path.stat().st_size:
                lock.write(b"\0")
                lock.flush()
            deadline = time.monotonic() + 60
            while True:
                lock.seek(0)
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Timed out waiting for emulator lifecycle lock")
                    time.sleep(0.1)
            try:
                yield
            finally:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH)
            yield


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class ExecutionIntegrityError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def execution_input_identity(source_root, relative, identity):
    """Same-host receipts require one exact original source-path input key."""
    expected = str(child(source_root, relative))
    inputs = identity.get("inputs", {})
    if not isinstance(inputs, dict):
        raise RuntimeError("Execution input identity is missing: " + relative)
    matches = [key for key in inputs if isinstance(key, str) and Path(key).is_absolute()
               and os.path.normcase(str(Path(key).resolve())) == os.path.normcase(expected)]
    if matches != [expected] or not re.fullmatch(r"[0-9a-f]{64}", str(inputs.get(expected, ""))):
        raise RuntimeError("Execution input identity is missing or ambiguous: " + relative)
    return {"path": expected, "sha256": inputs[expected]}


def execution_policy_identity(source_root, evidence, identity):
    recorded = execution_input_identity(source_root, "tests/emulator/emulator_skip_policy.json", identity)
    path = evidence / "execution-skip-policy.json"
    if sha(path) != recorded["sha256"]:
        raise ExecutionIntegrityError("execution-input-drift", "Captured execution skip policy differs from execution identity")
    pin = identity.get("upstream_commit")
    if not isinstance(pin, str) or not re.fullmatch(r"[0-9a-f]{40}", pin):
        raise RuntimeError("Execution upstream identity is missing or invalid")
    if read_json(path).get("upstream_commit") != pin:
        raise ExecutionIntegrityError("execution-input-drift", "Captured execution skip policy upstream pin differs from execution identity")
    return recorded


def guard_identity(source_root, rule, execution_identity=None):
    """Match pinned source bytes, allowing only Git's CRLF checkout conversion."""
    path = child(source_root, rule["source"])
    raw = path.read_bytes()
    canonical = raw.replace(b"\r\n", b"\n")
    actual = hashlib.sha256(raw).hexdigest()
    normalized = hashlib.sha256(canonical).hexdigest()
    recorded = None
    if execution_identity is not None:
        recorded = execution_input_identity(source_root, rule["source"], execution_identity)
        if actual != recorded["sha256"]:
            raise ExecutionIntegrityError("execution-input-drift", "Skip policy guard source differs from execution identity: " + rule["id"])
    if rule["source_sha256"] not in (actual, normalized) or rule["guard"] not in canonical.decode("utf-8"):
        raise RuntimeError("Skip policy source identity changed: " + rule["id"])
    result = {"source": rule["source"], "source_checkout_sha256": actual,
              "source_canonical_sha256": normalized,
              "checkout_conversion": "crlf-to-lf" if actual != normalized else "none"}
    if recorded is not None:
        result["execution_input"] = recorded
    return result


def child(root, relative):
    result = (root / relative).resolve()
    if not result.is_relative_to(root.resolve()):
        raise RuntimeError("Path escapes its owned root: " + str(relative))
    return result


def prepared_suite_names(corpus):
    """Expand the prepared inventory once for execution, auditing and discovery."""
    if not isinstance(corpus, dict):
        raise RuntimeError("Invalid prepared suite inventory")
    names = []
    for key, suffix in (("suites", ""), ("no_opt", "_no_opt_SUITE"),
                        ("stripped_types", "_stripped_types_SUITE")):
        values = corpus.get(key)
        if not isinstance(values, list) or (key == "suites" and not values):
            raise RuntimeError("Invalid prepared suite inventory: " + key)
        pattern = r"[a-zA-Z][a-zA-Z0-9_]*_SUITE" if key == "suites" else r"[a-zA-Z][a-zA-Z0-9_]*"
        for value in values:
            if not isinstance(value, str) or not re.fullmatch(pattern, value):
                raise RuntimeError("Invalid prepared suite name in " + key + ": " + repr(value))
            if suffix and (value.endswith("_SUITE") or value + "_SUITE" not in corpus["suites"]):
                raise RuntimeError("Prepared variant lacks its ordinary suite: " + value)
            names.append(value + suffix)
    if len(set(names)) != len(names):
        raise RuntimeError("Duplicate prepared suite identities")
    return sorted(names)


def cmake_bracket(value):
    """Quote one literal CMake argument without expansion or shell parsing."""
    equals = ""
    while "]" + equals + "]" in value:
        equals += "="
    # CMake discards an initial newline after an opening bracket. Supply that
    # newline ourselves so even values beginning with a newline remain literal.
    return "[" + equals + "[\n" + value + "]" + equals + "]"


def write_suite_ctest(args, corpus):
    """Publish a separate CTest tree; root CTest never traverses this catalog."""
    suites = prepared_suite_names(corpus)
    catalog = args.full_root / "suites/CTestTestfile.cmake"
    lines = ["# Prepared emulator suite diagnostics. Not part of root CTest traversal."]
    for suite in suites:
        name = "emulator.suite." + suite
        command = [sys.executable, "-B", str(args.full_root / "run.py"), "--suite", suite]
        lines.append("add_test(" + " ".join(cmake_bracket(value) for value in [name, *command]) + ")")
        lines.append("set_tests_properties(" + cmake_bracket(name)
                     + " PROPERTIES LABELS " + cmake_bracket("emulator-suite;" + args.flavor)
                     + " RUN_SERIAL TRUE TIMEOUT 21660 SKIP_RETURN_CODE 77)")
    catalog.parent.mkdir(parents=True, exist_ok=True)
    temporary = catalog.with_name(".CTestTestfile-" + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_text("\n".join(lines) + "\n")
        temporary.replace(catalog)
    finally:
        temporary.unlink(missing_ok=True)
    return catalog


def scope_manifest(source_root):
    path = source_root / "tests/emulator/emulator_scope.json"
    scope = read_json(path)
    if scope.get("schema_version") != 1 or not scope.get("directory") or not scope.get("exclusions"):
        raise RuntimeError("Invalid functional-scope manifest: " + str(path))
    seen = set()
    for item in scope["exclusions"]:
        if item["id"] in seen:
            raise RuntimeError("Duplicate scope exclusion: " + item["id"])
        seen.add(item["id"])
        if item["origin"] not in ("base-spec", "companion-spec"):
            raise RuntimeError("Unknown scope origin: " + item["id"])
        if item["directive"] not in ("skip_cases", "skip_groups"):
            raise RuntimeError("Unknown scope directive: " + item["id"])
        if item["directive"] == "skip_cases" and "case" not in item:
            raise RuntimeError("Scope case exclusion lacks a case: " + item["id"])
        if item["directive"] == "skip_groups" and "group" not in item:
            raise RuntimeError("Scope group exclusion lacks a group: " + item["id"])
        if any(character in item["reason"] for character in "\"\n\\"):
            raise RuntimeError("Scope reason cannot be rendered as an Erlang string: " + item["id"])
        if "required_flavor" in item and item["required_flavor"] not in ("emu", "jit"):
            raise RuntimeError("Unknown required flavor: " + item["id"])
    return path, scope


def check_scope_policy(source_root, scope):
    policy = read_json(source_root / "tests/emulator/emulator_skip_policy.json")
    rules = {rule["id"]: rule for rule in policy["rules"] if rule.get("class") == "scope"}
    items = {item["id"]: item for item in scope["exclusions"]}
    if set(rules) != set(items):
        raise RuntimeError("Scope policy rules and manifest ids differ")
    for item_id, item in items.items():
        rule = rules[item_id]
        if rule.get("suite") != item["suite"] or rule.get("condition", {}).get("kind") != "always":
            raise RuntimeError("Scope rule does not match its manifest entry: " + item_id)
        if item.get("case") and rule.get("case") != item["case"]:
            raise RuntimeError("Scope case mismatch: " + item_id)
        if item.get("group") and rule.get("group") != item["group"]:
            raise RuntimeError("Scope group mismatch: " + item_id)
        if re.fullmatch(rule["reason_regex"], item["reason"]) is None:
            raise RuntimeError("Scope reason does not match its policy rule: " + item_id)


def render_scope_spec(scope):
    lines = ["%% Project functional-scope exclusions. emulator.spec stays unchanged."]
    for item in scope["exclusions"]:
        if item["origin"] != "companion-spec":
            continue
        target = item["case"] if item["directive"] == "skip_cases" else item["group"]
        lines.append('{%s,"%s",%s,[%s],"%s"}.' % (
            item["directive"], scope["directory"], item["suite"], target, item["reason"]))
    if len(lines) == 1:
        raise RuntimeError("Functional-scope manifest has no companion exclusions")
    return "\n".join(lines) + "\n"


def compiler_variables(args):
    path = args.native_root / "compiler_config" / "variables"
    if not path.is_file():
        raise RuntimeError("Missing compiler configuration: " + str(path))
    match = re.fullmatch(r"\{'CC', \"([^\"]+)\"\}\.\n", path.read_text())
    if not match:
        raise RuntimeError("Compiler configuration must contain one absolute CC term: " + str(path))
    compiler = match.group(1)
    if not Path(compiler).is_absolute() or not Path(compiler).is_file():
        raise RuntimeError("Selected C compiler is not an absolute existing file: " + compiler)
    if not WINDOWS and re.search(r"[\s\"'`$\\;&|<>()*?]", compiler):
        raise RuntimeError("port_SUITE:otp_3906 cannot quote this compiler path: " + compiler)
    recorded = read_json(args.native_manifest).get("toolchain", {}).get("CMAKE_C_COMPILER", "")
    if os.path.normcase(os.path.abspath(recorded)) != os.path.normcase(os.path.abspath(compiler)):
        raise RuntimeError("Compiler configuration does not match the native toolchain: " + recorded)
    return path, compiler


def helper_aliases(bindir, build_type):
    """Validate link identity separately: sha() follows symbolic links."""
    aliases = {}
    if build_type == "debug" and not WINDOWS:
        alias = bindir / "erl_child_setup.debug"
        if not alias.is_symlink() or os.readlink(alias) != "erl_child_setup":
            raise RuntimeError("Debug helper must be a relative alias to erl_child_setup: " + str(alias))
        aliases[alias.name] = "erl_child_setup"
    return aliases


def support_files(args):
    """Read the selected imports emitted from the owning CMake source lists."""
    paths = args.support_files.read_text().splitlines()
    if not paths:
        raise RuntimeError("No imported emulator support files were selected")
    for relative in paths:
        path = Path(relative)
        if path.is_absolute() or not path.parts or path.parts[0] not in ("lib", "erts") or ".." in path.parts:
            raise RuntimeError("Invalid imported test support path: " + relative)
    return paths


def input_files(args):
    source = args.source_root
    support = source / "tests/emulator/support"
    pin = (source / "UPSTREAM_COMMIT").read_text().strip()
    files = {}
    for relative in support_files(args):
        path = child(support, relative)
        files[str(path)] = sha(path)
    # Hash current inputs instead of keeping a second, checked-in hash inventory.
    for root in (source / "erts/emulator/test", source / "erts/preloaded/src",
                 source / "erts/preloaded/ebin", source / "bootstrap"):
        for path in root.rglob("*"):
            if path.is_file():
                files[str(path)] = sha(path)
    for name in ("emulator/full_emulator.py", "emulator/beam_emulator_harness.erl",
                 "emulator/beam_emulator_full_prepare.erl", "emulator/beam_emulator_full_run.erl",
                 "emulator/beam_emulator_focus.erl", "cmake/RunEmulator.py.in",
                 "emulator/beam_emulator_events.erl", "emulator/emulator_skip_policy.json",
                 "emulator/emulator_scope.json",
                 "CMakeLists.txt", "cmake/FullEmulatorTests.cmake", "cmake/FullEmulatorNative.cmake",
                 "emulator/emulator_process.c"):
        path = source / "tests" / name
        files[str(path)] = sha(path)
    runtime_inputs = [args.runtime_bin / program("beam")]
    if WINDOWS:
        runtime_inputs += [args.runtime_bin / program(name) for name in ("beam.smp", "beam." + args.flavor)]
        if args.build_type == "debug":
            runtime_inputs += [args.runtime_bin / (name + ".pdb") for name in ("beam", "beam.smp", "beam." + args.flavor)]
        runtime_inputs += sorted((source / "tests/emulator").glob("*windows*.c"))
        runtime_inputs += sorted((source / "tests/emulator").glob("*windows*.h"))
    else:
        runtime_inputs.append(args.runtime_bin / "erl_child_setup")
    for path in (source / "UPSTREAM_COMMIT", args.support_files, args.native_manifest,
                 args.process_tool, *runtime_inputs):
        files[str(path)] = sha(path)
    native = read_json(args.native_manifest)
    for item in native["artifacts"]:
        path = child(args.native_root, item["path"])
        files[str(path)] = sha(path)
        resolved = []
        for relative in item.get("sources", []):
            path = Path(relative)
            if not path.is_absolute():
                path = child(source, relative)
                if not path.is_file():
                    path = child(support, relative)
            if not path.is_file():
                raise RuntimeError("Missing declared native source: " + str(relative))
            files[str(path)] = sha(path)
            resolved.append({"declared": relative, "path": str(path), "sha256": files[str(path)]})
        item["resolved_inputs"] = resolved
    variables, _compiler = compiler_variables(args)
    files[str(variables)] = sha(variables)
    _scope_manifest, scope = scope_manifest(source)
    check_scope_policy(source, scope)
    # Audited guards can live outside the corpus, including retained VM sources.
    policy = read_json(source / "tests/emulator/emulator_skip_policy.json")
    for relative in sorted({rule["source"] for rule in policy["rules"] + policy.get("empty_suite_rules", [])}):
        path = child(source, relative)
        files[str(path)] = sha(path)
    identity = {"profile": "full", "flavor": args.flavor, "build_type": args.build_type,
                "helper_aliases": helper_aliases(args.runtime_bin, args.build_type), "upstream_commit": pin,
                "inputs": dict(sorted(files.items())), "native": native}
    if WINDOWS:
        identity["child_helper"] = "windows-no-erl_child_setup"
    identity["fingerprint"] = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    return identity


def validate_execution_inputs(args, identity):
    """Reject drift in the complete source/support closure before accepting a run."""
    rebuild = "cmake --build " + shlex.quote(str(args.binary_root)) + " --target beam_emulator_full_test_support"
    try:
        current = input_files(args)
    except Exception as error:
        raise ExecutionIntegrityError("execution-input-unverifiable", "Execution input identity could not be revalidated: " + str(error)
                                      + "; run: " + rebuild) from error
    if current["fingerprint"] != identity["fingerprint"]:
        raise ExecutionIntegrityError("execution-input-drift", "Execution inputs changed during emulator execution; expected "
                                      + identity["fingerprint"] + ", observed " + current["fingerprint"]
                                      + "; run: " + rebuild)
    return current


def integrity_failure_code(error):
    if isinstance(error, ExecutionIntegrityError):
        return error.code
    text = str(error)
    for message, code in (("Execution inputs changed during emulator execution", "execution-input-drift"),
                          ("Execution input identity could not be revalidated", "execution-input-unverifiable"),
                          ("Run-owned processes remain", "cleanup-incomplete"),
                          ("Windows process job cleanup failed", "cleanup-incomplete")):
        if message in text:
            return code
    return None


def note_integrity_failure(receipt, error):
    code = integrity_failure_code(error)
    if code:
        failures = receipt.setdefault("execution_integrity_failures", [])
        if code not in failures:
            failures.append(code)
        receipt["qualification_eligible"] = False


def execution_integrity_failures(evidence):
    """A later policy review or cleanup cannot erase original run failures."""
    failures = []
    cleanup_path = evidence / "cleanup.json"
    if not cleanup_path.is_file():
        failures.append("Missing execution cleanup evidence")
    else:
        cleanup_result = read_json(cleanup_path)
        if not isinstance(cleanup_result.get("remaining"), list) or cleanup_result["remaining"]:
            failures.append("Execution left owned processes or incomplete cleanup evidence")
        if "complete" in cleanup_result and cleanup_result["complete"] is not True:
            failures.append("Execution cleanup did not complete")
    receipts = [path for path in (evidence / "result.json", evidence / "failure.json") if path.is_file()]
    if not receipts:
        failures.append("Missing original controller exit evidence")
    for path in receipts:
        receipt = read_json(path)
        exit_code = receipt.get("controller_exit_code")
        if type(exit_code) is not int or exit_code != 0:
            failures.append(path.name + ": candidate controller did not exit successfully")
        codes = receipt.get("execution_integrity_failures", [])
        if not isinstance(codes, list):
            failures.append(path.name + ": invalid execution-integrity failure record")
        elif codes:
            failures.append(path.name + ": immutable execution-integrity failure " + repr(codes))
        messages = [receipt.get("error", "")]
        problems = receipt.get("problems", [])
        if not isinstance(problems, list):
            failures.append(path.name + ": invalid original problem evidence")
        else:
            messages += [problem for problem in problems if isinstance(problem, str)]
        for message in messages:
            if integrity_failure_code(message):
                failures.append(path.name + ": " + message)
    return failures


def write_execution_failure(evidence, error, selection, controller, summary=None):
    path = evidence / "failure.json"
    failure = read_json(path) if path.is_file() else {}
    note_integrity_failure(failure, failure.get("error", ""))
    failure.update(status="failed", error=str(error), selection=selection,
                   purpose="diagnostic" if selection["mode"] == "focused" else "complete-campaign",
                   controller_exit_code=controller.poll() if controller else None)
    note_integrity_failure(failure, error)
    if summary is not None:
        summary["status"] = "failed"
        summary["qualification_eligible"] = False
        summary.setdefault("problems", []).append(str(error))
        note_integrity_failure(summary, error)
        write_json(evidence / "result.json", summary)
    write_json(path, failure)


def finish_execution_cleanup(evidence, tool, image, journal, nonce, known,
                             controller, epmd, job, selection, summary=None):
    try:
        if WINDOWS:
            owned_processes(tool, image, journal, nonce, known)
            close_job(job)
        remaining = cleanup(tool, image, journal, nonce, known)
        if not isinstance(remaining, list):
            raise ExecutionIntegrityError("cleanup-incomplete", "Execution process cleanup did not return complete process evidence")
        for process in (controller, epmd):
            if process:
                process.wait(timeout=5)
        if remaining:
            raise ExecutionIntegrityError("cleanup-incomplete", "Run-owned processes remain: " + str(evidence / "cleanup.json"))
        # Complete means close_job, process checks, waits and this persistence all
        # succeeded. Historical cleanup receipts without this field remain valid.
        write_json(evidence / "cleanup.json", {"owned_processes": list(known.values()),
                                               "remaining": [], "complete": True})
    except Exception as error:
        failure = ExecutionIntegrityError("cleanup-incomplete", str(error))
        try:
            write_json(evidence / "cleanup.json", {"owned_processes": list(known.values()),
                                                   "remaining": None, "complete": False,
                                                   "error": str(error)})
        except OSError:
            pass  # Original failure receipts still make a missing cleanup fail closed.
        write_execution_failure(evidence, failure, selection, controller, summary)
        raise failure from error


def timing_phase(args, phase, start=False):
    if not hasattr(args, "_attempt_timing"):
        return
    active = args._timing_active
    if start:
        active[phase] = time.monotonic()
    elif phase in active:
        args._attempt_timing["phases"][phase + "_wall_seconds"] = time.monotonic() - active.pop(phase)


def timed_attempt(function):
    @wraps(function)
    def measured(args):
        started = time.monotonic()
        args._attempt_timing = {"action": function.__name__, "phases": {}, "commands": []}
        args._timing_active = {}
        args._timing_evidence = None
        try:
            return function(args)
        finally:
            for phase in list(args._timing_active):
                timing_phase(args, phase)
            args._attempt_timing["total_wall_seconds"] = time.monotonic() - started
            if args._timing_evidence is not None:
                try:
                    write_json(args._timing_evidence / "timing.json", args._attempt_timing)
                except OSError as error:
                    print("Could not persist optional timing evidence: " + str(error), file=sys.stderr)
    return measured


def attempt(args, kind, identity=None):
    tag = hashlib.sha256(str(args.binary_root).encode()).hexdigest()[:12]
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    parent = args.source_root.parent / "tmp"
    parent.mkdir(parents=True, exist_ok=True)
    for _ in range(128):
        token = uuid.uuid4().hex
        name = "beam-full-" + (token[:8] if WINDOWS else tag + "-" + kind + "-" + stamp + "-" + token[:6])
        root = parent / name
        try:
            # CRT fopen paths in retained Windows fixtures still have MAX_PATH
            # limits. A compact name is private; exclusive creation owns it.
            root.mkdir(exist_ok=False)
            break
        except FileExistsError:
            continue
    else:
        raise RuntimeError("Could not allocate a fresh owned emulator attempt directory")
    metadata = {"name": root.name, "kind": kind, "created_utc": stamp,
                "build_tag": tag, "platform": platform.system(), "host": socket.gethostname(),
                "source_root": str(args.source_root), "binary_root": str(args.binary_root),
                "flavor": args.flavor, "build_type": args.build_type}
    if identity is not None:
        metadata.update(input_fingerprint=identity["fingerprint"], upstream_commit=identity["upstream_commit"])
    if hasattr(args, "_attempt_timing"):
        args._timing_evidence = root
        args._attempt_timing["attempt"] = metadata
    for name in ("home", "work", "logs"):
        (root / name).mkdir()
    write_json(root / "attempt.json", metadata)
    (args.full_root / ("latest-" + kind + ".txt")).write_text(str(root) + "\n")
    print("Emulator " + kind + " evidence: " + str(root), flush=True)
    return root


def environment(root, bindir):
    host = socket.gethostname()
    names = sorted({"localhost", host, host.split(".")[0], host.split(".")[0] + ".beam.test"})
    (root / "hosts").write_text("127.0.0.1 " + " ".join(names) + "\n")
    (root / "inetrc").write_text('{lookup,[file]}.\n{domain,"beam.test"}.\n{hosts_file,' + json.dumps(str(root / "hosts")) + '}.\n')
    common = {"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "HOME": str(root / "home"),
            "TMPDIR": str(root / "work"), "BINDIR": str(bindir),
            "ERL_INETRC": str(root / "inetrc"), "ERL_CRASH_DUMP": str(root / "erl_crash.dump"),
            "TZ": "Europe/Stockholm"}
    if WINDOWS:
        system = Path(os.environ["SystemRoot"])
        common.pop("TZ")
        common.update(PATH=str(system / "System32"), SystemRoot=str(system), WINDIR=str(system),
                      SYSTEMDRIVE=system.drive, COMSPEC=str(system / "System32/cmd.exe"),
                      PATHEXT=".COM;.EXE;.BAT;.CMD", TMP=str(root / "work"), TEMP=str(root / "work"),
                      USERPROFILE=str(root / "home"), HOMEDRIVE=root.drive,
                      HOMEPATH=str(root / "home")[len(root.drive):])
    return common


def boot(args, runtime, evidence, code, extra, log, timeout=3600, additional_paths=()):
    paths = [vm_path(p) for p in (runtime / "lib").glob("*/ebin")]
    paths += [vm_path(runtime / "support"), *map(vm_path, additional_paths)]
    command = [str(args.runtime_bin / program("beam")), "-S", "2:2", "-SDcpu", "1", "-SDio", "1", "-K", "false" if WINDOWS else "true", "--",
               "-root", vm_path(runtime), "-bindir", vm_path(args.runtime_bin), "-progname", "beam",
               "-boot", vm_path(runtime / "bin/start_clean"), "-path", *paths,
               "-noshell", "-noinput", "-eval", code, "-extra", *map(vm_path, extra)]
    env = environment(evidence, args.runtime_bin)
    job = None
    known = {}
    journal = evidence / (log.stem + "-processes.jsonl")
    record_file = evidence / (log.stem + "-process.json")
    nonce = uuid.uuid4().hex
    try:
        if WINDOWS:
            env.update(BEAM_TEST_RUN_NONCE=nonce, BEAM_TEST_PROCESS_JOURNAL=str(journal))
            job = start_job(args.process_tool, env, evidence)
            command = [str(args.process_tool), "run", str(record_file), *command]
        with open(log, "wb") as output:
            command_started = time.monotonic()
            try:
                result = subprocess.run(command, cwd=evidence / "work", env=env,
                                        stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
            finally:
                if hasattr(args, "_attempt_timing"):
                    args._attempt_timing["commands"].append({"log": log.name,
                        "supervised_wall_seconds": time.monotonic() - command_started})
    finally:
        if WINDOWS:
            if record_file.exists():
                record = read_json(record_file)
                known[record["pid"]] = record
            owned_processes(args.process_tool, runtime, journal, nonce, known)
            close_job(job)
            remaining = cleanup(args.process_tool, runtime, journal, nonce, known)
            write_json(evidence / (log.stem + "-cleanup.json"),
                       {"owned_processes": list(known.values()), "remaining": remaining})
            if remaining:
                raise RuntimeError("Preparation left owned processes: " + str(log))
    if result.returncode:
        raise RuntimeError("Candidate command failed (" + str(result.returncode) + "): " + str(log))


@timed_attempt
def prepare(args):
    identity = input_files(args)
    evidence = attempt(args, "prepare", identity)
    write_json(evidence / "input-identity.json", identity)
    runtime = args.full_root / "runtime"
    if runtime.exists():
        shutil.rmtree(runtime)
    shutil.copytree(args.source_root / "bootstrap", runtime)
    support = args.source_root / "tests/emulator/support"
    for relative in support_files(args):
        target = child(runtime, relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(child(support, relative), target)
    (runtime / "support").mkdir(exist_ok=True)
    code = '''try
 [Source,Root,Evidence,Flavor,BuildType] = init:get_plain_arguments(),
 Flavor = atom_to_list(erlang:system_info(emu_flavor)),
 Load = fun(Name) ->
   File=filename:join([Source,"tests","emulator",atom_to_list(Name)++".erl"]),
   case compile:file(File,[binary,debug_info,return_errors,{i,filename:join([Root,"lib","common_test","include"])}]) of
    {ok,Name,Bin} -> ok=file:write_file(filename:join([Root,"support",atom_to_list(Name)++".beam"]),Bin),
                     {module,Name}=code:load_binary(Name,File,Bin);
    Error -> erlang:error({full_harness_compile_failed,File,Error})
   end
 end,
 Load(beam_emulator_harness),
 {ok,_FrameworkRecords}=beam_emulator_harness:prepare_framework([Root,Evidence,Flavor,BuildType]),
 Load(beam_emulator_full_prepare),
 ok=beam_emulator_full_prepare:prepare([Root,filename:join([Source,"erts","emulator","test"]),Evidence,Flavor,BuildType]),
 Load(beam_emulator_events), Load(beam_emulator_focus), Load(beam_emulator_full_run), erlang:halt(0)
 catch C:R:S -> erlang:display({full_prepare_failed,C,R,S}),erlang:halt(1) end.'''
    boot(args, runtime, evidence, code,
         [args.source_root, runtime, evidence, args.flavor, args.build_type], evidence / "compile.log")
    corpus = read_json(evidence / "corpus-selection.json")
    # Keep expected coverage among the hashed outputs, like the compiled suites.
    shutil.copy2(evidence / "corpus-selection.json", runtime / "support/corpus-selection.json")
    variables, _compiler = compiler_variables(args)
    compiler_dir = runtime / "compiler_config"
    compiler_dir.mkdir()
    shutil.copy2(variables, compiler_dir / "variables")
    _scope_manifest, scope = scope_manifest(args.source_root)
    specs = runtime / "specs"
    if not specs.is_dir():
        raise RuntimeError("Full preparation did not stage specifications")
    (specs / "functional-scope.spec").write_text(render_scope_spec(scope))
    native = read_json(args.native_manifest)
    for item in native["artifacts"]:
        target = child(runtime, item["path"])
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(child(args.native_root, item["path"]), target)
    tool = runtime / "tools" / program("emulator_process")
    tool.parent.mkdir(exist_ok=True)
    shutil.copy2(args.process_tool, tool)
    # Upstream runs this VM-halting probe as a separate process.
    boot(args, runtime, evidence, "has_fpe_bug:start().", [], evidence / "float-probe.log",
         additional_paths=[runtime / "emulator_test/float_SUITE_data"])
    outputs = {str(p.relative_to(runtime)): sha(p) for p in sorted(runtime.rglob("*")) if p.is_file()}
    receipt = {"status": "prepared", "identity": identity, "outputs": outputs,
               "suites": corpus["suites"], "no_opt": corpus["no_opt"], "stripped_types": corpus["stripped_types"],
               "evidence": str(evidence)}
    write_json(runtime / "prepared-receipt.json", receipt)
    write_json(evidence / "prepared-receipt.json", receipt)
    catalog = write_suite_ctest(args, corpus)
    print("Full corpus prepared: " + str(len(outputs)) + " private files", flush=True)
    print("Individual suite diagnostics: ctest --test-dir " + shlex.quote(str(catalog.parent)) + " -N", flush=True)


def inspect(tool, pid):
    result = subprocess.run([str(tool), "inspect", str(pid)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                            text=True, encoding="utf-8")
    return json.loads(result.stdout) if result.returncode == 0 else None


def signal_owned(tool, record, number):
    return subprocess.run([str(tool), "signal", str(record["pid"]), str(record["birth_sec"]),
                           str(record["birth_usec"]), record["path"], str(int(number))],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def start_job(tool, env, evidence):
    if not WINDOWS:
        return None
    name = "Local\\beam-full-" + uuid.uuid4().hex
    with open(evidence / "job.log", "ab") as log:
        server = subprocess.Popen([str(tool), "job-server", name], env=env,
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
    ready = server.stdout.readline()
    if not ready or server.poll() is not None:
        server.stdin.close()
        server.wait(timeout=5)
        raise RuntimeError("Could not create private Windows process job")
    state = json.loads(ready)
    if state.get("job") != name or state.get("status") != "ready":
        server.stdin.close()
        server.wait(timeout=5)
        raise RuntimeError("Unexpected Windows process job receipt: " + repr(state))
    env["BEAM_TEST_JOB_NAME"] = name
    return server


def close_job(server):
    if server is not None:
        server.stdin.close()
        code = server.wait(timeout=10)
        server.stdout.close()
        if code:
            raise RuntimeError("Windows process job cleanup failed: " + str(code))


class OwnedWindowsProcess:
    """Observe a tool-spawned process without opening a PID-reuse kill path."""
    def __init__(self, tool, record):
        self.tool, self.record, self.pid = tool, record, record["pid"]

    def poll(self):
        current = inspect(self.tool, self.pid)
        return None if same_process(current, self.record) else 0

    def terminate(self):
        signal_owned(self.tool, self.record, 15)

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired("owned Windows process", timeout)
            time.sleep(0.05)
        return 0


def same_process(current, record):
    return bool(current) and all(current.get(key) == record.get(key)
                                 for key in ("birth_sec", "birth_usec", "uid", "path"))


def start_epmd(image, tool, env, log):
    for _ in range(20):
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        env["ERL_EPMD_PORT"] = str(port)
        env["ERL_EPMD_ADDRESS"] = "127.0.0.1"
        executable = image / "erts-17.0/bin" / program("epmd")
        if WINDOWS:
            record_file = image.parent / ("epmd-process-" + str(port) + ".json")
            subprocess.run([str(tool), "spawn", str(record_file), str(executable), "-port", str(port), "-d"],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=10)
            process = OwnedWindowsProcess(tool, read_json(record_file))
        else:
            process = subprocess.Popen([str(executable), "-port", str(port), "-d"],
                                       env=env, stdout=log, stderr=subprocess.STDOUT)
        record = None
        for _ in range(100):
            if process.poll() is not None:
                break
            record = inspect(tool, process.pid)
            owns = subprocess.run([str(tool), "owns-listener", str(process.pid), str(port)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
            if owns and record and Path(record["path"]).resolve() == executable.resolve():
                return process, record, port
            time.sleep(0.05)
        if process.poll() is None:
            # An unreaped Popen child cannot have had its PID recycled.
            process.terminate()
        process.wait(timeout=5)
    raise RuntimeError("Could not acquire an owned private EPMD endpoint")


def owned_processes(tool, image, journal, nonce, known):
    if journal.exists():
        for line in journal.read_text(encoding="utf-8").splitlines():
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if record.get("nonce") == nonce:
                known[record["pid"]] = record
    if WINDOWS:
        records = json.loads(subprocess.check_output([str(tool), "list"], text=True, encoding="utf-8"))
        inventory = [(record["pid"], record["ppid"], record) for record in records]
    else:
        inventory = [(int(pid), int(parent), None) for pid, parent in
                     (line.split() for line in subprocess.check_output(["/bin/ps", "-axo", "pid=,ppid="], text=True).splitlines())]
    for pid, parent, snapshot in inventory:
        if pid == os.getpid():
            continue
        # Detached native helpers are identifiable by their private executable;
        # external child commands require a verified owned parent as evidence.
        record = snapshot or inspect(tool, pid)
        parent_record = inspect(tool, parent) if parent in known else None
        owned_parent = same_process(parent_record, known[parent]) if parent in known else False
        private_path = record and Path(record["path"]).resolve().is_relative_to(image.resolve())
        if record and (private_path or owned_parent):
            known[pid] = record
    return known


def cleanup(tool, image, journal, nonce, known):
    owned_processes(tool, image, journal, nonce, known)
    for number in ((15, 9) if WINDOWS else (signal.SIGTERM, signal.SIGKILL)):
        owned_processes(tool, image, journal, nonce, known)
        for record in reversed(list(known.values())):
            signal_owned(tool, record, number)
        time.sleep(0.3)
    remaining = []
    for record in known.values():
        current = inspect(tool, record["pid"])
        if same_process(current, record):
            remaining.append(current)
    return remaining


def condition(rule, facts, event):
    facts = dict(facts, **event.get("guard_facts", {}))
    item = rule["condition"]
    kind = item["kind"]
    if kind == "always":
        return True
    value = facts.get(item.get("field", ""))
    if kind in ("runtime_equals", "runtime_not_equals", "runtime_one_of") and item.get("field") not in facts:
        return False
    if kind == "runtime_equals":
        return value == item["value"]
    if kind == "runtime_not_equals":
        return value != item["value"]
    if kind == "runtime_one_of":
        return value in item["values"]
    if kind == "tool_absent":
        return item["tool"] in facts.get("tools", {}) and facts["tools"][item["tool"]] is None
    if kind == "modules_absent":
        return "property_tools" in facts and not any(module in facts["property_tools"] for module in item["modules"])
    if kind == "old_release_absent":
        return "optional_releases" in facts and not facts["optional_releases"]
    if kind == "all":
        return all(condition({"condition": part}, facts, event) for part in item["all"])
    if kind == "memory_below":
        if event.get("resource_memory_error"):
            return False
        memory = dict(event.get("resource_memory", facts.get("memory", [])))
        available = memory.get("available_memory", memory.get("total_memory", memory.get("system_total_memory", 0)))
        threshold = item["minimum_gb"]
        if facts.get("build_type") in ("asan", "valgrind") and "sanitizer_minimum_gb" in item:
            threshold = item["sanitizer_minimum_gb"]
        elif item.get("debug_scale") and facts.get("debug_compiled") in (True, "true"):
            threshold = item["minimum_gb"] * item["debug_scale"]
        limit = threshold * (1024 ** 3)
        return bool(available) and (available <= limit if item.get("inclusive") else available < limit)
    if kind == "memory_components_below":
        if event.get("resource_memory_error"):
            return False
        memory = dict(event.get("resource_memory", facts.get("memory", [])))
        if "free_memory" not in memory:
            return False
        available = sum(memory.get(key, 0) for key in ("free_memory", "cached_memory", "buffered_memory"))
        return available < item["threshold_gb"] * (1024 ** 3)
    if kind == "paths_absent":
        return facts.get("platform") == item["platform"] and all(not Path(path).exists() for path in item["paths"])
    if kind == "platform_equals":
        return facts.get("platform") == item["value"]
    return False


def requested_selection(suite=None, group=None, cases=None):
    return {"mode": "focused" if suite else "full", "suite": suite,
            "group": group or "", "cases": cases.split(",") if cases else []}


def invocation_selection(invocation):
    if "selection" in invocation:
        return invocation["selection"]
    # Old receipts have six -extra arguments and no structured selection.
    extra = invocation["command"][invocation["command"].index("-extra") + 1:]
    if len(extra) != 6:
        raise RuntimeError("Unrecognized legacy emulator invocation")
    selection = requested_selection(None if extra[-2] == "full" else extra[-2], cases=extra[-1])
    selection["legacy"] = True
    return selection


def focus_evidence(image, logdir):
    inventory = logdir / "focus-inventory.json"
    metadata = read_json(inventory)
    spec = child(image, metadata["spec"])
    return {"inventory": metadata, "inventory_sha256": sha(inventory), "spec_sha256": sha(spec)}


def focused_coverage(events, selection, focus, empty_suites=()):
    problems = []
    suite = selection["suite"]
    endpoints = suite in ("a_SUITE", "z_SUITE")
    expected_suites = ["a_SUITE", "z_SUITE"] if endpoints and not selection.get("legacy") else [suite]
    if focus:
        recorded = focus["inventory"].get("execution_suites", [suite])
        if recorded != expected_suites:
            problems.append({"expected_execution_suites": expected_suites, "recorded_execution_suites": recorded})
    seen = {e["suite"] for e in events if e.get("suite") and e["suite"] != "ct_framework"}
    if seen | set(empty_suites) != set(expected_suites):
        if len(expected_suites) == 1:
            problems.append({"expected_suite": suite, "observed_suites": sorted(seen)})
        else:
            problems.append({"expected_suites": expected_suites, "observed_suites": sorted(seen)})
    if len(expected_suites) > 1:
        order = []
        for event in events:
            if event.get("event") in ("tc_start", "tc_done", "tc_user_skip", "tc_auto_skip") and not event.get("callback"):
                name = event.get("suite")
                if name in expected_suites and (not order or order[-1] != name):
                    order.append(name)
        if order != expected_suites:
            problems.append({"lifecycle_suite_order": order, "expected": expected_suites})
    if not focus:
        # Legacy diagnostics lack a bound focused specification/inventory.
        return problems
    inventory = focus["inventory"]
    if inventory["selection"] != {key: selection[key] for key in ("mode", "suite", "group", "cases")}:
        problems.append("Focused inventory differs from the requested selection")
    if inventory["coverage_mode"] not in ("exact_cases", "ct_planned"):
        problems.append("Unrecognized focused coverage mode")
    if inventory["coverage_mode"] == "exact_cases":
        expected = {(item["module"], item["case"]) for item in inventory["expected_cases"]}
        observed = {(e["suite"], e["case"]) for e in events
                    if e.get("event") in ("tc_done", "tc_user_skip", "tc_auto_skip")
                    and not e.get("callback") and e.get("suite") != "ct_framework"}
        if expected != observed:
            problems.append({"missing_cases": sorted(expected - observed),
                             "unexpected_cases": sorted(observed - expected)})
        skipped_groups = {e.get("group") for e in events if e.get("suite") == suite
                          and e.get("callback") == "init_per_group" and e.get("skipkind") == "user"}
        paths = inventory.get("expected_group_paths", [[group] for group in inventory["expected_groups"]])
        for path in paths:
            group = path[-1]
            if any(ancestor in skipped_groups for ancestor in path):
                continue
            callbacks = [e for e in events if e.get("suite") == suite and e.get("group") == group]
            initialized = any(e.get("callback") == "init_per_group" and e.get("outcome") == "passed"
                              for e in callbacks)
            ended = any(e.get("callback") == "end_per_group" and e.get("outcome") == "passed"
                        for e in callbacks)
            if not (initialized and ended):
                problems.append({"incomplete_group_callbacks": group})
    return problems


def suite_outcomes(events, execution_suites, accepted, scope_exclusions, empty_suites=()):
    """Classify each target independently; companion passes cannot mask no work."""
    audited = [record["event"] for record in accepted + scope_exclusions]
    outcomes = {}
    mapping = {"passed": "passed", "failed": "failed", "skipped": "user_skipped", "auto_skipped": "auto_skipped"}
    for suite in execution_suites:
        leaves = [event for event in events
                  if event.get("suite") == suite and not event.get("callback")
                  and event.get("event") in ("tc_done", "tc_user_skip", "tc_auto_skip")]
        counts = dict(passed=0, failed=0, user_skipped=0, auto_skipped=0)
        valid = bool(leaves) or suite in empty_suites
        if suite in empty_suites and leaves:
            valid = False
        for event in leaves:
            outcome = event.get("outcome")
            if outcome not in mapping:
                valid = False
                continue
            counts[mapping[outcome]] += 1
            if outcome == "skipped" and event not in audited:
                valid = False
            if outcome in ("failed", "auto_skipped"):
                valid = False
        callbacks = [event for event in events if event.get("suite") == suite and event.get("callback")]
        if any(event.get("outcome") in ("failed", "framework_error", "auto_skipped")
               or event.get("skipkind") == "auto"
               or (event.get("skipkind") == "user" and event not in audited) for event in callbacks):
            valid = False
        status = "failed" if not valid else ("passed" if counts["passed"] else "not_applicable")
        outcomes[suite] = {"status": status, "result": counts}
    return outcomes


def audited_empty_suites(args, policy, facts, selection, focus, events, execution_identity=None):
    """An observed empty inventory qualifies only under a pinned native guard."""
    observed = facts.get("suite_all_empty", [])
    requested = (focus["inventory"].get("empty_suites", []) if focus else []) if selection["mode"] == "focused" else observed
    if not isinstance(observed, list) or not isinstance(requested, list):
        raise RuntimeError("Invalid empty-suite observations")
    if len(set(requested)) != len(requested):
        raise RuntimeError("Duplicate empty-suite observations")
    accepted = {}
    for suite in requested:
        if suite not in observed:
            raise RuntimeError("Empty-suite inventory disagrees with candidate runtime facts: " + suite)
        if selection["mode"] == "focused":
            inventory = focus["inventory"]
            if selection.get("group") or selection.get("cases") or selection["suite"] != suite:
                raise RuntimeError("An empty-suite guard cannot accept a partial selection")
            if inventory.get("execution_suites") != [suite] or inventory.get("expected_cases") or inventory.get("expected_groups"):
                raise RuntimeError("Empty-suite inventory contains executable targets")
        if any(event.get("suite") == suite for event in events):
            raise RuntimeError("Empty-suite guard disagrees with observed testcases: " + suite)
        matches = [rule for rule in policy.get("empty_suite_rules", [])
                   if rule.get("suite") == suite and rule.get("inventory_function") == "all"
                   and rule.get("inventory_result") == [] and condition(rule, facts, {})]
        if len(matches) != 1:
            raise RuntimeError("Empty suite lacks one exact audited upstream guard: " + suite)
        accepted[suite] = {"rule": matches[0]["id"], "source_identity": guard_identity(args.source_root, matches[0], execution_identity)}
    return accepted


def classify(args, image, logdir, corpus, selection, auditdir=None, focus=None, execution_identity=None):
    auditdir = auditdir or logdir
    auditdir.mkdir(parents=True, exist_ok=True)
    result = read_json(logdir / "ct-result.json")
    events = [json.loads(line) for line in (logdir / "events.jsonl").read_text().splitlines()]
    facts = read_json(logdir / "runtime-facts.json")
    captured_environment = logdir.parent / "execution-environment-facts.json"
    if captured_environment.exists():
        facts.update(read_json(captured_environment))
    else:
        # Older executions already captured the candidate's perf probe and
        # architecture. Never substitute the auditor's current tool inventory.
        facts["platform"] = "Darwin" if "darwin" in facts.get("architecture", "") else "unknown"
        facts["optional_releases"] = {}
        facts["tools"] = {"perf": None if facts.get("perf") in (False, "false") else facts.get("perf")}
    starts = [e for e in events if e.get("event") == "start_info"]
    ends = [e for e in events if e.get("event") == "test_done"]
    problems = []
    expected_checks = "true" if args.build_type == "debug" else "false"
    expected_runtime = {"flavor": args.flavor, "build_type": args.build_type,
                        "debug_compiled": expected_checks, "lock_checking": expected_checks}
    if any(facts.get(key) != value for key, value in expected_runtime.items()):
        problems.append({"unexpected_runtime": facts, "expected_runtime": expected_runtime})
    if len(starts) != 1 or len(ends) != 1:
        problems.append("Missing or duplicated Common Test run boundaries")
    for boundary in ("handler_started", "handler_stopped"):
        if len([e for e in events if e.get("event") == boundary]) != 1:
            problems.append("Missing or duplicated " + boundary)
    if [e.get("seq") for e in events] != list(range(1, len(events) + 1)):
        problems.append("Event journal sequence is incomplete")
    if any(e.get("malformed") or e.get("outcome") == "unrecognized" for e in events):
        problems.append("Malformed or unrecognized critical Common Test event")
    stats = [e for e in events if e.get("event") == "test_stats"]
    if stats and any(stats[-1].get(k) != result[k] for k in ("passed", "failed", "user_skipped", "auto_skipped")):
        problems.append("Common Test statistics disagree with final result")
    leaf_counts = {"passed": 0, "failed": 0, "skipped": 0, "auto_skipped": 0}
    for event in events:
        if event.get("event") in ("tc_done", "tc_user_skip", "tc_auto_skip") and not event.get("callback"):
            outcome = event.get("outcome")
            if outcome in leaf_counts:
                leaf_counts[outcome] += 1
    expected_counts = {"passed": result["passed"], "failed": result["failed"],
                       "skipped": result["user_skipped"], "auto_skipped": result["auto_skipped"]}
    if leaf_counts != expected_counts:
        problems.append({"leaf_event_counts": leaf_counts, "expected": expected_counts})
    policy = read_json(args.source_root / "tests/emulator/emulator_skip_policy.json")
    shutil.copy2(args.source_root / "tests/emulator/emulator_skip_policy.json", auditdir / "applied-skip-policy.json")
    if policy["upstream_commit"] != (args.source_root / "UPSTREAM_COMMIT").read_text().strip():
        raise RuntimeError("Skip policy upstream identity changed")
    historical = bool(selection.get("legacy"))
    historical_reasons = []
    current_fingerprint = None
    if execution_identity is not None:
        original_policy = execution_policy_identity(args.source_root, logdir.parent, execution_identity)
        if not re.fullmatch(r"[0-9a-f]{64}", str(execution_identity.get("fingerprint", ""))):
            historical_reasons.append("Execution input fingerprint is missing or invalid")
        if sha(args.source_root / "tests/emulator/emulator_skip_policy.json") != original_policy["sha256"]:
            historical_reasons.append("Applied skip policy differs from the original execution policy")
        try:
            current_fingerprint = input_files(args)["fingerprint"]
        except Exception as error:
            historical_reasons.append("Current input identity could not be established: " + str(error))
        if current_fingerprint != execution_identity.get("fingerprint"):
            historical_reasons.append("Current input fingerprint differs from the original execution")
        historical = historical or bool(historical_reasons)
    empty_suites = audited_empty_suites(args, policy, facts, selection, focus, events, execution_identity)
    if not stats:
        # Pinned CT emits no test_stats for a sole all/0 == [] suite. Require
        # its exact native guard and the measured zero-case planning contract.
        empty_run = (selection["mode"] == "focused" and set(empty_suites) == {selection["suite"]}
                     and len(starts) == 1 and starts[0].get("tests") == 1
                     and starts[0].get("suites") == 0 and starts[0].get("cases") == 0
                     and all(value == 0 for value in result.values())
                     and not any(event.get("suite") for event in events))
        if not empty_run:
            problems.append("Common Test statistics disagree with final result")
    rules_by_id = {rule["id"]: rule for rule in policy["rules"]}
    accepted, rejected, scope_exclusions = [], [], []
    matched_scope = set()
    for event in events:
        if event.get("outcome") in ("failed", "framework_error", "auto_skipped") or event.get("skipkind") == "auto":
            problems.append(event)
        if event.get("skipkind") != "user":
            continue
        matches = []
        for rule in policy["rules"]:
            if any(rule.get(field) not in (None, "*", event.get(field)) for field in ("suite", "case", "group", "callback")):
                continue
            if not re.fullmatch(rule["reason_regex"], event.get("reasontext", ""), flags=re.DOTALL):
                continue
            guard_identity(args.source_root, rule, execution_identity)
            origin = rule.get("guard_callback")
            if origin and not any(e.get("suite") == event.get("suite") and e.get("callback") == origin
                                  and e.get("skipkind") == "user"
                                  and re.fullmatch(rule["reason_regex"], e.get("reasontext", ""), flags=re.DOTALL)
                                  and (rule.get("group") is None or e.get("group") == rule["group"])
                                  for e in events):
                continue
            if condition(rule, facts, event):
                matches.append(rule["id"])
        scope_ids = [rule_id for rule_id in matches if rules_by_id[rule_id].get("class") == "scope"]
        runtime_ids = [rule_id for rule_id in matches if rules_by_id[rule_id].get("class") != "scope"]
        matched_scope.update(scope_ids)
        record = {"event": event, "rules": matches,
                  "source_identities": [guard_identity(args.source_root, rules_by_id[rule_id], execution_identity) for rule_id in matches]}
        if scope_ids and not runtime_ids:
            scope_exclusions.append(record)
        elif matches:
            accepted.append(record)
        else:
            rejected.append(record)
    scope_file, scope = scope_manifest(args.source_root)
    check_scope_policy(args.source_root, scope)
    if selection["mode"] == "full":
        expected = set(prepared_suite_names(corpus))
        seen = {e["suite"] for e in events if e.get("suite") and e["suite"] != "ct_framework"}
        if seen | set(empty_suites) != expected:
            problems.append({"missing_suites": sorted(expected - seen - set(empty_suites)), "unexpected_suites": sorted(seen - expected)})
        ordered = [e["suite"] for e in events if e.get("event") == "tc_start"
                   and not e.get("callback") and e.get("suite") != "ct_framework"]
        if not ordered or ordered[0] != "a_SUITE" or ordered[-1] != "z_SUITE":
            problems.append("Full run did not preserve a_SUITE / z_SUITE boundaries")
        if starts and starts[0].get("suites") != len(expected) - len(empty_suites):
            problems.append("Common Test planned suite count disagrees with the pinned corpus")
        missing_scope = []
        for item in scope["exclusions"]:
            required = item["required_flavor"] == facts.get("flavor") if "required_flavor" in item else item.get("required", False)
            if required and item["id"] not in matched_scope:
                missing_scope.append(item["id"])
        if missing_scope:
            problems.append({"missing_scope_exclusions": missing_scope})
        unknown_scope = sorted(matched_scope - {item["id"] for item in scope["exclusions"]})
        if unknown_scope:
            problems.append({"unknown_scope_rules": unknown_scope})
    else:
        problems.extend(focused_coverage(events, selection, focus, empty_suites))
    if starts and isinstance(starts[0].get("cases"), int) and starts[0]["cases"] != sum(result.values()):
        problems.append("Planned testcase count disagrees with terminal totals")
    suite_results = {}
    if selection["mode"] == "focused":
        execution_suites = focus["inventory"].get("execution_suites", [selection["suite"]]) if focus else [selection["suite"]]
        suite_results = suite_outcomes(events, execution_suites, accepted, scope_exclusions, empty_suites)
        if any(record["status"] == "failed" for record in suite_results.values()):
            problems.append({"incomplete_or_nonpassing_suites": suite_results})
        if suite_results[selection["suite"]]["status"] == "not_applicable":
            planned = starts[0].get("cases") if len(starts) == 1 else None
            exact = focus and focus["inventory"]["coverage_mode"] == "exact_cases"
            if type(planned) is not int and not exact:
                problems.append("Not-applicable diagnostic lacks complete planned or exact case coverage")
    if result["failed"] or result["auto_skipped"] or (selection["mode"] == "full" and result["passed"] <= 0) or rejected:
        problems.append("Nonpassing totals or unaudited skips")
    status = "failed" if problems else (suite_results[selection["suite"]]["status"] if suite_results else "passed")
    summary = {"status": status, "selection": selection,
               "purpose": "diagnostic" if selection["mode"] == "focused" else "complete-campaign",
               "historical": historical,
               "historical_reclassification": historical and execution_identity is not None,
               "qualification_eligible": execution_identity is not None and not historical and status in ("passed", "not_applicable"),
               "result": result, "accepted_skips": accepted, "scope_exclusions": scope_exclusions,
               "rejected_skips": rejected, "problems": problems, "runtime": facts,
               "scope_manifest_sha256": sha(scope_file),
               "applied_skip_policy_sha256": sha(auditdir / "applied-skip-policy.json"),
               "execution_skip_policy_sha256": sha(logdir.parent / "execution-skip-policy.json")}
    if execution_identity is not None:
        summary["execution_input_fingerprint"] = execution_identity.get("fingerprint")
        summary["current_input_fingerprint"] = current_fingerprint
        summary["historical_reasons"] = historical_reasons
    if empty_suites:
        summary["audited_empty_suites"] = empty_suites
    if suite_results:
        summary["suite_results"] = suite_results
    if focus:
        summary["focus"] = focus
    write_json(auditdir / "audit.json", summary)
    return summary


def stage_candidate(args, image):
    """Stage the exact candidate/helper used by both full execution and smoke boots."""
    actual_vm = image / "libexec" / candidate_program()
    actual_vm.parent.mkdir(exist_ok=True)
    shutil.copy2(args.runtime_bin / program("beam"), actual_vm)
    bindir = image / "erts-17.0/bin"
    if WINDOWS:
        for name in ("beam", "beam.smp", "beam." + args.flavor):
            shutil.copy2(args.runtime_bin / program(name), bindir / program(name))
            if args.build_type == "debug":
                shutil.copy2(args.runtime_bin / (name + ".pdb"), bindir / (name + ".pdb"))
        if args.build_type == "debug":
            shutil.copy2(args.runtime_bin / "beam.pdb", actual_vm.with_suffix(".pdb"))
        return actual_vm
    shutil.copy2(args.runtime_bin / "erl_child_setup", bindir / "erl_child_setup")
    alias = bindir / "erl_child_setup.debug"
    alias.unlink(missing_ok=True)
    if args.build_type == "debug":
        alias.symlink_to("erl_child_setup")
    if helper_aliases(bindir, args.build_type) != helper_aliases(args.runtime_bin, args.build_type):
        raise RuntimeError("Staged helper aliases differ from the candidate")
    tool = image / "tools/emulator_process"
    for name in ("beam.smp", "beam." + args.flavor):
        shutil.copy2(tool, bindir / name)
    return actual_vm


def verify_execution_image(args, image, identity):
    if identity["build_type"] != args.build_type or identity["flavor"] != args.flavor:
        raise RuntimeError("Execution build type or flavor differs from its input identity")
    actual_vm = image / "libexec" / candidate_program()
    if WINDOWS:
        artifacts = {}
        pairs = [(actual_vm, args.runtime_bin / "beam.exe")]
        for name in ("beam", "beam.smp", "beam." + args.flavor):
            pairs.append((image / "erts-17.0/bin" / program(name), args.runtime_bin / program(name)))
            if args.build_type == "debug":
                pairs.append((image / "erts-17.0/bin" / (name + ".pdb"), args.runtime_bin / (name + ".pdb")))
        if args.build_type == "debug":
            pairs.append((actual_vm.with_suffix(".pdb"), args.runtime_bin / "beam.pdb"))
        for actual, source in pairs:
            if sha(actual) != identity["inputs"][str(source)]:
                raise RuntimeError("Executed candidate artifact identity changed: " + str(actual))
            artifacts[str(actual.relative_to(image))] = sha(actual)
        if identity.get("child_helper") != "windows-no-erl_child_setup":
            raise RuntimeError("Windows candidate unexpectedly requires a Unix child helper")
        return {"candidate_sha256": sha(actual_vm), "child_setup_sha256": "windows-no-erl_child_setup",
                "build_type": args.build_type, "helper_aliases": {}, "windows_artifacts": artifacts}
    helper = image / "erts-17.0/bin/erl_child_setup"
    for actual, source in ((actual_vm, args.runtime_bin / "beam"),
                           (helper, args.runtime_bin / "erl_child_setup")):
        if sha(actual) != identity["inputs"][str(source)]:
            raise RuntimeError("Executed candidate artifact identity changed: " + str(actual))
    aliases = helper_aliases(helper.parent, args.build_type)
    if aliases != identity["helper_aliases"]:
        raise RuntimeError("Executed helper aliases differ from the input identity")
    return {"candidate_sha256": sha(actual_vm), "child_setup_sha256": sha(helper),
            "build_type": args.build_type, "helper_aliases": aliases}


def configure_launcher(image, actual_vm, env, nonce, journal, port):
    launcher = image / "bin" / program("erl")
    if WINDOWS:
        if not launcher.is_file():
            raise RuntimeError("Missing prepared private Windows launcher: " + str(launcher))
        configuration = ("[erlang]\nBindir=" + (image / "erts-17.0/bin").as_posix()
                         + "\nRootdir=" + image.as_posix() + "\nProgname=" + launcher.as_posix() + "\n")
        for directory in (image / "bin", image / "erts-17.0/bin"):
            (directory / "erl.ini").write_text(configuration, encoding="utf-8")
        shutil.copy2(launcher, image / "erts-17.0/bin/erl.exe")
        return launcher
    script = "#!/bin/sh\n"
    for name, value in {"ROOTDIR": str(image), "BINDIR": str(image / "erts-17.0/bin"),
                        "EMU": "beam", "PROGNAME": str(launcher),
                        "BEAM_TEST_ACTUAL_VM": str(actual_vm), "BEAM_TEST_RUN_NONCE": nonce,
                        "BEAM_TEST_PROCESS_JOURNAL": str(journal)}.items():
        script += name + "=" + shlex.quote(value) + "\nexport " + name + "\n"
    script += 'ERL_EPMD_PORT="${ERL_EPMD_PORT:-' + str(port) + '}"\nexport ERL_EPMD_PORT\nexec "$BINDIR/erlexec" "$@"\n'
    launcher.write_text(script)
    launcher.chmod(0o755)
    return launcher


def capture_environment(evidence, env):
    facts = {"platform": platform.system(), "optional_releases": {},
             "tools": {name: shutil.which(name, path=env["PATH"]) for name in ("perf", "taskset", "lttng")}}
    if WINDOWS:
        result = subprocess.run([str(Path(env["SystemRoot"]) / "System32/tzutil.exe"), "/g"],
                                env=env, check=True, capture_output=True, text=True, timeout=10)
        zone = result.stdout.strip()
        if not zone:
            raise RuntimeError("Native Windows timezone discovery returned no timezone")
        facts.update(timezone=zone, host_timezone_probe="native_win32_tzutil", host_timezone_probe_succeeded="true")
    else:
        zone = subprocess.run(["/bin/date", "+%Z"], env=env, check=True,
                              capture_output=True, text=True).stdout.strip()
        if not zone.startswith(("ME", "CE")):
            raise RuntimeError("Europe/Stockholm did not produce a Central European abbreviation: " + zone)
        facts.update(timezone=env["TZ"], timezone_abbreviation=zone)
    write_json(evidence / "execution-environment-facts.json", facts)


@timed_attempt
def run(args):
    timing_phase(args, "setup", start=True)
    selection = requested_selection(args.suite, args.group, args.cases)
    rebuild = "cmake --build " + shlex.quote(str(args.binary_root)) + " --target beam_emulator_full_test_support"
    runtime = args.full_root / "runtime"
    if not (runtime / "prepared-receipt.json").is_file():
        raise RuntimeError("Full emulator support is not prepared; run: " + rebuild)
    try:
        identity = input_files(args)
    except FileNotFoundError as error:
        raise RuntimeError("Preparation input is missing; run: " + rebuild) from error
    receipt = read_json(runtime / "prepared-receipt.json")
    if receipt["identity"]["fingerprint"] != identity["fingerprint"]:
        raise RuntimeError("Full emulator preparation is stale; run: " + rebuild)
    for relative, expected in receipt["outputs"].items():
        output = child(runtime, relative)
        if not output.is_file() or sha(output) != expected:
            raise RuntimeError("Prepared artifact changed: " + relative + "; run: " + rebuild)
    corpus = read_json(runtime / "support/corpus-selection.json")
    if any(receipt[key] != corpus[key] for key in ("suites", "no_opt", "stripped_types")):
        raise RuntimeError("Prepared receipt coverage differs from the compiled corpus selection")
    suites = prepared_suite_names(corpus)
    if args.suite and args.suite not in suites:
        raise RuntimeError("Unknown prepared emulator suite: " + args.suite)
    evidence = attempt(args, "focused-run" if args.suite else "run", identity)
    image = evidence / "image"
    shutil.copy2(args.source_root / "tests/emulator/emulator_skip_policy.json", evidence / "execution-skip-policy.json")
    shutil.copy2(args.source_root / "tests/emulator/emulator_scope.json", evidence / "execution-scope.json")
    shutil.copytree(runtime, image)
    tool = image / "tools" / program("emulator_process")
    env = environment(evidence, image / "erts-17.0/bin")
    nonce = uuid.uuid4().hex
    journal = evidence / "processes.jsonl"
    actual_vm = stage_candidate(args, image)
    artifacts = verify_execution_image(args, image, identity)
    env.update(BEAM_TEST_ACTUAL_VM=str(actual_vm), BEAM_TEST_RUN_NONCE=nonce,
               BEAM_TEST_PROCESS_JOURNAL=str(journal))
    env["PATH"] = os.pathsep.join((str(image / "bin"), str(image / "erts-17.0/bin"), env["PATH"]))
    known = {}
    epmd = controller = job = None
    summary = None
    try:
        job = start_job(tool, env, evidence)
        with open(evidence / "epmd.log", "wb") as output:
            epmd, epmd_record, port = start_epmd(image, tool, env, output)
        known[epmd.pid] = epmd_record
        launcher = configure_launcher(image, actual_vm, env, nonce, journal, port)
        env.update(ROOTDIR=str(image), EMU="beam", PROGNAME=str(launcher))
        capture_environment(evidence, env)
        paths = [vm_path(image / "compiler_config")] + [vm_path(p) for p in (image / "lib").glob("*/ebin")] + [vm_path(image / "support"), vm_path(image / "emulator_test")]
        code = 'try ok=beam_emulator_full_run:run(init:get_plain_arguments()),erlang:halt(0) catch C:R:S -> erlang:display({full_run_failed,C,R,S}),erlang:halt(1) end.'
        command = [str(launcher), "+K", "false" if WINDOWS else "true", "-sname", "beamfull_" + nonce[:12], "-boot", vm_path(image / "bin/start_clean"),
                   "-pa", *paths, "-noshell", "-noinput", "-eval", code, "-extra",
                   vm_path(image), args.flavor, args.build_type, vm_path(evidence / "logs"), args.suite or "full", args.cases or ""]
        if args.suite:
            command.append(args.group or "")
        invocation = {"command": command, "environment": env, "identity": identity,
                      "epmd": epmd_record, "epmd_port": port, "nonce": nonce,
                      "artifacts": artifacts, "selection": selection}
        write_json(evidence / "invocation.json", invocation)
        timing_phase(args, "setup")
        timing_phase(args, "controller_execution", start=True)
        with open(evidence / "vm.log", "wb") as output:
            controller = subprocess.Popen(command, cwd=evidence / "work", env=env, stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + args.timeout
            last_scan = 0.0
            event_offset = 0
            event_pending = ""
            last_suite = None
            while controller.poll() is None:
                if time.monotonic() >= deadline:
                    raise RuntimeError("Full emulator execution timed out")
                if time.monotonic() - last_scan > 10:
                    owned_processes(tool, image, journal, nonce, known)
                    last_scan = time.monotonic()
                    event_path = evidence / "logs/events.jsonl"
                    if event_path.exists():
                        with open(event_path) as event_input:
                            event_input.seek(event_offset)
                            event_pending += event_input.read()
                            event_offset = event_input.tell()
                        lines = event_pending.split("\n")
                        event_pending = lines.pop()
                        for line in lines:
                            event = json.loads(line)
                            if event.get("event") == "tc_start" and event.get("suite") != "ct_framework" and event.get("suite") != last_suite:
                                last_suite = event.get("suite")
                                print("Emulator suite: " + str(last_suite), flush=True)
                time.sleep(0.5)
        timing_phase(args, "controller_execution")
        verify_execution_image(args, image, identity)
        focus = None
        if args.suite and (evidence / "logs/focus-inventory.json").exists():
            focus = focus_evidence(image, evidence / "logs")
            invocation["focus"] = focus
            write_json(evidence / "invocation.json", invocation)
        if not (evidence / "logs/ct-result.json").exists():
            raise RuntimeError("Candidate rejected the selection or failed before Common Test; see " + str(evidence / "vm.log"))
        if args.suite and focus is None:
            raise RuntimeError("Focused run did not record its selection inventory")
        summary = classify(args, image, evidence / "logs", corpus, selection, focus=focus,
                           execution_identity=identity)
        summary["controller_exit_code"] = controller.returncode
        if controller.returncode:
            summary["status"] = "failed"
            summary["qualification_eligible"] = False
        try:
            validate_execution_inputs(args, identity)
        except RuntimeError as error:
            summary["status"] = "failed"
            summary["problems"].append(str(error))
            note_integrity_failure(summary, error)
            write_json(evidence / "logs/audit.json", summary)
            write_json(evidence / "result.json", summary)
            raise
        write_json(evidence / "result.json", summary)
    except Exception as error:
        write_execution_failure(evidence, error, selection, controller)
        raise
    finally:
        timing_phase(args, "setup")
        timing_phase(args, "controller_execution")
        timing_phase(args, "cleanup", start=True)
        try:
            finish_execution_cleanup(evidence, tool, image, journal, nonce, known,
                                     controller, epmd, job, selection, summary)
        finally:
            timing_phase(args, "cleanup")
    if summary and summary["status"] == "not_applicable":
        print("Focused emulator diagnostic not applicable: " + str(summary["result"]), flush=True)
        return 77
    if not summary or summary["status"] != "passed":
        raise RuntimeError("Full emulator run is nonpassing: " + str(evidence))
    print(("Focused emulator diagnostic passed: " if args.suite else "Full emulator run passed: ") + str(summary["result"]), flush=True)
    return 0


def audit(args):
    if not args.attempt or args.attempt.parent != args.source_root.parent / "tmp" or not args.attempt.name.startswith("beam-full-"):
        raise RuntimeError("Audit requires an existing owned full-run attempt under workspace tmp")
    evidence = args.attempt
    invocation = read_json(evidence / "invocation.json")
    receipt = read_json(evidence / "image/prepared-receipt.json")
    if receipt["identity"]["fingerprint"] != invocation["identity"]["fingerprint"]:
        raise RuntimeError("Prepared image receipt does not match the execution invocation")
    cleanup_result = read_json(evidence / "cleanup.json")
    if cleanup_result["remaining"]:
        raise RuntimeError("Execution left owned processes; a new audit cannot turn it green")
    artifacts = verify_execution_image(args, evidence / "image", invocation["identity"])
    if artifacts != invocation["artifacts"]:
        raise RuntimeError("Executed candidate artifacts differ from invocation evidence")
    selection = invocation_selection(invocation)
    focus = None
    if selection["mode"] == "focused" and not selection.get("legacy"):
        focus = focus_evidence(evidence / "image", evidence / "logs")
        if focus != invocation.get("focus"):
            raise RuntimeError("Focused specification or inventory differs from execution evidence")
        if "execution_suites" not in focus["inventory"]:
            selection = dict(selection, legacy=True)
    failures = execution_integrity_failures(evidence)
    if failures:
        raise RuntimeError("Execution integrity prevents standalone audit: " + "; ".join(failures))
    execution_policy_identity(args.source_root, evidence, invocation["identity"])
    report = evidence / "audits" / (datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:6])
    summary = classify(args, evidence / "image", evidence / "logs", receipt, selection, report,
                       focus=focus, execution_identity=invocation["identity"])
    summary["audit_driver_sha256"] = sha(__file__)
    summary["execution_evidence_sha256"] = {
        str(path.relative_to(evidence)): sha(path) for path in
        [evidence / "logs/events.jsonl", evidence / "logs/ct-result.json", evidence / "logs/runtime-facts.json",
         evidence / "invocation.json", evidence / "image/prepared-receipt.json", evidence / "cleanup.json",
         evidence / "execution-skip-policy.json"]}
    write_json(report / "audit.json", summary)
    label = "Historically reclassified execution evidence: " if summary["historical_reclassification"] else "Audited execution evidence: "
    print(label + str(report), flush=True)
    if summary["status"] == "not_applicable":
        return 77
    if summary["status"] != "passed":
        raise RuntimeError("Execution remains nonpassing after audit")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "run", "audit"))
    for name in ("source-root", "binary-root", "full-root", "runtime-bin", "native-root", "native-manifest", "process-tool", "support-files"):
        parser.add_argument("--" + name, type=lambda p: Path(p).resolve(), required=True)
    parser.add_argument("--flavor", choices=("emu", "jit"), required=True)
    parser.add_argument("--build-type", choices=("opt", "debug"), required=True)
    parser.add_argument("--suite")
    parser.add_argument("--group")
    parser.add_argument("--cases")
    parser.add_argument("--timeout", type=int, default=21600)
    parser.add_argument("--attempt", type=lambda p: Path(p).resolve())
    args = parser.parse_args()
    if args.suite == "full":
        parser.error("--suite must name one suite; omit it for the full CTest campaign")
    if (args.group is not None or args.cases is not None) and not args.suite:
        parser.error("--group and --cases require --suite")
    if args.suite is not None and not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_]*_SUITE", args.suite):
        parser.error("--suite must be a prepared suite module name")
    if args.group is not None and not re.fullmatch(r"[a-zA-Z][a-zA-Z0-9_]*", args.group):
        parser.error("--group must be one nonempty group name")
    if args.cases is not None and (not args.cases or any(not re.fullmatch(r"[a-zA-Z0-9_][a-zA-Z0-9_-]*", case) for case in args.cases.split(","))):
        parser.error("--cases must be a nonempty comma-separated list of case names")
    if args.cases and len(set(args.cases.split(","))) != len(args.cases.split(",")):
        parser.error("--cases contains duplicate names")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.full_root != args.binary_root / "tests/emulator/full" or args.native_root != args.full_root / "native":
        parser.error("Full support roots must belong to this build's tests/emulator/full directory")
    if args.support_files != args.full_root / "support-files.txt":
        parser.error("Selected imports must come from this build's tests/emulator/full/support-files.txt")
    args.full_root.mkdir(parents=True, exist_ok=True)
    with lifecycle_lock(args.full_root / "lifecycle.lock", args.action == "prepare"):
        return {"prepare": prepare, "run": run, "audit": audit}[args.action](args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print("Emulator corpus error: " + str(error), file=sys.stderr, flush=True)
        sys.exit(1)
