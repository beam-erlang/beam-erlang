"""Bound child process groups without masking successful-runtime cleanup."""

from contextlib import contextmanager
from pathlib import Path
import os
import signal
import subprocess


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def isolated_environment(work):
    work = Path(work).resolve()
    env = os.environ.copy()
    for name in list(env):
        if name.startswith(("ERL_", "BEAM_", "DYLD_", "LD_", "ESOCK_")) or name in {
                "BINDIR", "ROOTDIR", "EMU", "LD_LIBRARY_PATH", "LD_PRELOAD",
                "LD_AUDIT", "CMAKE_PREFIX_PATH", "BEAM_DIR", "PYTHONPATH",
                "PYTHONHOME", "LC_ALL", "LC_CTYPE"}:
            env.pop(name)
    env.update(PATH="/usr/bin:/bin", LANG="C.UTF-8", TERM="xterm")
    env["HOME"] = str(work / "home")
    for kind in ("CONFIG", "DATA", "CACHE", "STATE"):
        env[f"XDG_{kind}_HOME"] = str(work / kind.lower())
    env["XDG_RUNTIME_DIR"] = str(work / "runtime")
    env["XDG_CONFIG_DIRS"] = str(work / "config-dirs")
    env["XDG_DATA_DIRS"] = str(work / "data-dirs")
    for name in ("HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
                 "XDG_RUNTIME_DIR", "XDG_CONFIG_DIRS", "XDG_DATA_DIRS"):
        Path(env[name]).mkdir(parents=True, exist_ok=True)
    Path(env["XDG_RUNTIME_DIR"]).chmod(0o700)
    return env


def kill_group(pid):
    # The leader may already have exited while a helper still holds a pipe.
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


@contextmanager
def child_process(command, *, cwd, env, **kwargs):
    command = [str(part) for part in command]
    child = subprocess.Popen(command, cwd=cwd, env=env,
                             start_new_session=True, **kwargs)
    try:
        yield child
        check(child.poll() is not None, "child interaction ended without waiting for exit")
    except BaseException as error:
        kill_group(child.pid)
        # communicate() also closes pipes and reaps the direct child.
        if child.stdin is not None and child.stdin.closed:
            child.stdin = None
        out, err = child.communicate()
        if isinstance(error, Exception):
            raise AssertionError(
                f"command failed ({type(error).__name__}: {error}): {command}\n"
                f"stdout={out!r}\nstderr={err!r}") from error
        raise
    finally:
        # Callers must finish their bounded interaction before leaving.
        if child.poll() is None:
            kill_group(child.pid)
            child.wait()
        for stream in (child.stdin, child.stdout, child.stderr):
            if stream is not None:
                stream.close()


def run(command, *, cwd, env, timeout, status=0, input_data=None,
        text=False, errors="strict"):
    """Return captured bytes by default; status=None leaves checking to the caller."""
    with child_process(command, cwd=cwd, env=env,
                       stdin=subprocess.PIPE if input_data is not None else subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        out, err = child.communicate(input_data, timeout=timeout)
        accepted = status if isinstance(status, tuple) else (status,)
        check(status is None or child.returncode in accepted,
              f"status={child.returncode}, expected={status}\nstdout={out!r}\nstderr={err!r}")
        if text:
            out, err = out.decode("utf-8", errors), err.decode("utf-8", errors)
        return subprocess.CompletedProcess([str(part) for part in command], child.returncode, out, err)
