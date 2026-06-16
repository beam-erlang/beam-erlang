/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ERL_WIN_PROCESS_H
#define ERL_WIN_PROCESS_H

/* A managed VM owns port descendants until its quiescent cleanup boundary.
 * Closing an individual port preserves ordinary pipe-close semantics. */
static HANDLE vm_process_job;
static SRWLOCK vm_process_job_lock = SRWLOCK_INIT;

static HANDLE managed_process_job(void)
{
    AcquireSRWLockExclusive(&vm_process_job_lock);
    if (!vm_process_job) {
        HANDLE job = CreateJobObjectW(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            DWORD error = GetLastError();
            CloseHandle(job); job = NULL; SetLastError(error);
        }
        vm_process_job = job;
    }
    HANDLE result = vm_process_job;
    ReleaseSRWLockExclusive(&vm_process_job_lock);
    return result;
}

static BOOL create_owned_process(const wchar_t *application, wchar_t *command, DWORD flags,
                                void *environment, const wchar_t *directory,
                                const STARTUPINFOW *selected, PROCESS_INFORMATION *process)
{
    STARTUPINFOEXW startup = {0};
    HANDLE inherited[3] = {0};
    HANDLE sources[3] = {selected->hStdInput,selected->hStdOutput,selected->hStdError};
    SIZE_T size = 0;
    DWORD error = 0;
    BOOL ok = FALSE;
    HANDLE job = erts_bare_mode ? managed_process_job() : NULL;
    if (erts_bare_mode && !job) return FALSE;
    startup.StartupInfo = *selected;
    startup.StartupInfo.cb = sizeof(startup);
    for (unsigned i=0; i<3; ++i) {
        if (!DuplicateHandle(GetCurrentProcess(),sources[i],GetCurrentProcess(),&inherited[i],
                             0,TRUE,DUPLICATE_SAME_ACCESS)) goto done;
    }
    startup.StartupInfo.hStdInput = inherited[0];
    startup.StartupInfo.hStdOutput = inherited[1];
    startup.StartupInfo.hStdError = inherited[2];
    InitializeProcThreadAttributeList(NULL,1,0,&size);
    startup.lpAttributeList = erts_alloc(ERTS_ALC_T_TMP,size);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size)) {
        error = GetLastError();
        erts_free(ERTS_ALC_T_TMP,startup.lpAttributeList); startup.lpAttributeList = NULL;
        goto done;
    }
    if (!UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited,sizeof(inherited),NULL,NULL)) goto done;
    ok = CreateProcessW(application,command,NULL,NULL,TRUE,
                        flags | EXTENDED_STARTUPINFO_PRESENT | (job ? CREATE_SUSPENDED : 0),
                        environment,directory,&startup.StartupInfo,process);
    if (ok && job && (!AssignProcessToJobObject(job,process->hProcess) ||
                     ResumeThread(process->hThread) == (DWORD)-1)) {
        error = GetLastError();
        TerminateProcess(process->hProcess,1);
        WaitForSingleObject(process->hProcess,INFINITE);
        CloseHandle(process->hThread); CloseHandle(process->hProcess);
        memset(process,0,sizeof(*process)); ok = FALSE;
    }
done:
    if (!error) error = GetLastError();
    if (startup.lpAttributeList) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        erts_free(ERTS_ALC_T_TMP,startup.lpAttributeList);
    }
    for (unsigned i=0; i<3; ++i) if (inherited[i]) CloseHandle(inherited[i]);
    SetLastError(error);
    return ok;
}

static void cleanup_managed_processes(void)
{
    if (!vm_process_job) return;
    if (!TerminateJobObject(vm_process_job,1)) ERTS_INTERNAL_ERROR("could not stop VM port descendants");
    ULONGLONG deadline = GetTickCount64()+5000;
    for (;;) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION counts;
        if (!QueryInformationJobObject(vm_process_job,JobObjectBasicAccountingInformation,
                                        &counts,sizeof(counts),NULL))
            ERTS_INTERNAL_ERROR("could not observe VM port descendants");
        if (!counts.ActiveProcesses) break;
        if (GetTickCount64() >= deadline) ERTS_INTERNAL_ERROR("VM port descendants did not stop");
        Sleep(10);
    }
    if (!CloseHandle(vm_process_job)) ERTS_INTERNAL_ERROR("could not release VM process job");
    vm_process_job = NULL;
}
#endif
