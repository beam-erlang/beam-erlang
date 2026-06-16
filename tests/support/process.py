"""Bound child process groups without masking successful-runtime cleanup."""

from contextlib import contextmanager
from pathlib import Path
import os
import signal
import subprocess

_windows_jobs = {}


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
    if os.name == 'nt':
        for name in list(env):
            if name.upper() == 'PATH': del env[name]
        env.update(PATH=str(Path(os.environ['SystemRoot']) / 'System32'),
                   LANG='C.UTF-8', TERM='xterm', PYTHONIOENCODING='utf-8')
    else:
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
    if os.name == 'nt':
        if pid not in _windows_jobs:
            raise AssertionError(f'No owned Windows job for child {pid}')
        _windows_jobs[pid].terminate()
        return
    # The leader may already have exited while a helper still holds a pipe.
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


@contextmanager
def child_process(command, *, cwd, env, owned_helpers=(), **kwargs):
    command = [str(part) for part in command]
    kwargs.setdefault("close_fds", True)
    job = None
    if os.name == 'nt':
        from .windows_job import Job
        job = Job()
        # Captured checks never need a visible console. Interactive sessions
        # use their explicit terminal adapter instead.
        kwargs['creationflags'] = kwargs.get('creationflags', 0) | 4 | subprocess.DETACHED_PROCESS
        try:
            child = subprocess.Popen(command, cwd=cwd, env=env, **kwargs)
            _windows_jobs[child.pid] = job
            job.attach_and_resume(child)
        except BaseException:
            job.close()
            if 'child' in locals():
                child.kill()
                child.communicate(timeout=5)
                _windows_jobs.pop(child.pid, None)
            raise
    else:
        child = subprocess.Popen(command, cwd=cwd, env=env,
                                 start_new_session=True, **kwargs)
    try:
        yield child
        check(child.poll() is not None, "child interaction ended without waiting for exit")
        if job:
            if not job.settled():
                survivors = job.survivors()
                allowed = {os.path.normcase(str(Path(path).resolve(strict=True))) for path in owned_helpers}
                check(survivors and all(os.path.normcase(path) in allowed for _, path in survivors),
                      f'owned child processes survived successful interaction: {survivors}')
                # Build tools may start explicitly selected persistent helpers.
                # Their exact OS-observed images must match before retiring the
                # owned job. Product runs retain strict survivor rejection.
                child.terminated_helpers = survivors
                job.terminate()
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
        if job:
            try:
                if job.active(): job.terminate()
            finally:
                job.close()
                _windows_jobs.pop(child.pid, None)
                child._handle.Close()


def run(command, *, cwd, env, timeout, status=0, input_data=None,
        text=False, errors="strict", owned_helpers=()):
    """Return captured bytes by default; status=None leaves checking to the caller."""
    with child_process(command, cwd=cwd, env=env, owned_helpers=owned_helpers,
                       stdin=subprocess.PIPE if input_data is not None else subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        out, err = child.communicate(input_data, timeout=timeout)
        accepted = status if isinstance(status, tuple) else (status,)
        check(status is None or child.returncode in accepted,
              f"status={child.returncode}, expected={status}\nstdout={out!r}\nstderr={err!r}")
        if text:
            out, err = out.decode("utf-8", errors), err.decode("utf-8", errors)
        result = subprocess.CompletedProcess([str(part) for part in command], child.returncode, out, err)
    result.terminated_helpers = getattr(child, 'terminated_helpers', [])
    return result
