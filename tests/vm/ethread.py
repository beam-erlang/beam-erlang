"""Run unchanged native ethread cases against the production thread library."""

from pathlib import Path
import argparse
import json
import subprocess

from support.process import child_process, isolated_environment


CASES = (
    "create_join_thread", "equal_tids", "mutex", "try_lock_mutex", "cond_wait",
    "broadcast", "tsd", "spinlock", "rwspinlock", "rwmutex", "atomic",
    "dw_atomic_massage", "thread_name",
)
SKIP = 77


def classify(case, returncode, stdout, stderr):
    """An exit status alone cannot distinguish the fixture's pass and skip."""
    if returncode != 0:
        raise AssertionError(f"native case exited with status {returncode}")
    terminal = []
    for line in (stdout.splitlines() + stderr.splitlines()):
        for kind in (b"SUCCESS", b"SKIP", b"FAILURE"):
            marker = b"ETHR-TEST-" + kind
            if line.startswith(marker):
                terminal.append((kind, line[len(marker):]))
                break
        else:
            if line.startswith(b"ETHR-TEST-"):
                if not (line.startswith(b"ETHR-TEST-PID")
                        and line[len(b"ETHR-TEST-PID"):].isdigit()):
                    raise AssertionError(f"malformed native marker: {line!r}")
    if len(terminal) != 1:
        raise AssertionError(f"expected one terminal marker, got {terminal!r}")
    kind, reason = terminal[0]
    if kind == b"SUCCESS":
        return 0
    if (kind == b"SKIP" and case == "thread_name"
            and reason == b"thread names are not supported"):
        return SKIP
    raise AssertionError(f"unexpected native result for {case}: {kind!r} {reason!r}")


def execute(command, case, work, timeout=600):
    if case not in CASES:
        raise ValueError(f"case is outside the native pilot: {case}")
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    environment = isolated_environment(work)
    stdout, stderr = work / "stdout.log", work / "stderr.log"
    report = dict(case=case, command=[str(part) for part in command],
                  timeout_seconds=timeout, outcome="failed")
    try:
        with stdout.open("wb") as out, stderr.open("wb") as err:
            with child_process(command, cwd=work, env=environment,
                               stdin=subprocess.DEVNULL, stdout=out, stderr=err) as child:
                child.communicate(timeout=timeout)
                report["native_returncode"] = child.returncode
                # Keep protocol failures inside the process-group cleanup scope,
                # even if the leader has already exited and left descendants.
                status = classify(case, child.returncode, stdout.read_bytes(), stderr.read_bytes())
        report["outcome"] = "skipped" if status == SKIP else "passed"
        if status == SKIP:
            report["reason"] = "thread names are not supported"
        return status
    except Exception as error:
        report["reason"] = str(error)
        raise
    finally:
        (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    args = parser.parse_args()
    status = execute([args.helper.resolve(strict=True), args.case], args.case, args.test_dir)
    print(f"{args.case}: {'SKIP: thread names are not supported' if status == SKIP else 'PASS'}; "
          f"logs: {args.test_dir.resolve()}")
    raise SystemExit(status)


if __name__ == "__main__":
    main()
