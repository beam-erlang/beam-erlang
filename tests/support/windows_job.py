"""Test-owned process-tree containment; filesystem isolation is separate."""

import ctypes as c
from ctypes import wintypes as w
import time

kernel = c.WinDLL('kernel32', use_last_error=True)
SIZE_T = c.c_size_t

class BasicLimits(c.Structure):
    _fields_ = [('process_time', c.c_int64), ('job_time', c.c_int64),
                ('flags', w.DWORD), ('minimum_working_set', SIZE_T),
                ('maximum_working_set', SIZE_T), ('active_limit', w.DWORD),
                ('affinity', SIZE_T), ('priority', w.DWORD), ('scheduling', w.DWORD)]

class ExtendedLimits(c.Structure):
    _fields_ = [('basic', BasicLimits), ('io', c.c_uint64 * 6),
                ('process_memory', SIZE_T), ('job_memory', SIZE_T),
                ('peak_process_memory', SIZE_T), ('peak_job_memory', SIZE_T)]

class Accounting(c.Structure):
    _fields_ = [('times', c.c_int64 * 4), ('faults', w.DWORD),
                ('total', w.DWORD), ('active', w.DWORD), ('terminated', w.DWORD)]

class ThreadEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD),
                ('pid', w.DWORD), ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]

def api(name, arguments, result=w.BOOL):
    function = getattr(kernel, name)
    function.argtypes, function.restype = arguments, result
    return function

create_job = api('CreateJobObjectW', [c.c_void_p, w.LPCWSTR], w.HANDLE)
set_job = api('SetInformationJobObject', [w.HANDLE, c.c_int, c.c_void_p, w.DWORD])
query_job = api('QueryInformationJobObject', [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.c_void_p])
assign_job = api('AssignProcessToJobObject', [w.HANDLE, w.HANDLE])
terminate_job = api('TerminateJobObject', [w.HANDLE, w.UINT])
close_handle = api('CloseHandle', [w.HANDLE])
snapshot_threads = api('CreateToolhelp32Snapshot', [w.DWORD, w.DWORD], w.HANDLE)
first_thread = api('Thread32First', [w.HANDLE, c.POINTER(ThreadEntry)])
next_thread = api('Thread32Next', [w.HANDLE, c.POINTER(ThreadEntry)])
open_thread = api('OpenThread', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE)
resume_thread = api('ResumeThread', [w.HANDLE], w.DWORD)
open_process = api('OpenProcess', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE)
query_image = api('QueryFullProcessImageNameW', [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)])

def require(value):
    if not value:
        raise c.WinError(c.get_last_error())
    return value

def resume_primary(pid):
    """The new process has never run: resume only its single initial thread."""
    snapshot = snapshot_threads(4, 0)
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    found = []
    try:
        entry = ThreadEntry(size=c.sizeof(ThreadEntry))
        require(first_thread(snapshot, c.byref(entry)))
        while True:
            if entry.pid == pid:
                found.append(entry.tid)
            if not next_thread(snapshot, c.byref(entry)):
                if c.get_last_error() != 18:  # ERROR_NO_MORE_FILES
                    raise c.WinError(c.get_last_error())
                break
    finally:
        require(close_handle(snapshot))
    if len(found) != 1:
        raise AssertionError(f'Cannot identify the suspended primary thread: {found}')
    thread = require(open_thread(2, False, found[0]))  # THREAD_SUSPEND_RESUME
    try:
        previous = resume_thread(thread)
        if previous != 1:
            raise AssertionError(f'Unexpected primary-thread suspension count: {previous}')
    finally:
        require(close_handle(thread))

class Job:
    def __init__(self):
        self.handle = require(create_job(None, None))
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        try:
            require(set_job(self.handle, 9, c.byref(limits), c.sizeof(limits)))
        except BaseException:
            self.close()
            raise

    def attach_and_resume(self, child):
        # Popen retains this process handle. Bind before the suspended primary
        # thread can execute CRT startup, DLL initialization or child creation.
        require(assign_job(self.handle, int(child._handle)))
        resume_primary(child.pid)

    def active(self):
        counts = Accounting()
        require(query_job(self.handle, 1, c.byref(counts), c.sizeof(counts), None))
        return counts.active

    def settled(self):
        deadline = time.monotonic() + 0.2
        while self.active():
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.01)
        return True

    def survivors(self):
        class ProcessIds(c.Structure):
            _fields_ = [('assigned', w.DWORD), ('count', w.DWORD), ('ids', SIZE_T * 4096)]
        processes = ProcessIds()
        require(query_job(self.handle, 3, c.byref(processes), c.sizeof(processes), None))
        if processes.count > 4096:
            raise AssertionError('Owned job process inventory exceeded its observation bound')
        result = []
        for pid in processes.ids[:processes.count]:
            handle = require(open_process(0x1000, False, pid))
            try:
                path = c.create_unicode_buffer(32768)
                length = w.DWORD(len(path))
                require(query_image(handle, 0, path, c.byref(length)))
                result.append((pid, path.value))
            finally:
                require(close_handle(handle))
        return result

    def terminate(self):
        require(terminate_job(self.handle, 1))
        end = time.monotonic() + 5
        while self.active():
            if time.monotonic() >= end:
                raise AssertionError('Owned Windows processes survived job termination')
            time.sleep(0.01)

    def close(self):
        if self.handle:
            handle, self.handle = self.handle, None
            require(close_handle(handle))
