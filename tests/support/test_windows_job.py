"""Native containment qualification, independent of VM or filesystem denial."""

import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from .process import child_process, isolated_environment, run

@unittest.skipUnless(os.name == 'nt', 'Windows Job Objects')
class WindowsJobTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.work = Path(self.directory.name)
        self.env = isolated_environment(self.work)

    def command(self, source):
        return [sys.executable, '-c', source]

    def test_success_and_literal_arguments(self):
        arguments = ['', 'two words', '日本語', 'a"b', '\\', 'x\\', '$HOME; & echo x']
        result = run(self.command('import json,sys; print(json.dumps(sys.argv[1:]))') + arguments,
                     cwd=self.work, env=self.env, timeout=5)
        self.assertEqual(json.loads(result.stdout), arguments)

    def test_timeout_after_leader_exits_stops_descendants(self):
        source = ("import subprocess,sys; child=subprocess.Popen([sys.executable,'-c',"
                  "'import time; time.sleep(60)'],stdin=sys.stdin,stdout=sys.stdout,stderr=sys.stderr,"
                  "creationflags=subprocess.DETACHED_PROCESS); print(child.pid,flush=True)")
        with self.assertRaisesRegex(AssertionError, 'TimeoutExpired') as caught:
            run(self.command(source), cwd=self.work, env=self.env, timeout=0.3)
        self.assertIn('stdout=',str(caught.exception))

    def test_success_reports_surviving_descendants(self):
        source = ("import subprocess,sys; child=subprocess.Popen([sys.executable,'-c',"
                  "'import time; time.sleep(60)'],stdin=subprocess.DEVNULL,"
                  "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.DETACHED_PROCESS); print(child.pid)")
        with self.assertRaisesRegex(AssertionError, 'owned child processes survived'):
            run(self.command(source), cwd=self.work, env=self.env, timeout=5)

    def test_explicit_build_helper_is_observed_and_retired(self):
        source = ("import subprocess,sys; subprocess.Popen([sys.executable,'-c',"
                  "'import time; time.sleep(60)'],stdin=subprocess.DEVNULL,"
                  "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.DETACHED_PROCESS)")
        result = run(self.command(source), cwd=self.work, env=self.env, timeout=5,
                     owned_helpers=[sys.executable])
        self.assertEqual(len(result.terminated_helpers), 1)
        self.assertEqual(os.path.normcase(result.terminated_helpers[0][1]),
                         os.path.normcase(str(Path(sys.executable).resolve(strict=True))))

    def test_failure_and_interruption_close_owned_process(self):
        for failure in (ValueError('probe failed'), KeyboardInterrupt()):
            with self.assertRaises(BaseException) as caught:
                with child_process(self.command("import time; print('ready',flush=True); time.sleep(60)"),
                                   cwd=self.work,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as child:
                    self.assertEqual(child.stdout.readline().strip(),b'ready')
                    raise failure
            self.assertIsNotNone(child.returncode)
            if isinstance(failure,KeyboardInterrupt): self.assertIs(caught.exception,failure)

    def test_unselected_inheritable_handle_is_excluded(self):
        from .windows_job import kernel, require, close_handle
        class Security(c.Structure):
            _fields_ = [('length',w.DWORD),('descriptor',c.c_void_p),('inherit',w.BOOL)]
        create = kernel.CreateEventW
        create.argtypes, create.restype = [c.POINTER(Security),w.BOOL,w.BOOL,w.LPCWSTR],w.HANDLE
        wait = kernel.WaitForSingleObject
        wait.argtypes, wait.restype = [w.HANDLE,w.DWORD],w.DWORD
        security = Security(c.sizeof(Security),None,True)
        event = require(create(c.byref(security),True,False,None))
        try:
            program = ("import ctypes as c\nk=c.WinDLL('kernel32')\n"
                       "k.SetEvent.argtypes=[c.c_void_p]\ntry:\n"
                       f'    k.SetEvent({int(event)})\n'
                       "except OSError as error:\n"
                       "    if error.winerror not in (6,-1073741816): raise\n")
            run(self.command(program),cwd=self.work,env=self.env,timeout=5)
            self.assertEqual(wait(event,0),258)  # WAIT_TIMEOUT: parent event untouched
        finally:
            require(close_handle(event))

if __name__ == '__main__':
    unittest.main()
