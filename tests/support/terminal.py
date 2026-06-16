"""POSIX terminal mechanics for installed acceptance."""

import errno
import fcntl
import os
import pty
import re
import select
import signal
import termios
import time

from .process import check


TIMEOUT = 10
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b\][^\x07]*(?:\x07|\x1b\\)")


def terminal_text(data):
    return ANSI.sub("", data.decode("utf-8", errors="replace")).replace("\r", "")


class TerminalShell:
    def __init__(self, command, cwd, env):
        self.fd, slave = pty.openpty()
        self.initial_flags = termios.tcgetattr(slave)[3]
        self.pid = os.fork()
        if self.pid == 0:
            try:
                os.close(self.fd)
                os.setsid()
                fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
                for descriptor in (0, 1, 2):
                    os.dup2(slave, descriptor)
                if slave > 2:
                    os.close(slave)
                os.chdir(cwd)
                os.execve(command[0], command, env)
            finally:
                os._exit(127)
        os.close(slave)
        self.status = None
        self.output = bytearray()
        self.completed = False

    def read(self):
        if select.select([self.fd], [], [], 0.05)[0]:
            try:
                self.output.extend(os.read(self.fd, 65536))
            except OSError as error:
                if error.errno != errno.EIO:
                    raise

    def poll(self):
        if self.status is None:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = os.waitstatus_to_exitcode(status)
        return self.status

    def expect(self, pattern, start=0):
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            self.read()
            if re.search(pattern, terminal_text(self.output[start:])):
                return
            check(self.poll() is None,
                  f"shell exited before {pattern!r}:\n{terminal_text(self.output)}")
        raise AssertionError(f"shell never produced {pattern!r}:\n"
                             f"{terminal_text(self.output)}")

    def send_and_expect(self, text, pattern):
        start = len(self.output)
        os.write(self.fd, text.encode("utf-8"))
        self.expect(pattern, start)

    def wait_exit(self, status):
        deadline = time.monotonic() + TIMEOUT
        while self.poll() is None and time.monotonic() < deadline:
            self.read()
        check(self.status == status,
              f"shell status={self.status}, expected={status}:\n"
              f"{terminal_text(self.output)}")
        restored_flags = termios.tcgetattr(self.fd)[3]
        mask = termios.ICANON | termios.ECHO
        check(restored_flags & mask == self.initial_flags & mask,
              "shell shutdown did not restore terminal input/echo modes")

        self.completed = True

    def close(self):
        try:
            # Failed sessions may leave helpers after the leader exits.
            # Successful shutdown must stand on its own cleanup observations.
            if not self.completed:
                os.killpg(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if self.status is None:
            _, status = os.waitpid(self.pid, 0)
            self.status = os.waitstatus_to_exitcode(status)
        os.close(self.fd)


def capture_terminal(command, *, cwd, env, timeout):
    """Capture a bounded PTY client whose output includes terminal newlines."""
    import subprocess
    from .process import child_process

    master, slave = pty.openpty()
    output = bytearray()
    try:
        with child_process(command, cwd=cwd, env=env, stdin=slave,
                           stdout=slave, stderr=slave) as process:
            os.close(slave)
            slave = None
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        chunk = os.read(master, 65536)
                    except OSError as error:
                        if error.errno != errno.EIO:
                            raise
                        break
                    if not chunk:
                        break
                    output.extend(chunk)
                elif process.poll() is not None:
                    break
            process.wait(timeout=max(0.01, deadline - time.monotonic()))
            return subprocess.CompletedProcess(command, process.returncode, bytes(output), b"")
    except Exception as error:
        raise AssertionError(f"terminal capture failed: {error}\noutput={bytes(output)!r}") from error
    finally:
        if slave is not None:
            os.close(slave)
        os.close(master)
