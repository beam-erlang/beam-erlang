#!/usr/bin/env python3
"""Public process-owned entry without either framework's assets."""
from functools import partial
from pathlib import Path
import argparse
from support.installation import add_installation_arguments, resolve_installation, stage_installation, expected_version
import fcntl
import shutil
import os
import pty
import select
import signal
import subprocess
import termios
import time

from support.process import run as capture, isolated_environment


def run(command, env, cwd, status=0, timeout=60):
    return capture(command, env=env, cwd=cwd, timeout=timeout, status=status, text=True, errors="replace")


def terminal_check(command, env, cwd, policy, nonblocking, interrupt):
    master, slave = pty.openpty()
    process = None
    os.set_blocking(slave, not nonblocking)
    initial = termios.tcgetattr(slave)
    initial_flags = fcntl.fcntl(slave, fcntl.F_GETFL)
    output = bytearray()
    deadline = time.monotonic() + 60

    def read_until(marker):
        while marker not in output:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([master], [], [], remaining)[0]:
                raise RuntimeError(f"terminal timed out waiting for {marker!r}: {bytes(output)!r}")
            chunk = os.read(master, 4096)
            if not chunk:
                raise RuntimeError(f"terminal closed before {marker!r}: {bytes(output)!r}")
            output.extend(chunk)

    try:
        process = subprocess.Popen(
            [*command, "251" if interrupt else "250", str(policy)],
            env=env, cwd=cwd, stdin=slave, stdout=slave, stderr=subprocess.PIPE,
            start_new_session=True)
        if interrupt:
            read_until(b"process_root_ready")
            process.send_signal(signal.SIGINT)
            read_until(b"(d)istribution")
            if termios.tcgetattr(slave) != initial or not os.get_blocking(slave):
                raise RuntimeError("break menu did not restore initial terminal mode and blocking I/O")
            os.write(master, b"c\n")
        _, error = process.communicate(timeout=max(1, deadline - time.monotonic()))
        os.set_blocking(master, False)
        while True:
            try:
                chunk = os.read(master, 4096)
                if not chunk:
                    break
                output.extend(chunk)
            except BlockingIOError:
                break
        expected = 2 if interrupt else 1
        if process.returncode or error or output.count(b"process_root_ready") != expected:
            raise RuntimeError(f"terminal policy {policy} failed: {process.returncode} {bytes(output)!r} {error!r}")
        if termios.tcgetattr(slave) != initial:
            raise RuntimeError(f"terminal policy {policy} left terminal settings changed")
        if fcntl.fcntl(slave, fcntl.F_GETFL) != initial_flags:
            raise RuntimeError(f"terminal policy {policy} left stream flags changed")
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.communicate()
        os.close(slave)
        os.close(master)


from support.scratch import scratch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cmake", required=True)
    parser.add_argument("--test-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    add_installation_arguments(parser)
    args = resolve_installation(parser, parser.parse_args())
    args.test_dir = args.test_dir.resolve()
    args.source_dir = args.source_dir.resolve()
    shutil.rmtree(args.test_dir, ignore_errors=True)
    args.test_dir.mkdir(parents=True)
    with scratch(args.test_dir):
        build_work = args.test_dir / "build-work"
        build_work.mkdir()
        env = isolated_environment(build_work)
        execute = partial(run, env=env, cwd=build_work)
        prefix = args.test_dir / "sdk"
        stage_installation(args, prefix)
        bytecode = args.test_dir / "bytecode"
        bytecode.mkdir()
        execute([str(prefix / "bin/beam"), "compile", "-o", str(bytecode),
             "--", str(args.source_dir / "fixtures/process_root.erl")])
        for name in ("preload_probe", "preload_on_load"):
            execute([str(prefix / "bin/beam"), "compile", "-o", str(bytecode),
                     "--", str(args.source_dir / "fixtures" / f"{name}.erl")])
        client = args.test_dir / "client"
        execute([args.cmake, "-S", str(args.source_dir), "-B", str(client),
             f"-DCMAKE_PREFIX_PATH={prefix}"])
        execute([args.cmake, "--build", str(client)])
        stage = args.test_dir / "stage"
        (stage / "bin").mkdir(parents=True)
        (stage / "lib").mkdir()
        shutil.copy2(client / "process_owned", stage / "bin/host")
        shutil.copy2(client / "preloads", stage / "bin/preloads")
        shutil.copy2(prefix / "lib/libbeam.so", stage / "lib/libbeam.so")
        deployed = args.test_dir / "relocated root λ"
        stage.rename(deployed)
        work = args.test_dir / "unrelated"
        work.mkdir()
        env = isolated_environment(work)
        env["PATH"] = str(work)
        env["ERL_CRASH_DUMP"] = str(work / "root.dump")
        result = execute([str(deployed / "bin/preloads"), str(bytecode / "preload_probe.beam"),
                          str(bytecode / "preload_on_load.beam")], env=env, cwd=work)
        if result.stdout != "composition preload lifetime checks passed\n" or result.stderr:
            raise RuntimeError(f"preload lifecycle failed: {result.stdout!r} {result.stderr!r}")
        for requested, expected in ((0, 0), (7, 7), (255, 1), (252, 0), (253, 0), (254, 0)):
            result = execute([str(deployed / "bin/host"), str(bytecode / "process_root.beam"),
                          str(requested)], env=env, cwd=work, status=expected)
            message = ("process_root_cancelled\n" if requested == 253 else
                       "process_root_rejected\n" if requested in (252, 254) else
                       "process_root_ready\n")
            if result.stdout != message:
                raise RuntimeError(f"root did not retain its identity and arguments: {result.stdout!r}")
            if requested == 255:
                if "System process <0.0.0> terminated: root_failed" not in result.stderr:
                    raise RuntimeError(f"root lost fatal failure semantics: {result.stderr!r}")
            elif result.stderr:
                raise RuntimeError(f"unexpected root diagnostics: {result.stderr!r}")
        for policy in range(4):
            master, slave = pty.openpty()
            initial = termios.tcgetattr(slave)
            try:
                result = subprocess.run(
                    [str(deployed / "bin/host"), str(bytecode / "process_root.beam"), "0", str(policy)],
                    env=env, cwd=work, stdin=slave, stdout=slave, stderr=subprocess.PIPE,
                    text=True, timeout=60)
                os.set_blocking(master, False)
                output = bytearray()
                while True:
                    try:
                        chunk = os.read(master, 4096)
                        if not chunk:
                            break
                        output.extend(chunk)
                    except BlockingIOError:
                        break
                if result.returncode or output.replace(b"\r\n", b"\n") != b"process_root_ready\n" or result.stderr:
                    raise RuntimeError(f"break policy {policy} failed: {bytes(output)!r} {result.stderr!r}")
                if termios.tcgetattr(slave) != initial:
                    raise RuntimeError(f"break policy {policy} left terminal settings changed")
            finally:
                os.close(slave)
                os.close(master)
        command = [str(deployed / "bin/host"), str(bytecode / "process_root.beam")]
        for nonblocking in (False, True):
            for policy in range(4):
                terminal_check(command, env, work, policy, nonblocking, interrupt=False)
            terminal_check(command, env, work, 0, nonblocking, interrupt=True)
        print("generic process-owned root checks passed")


if __name__ == "__main__":
    main()
