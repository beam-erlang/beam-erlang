/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_CLI_WINDOWS_PROCESS_H
#define BEAM_CLI_WINDOWS_PROCESS_H

static wchar_t *cli_executable(const char *name)
{
    wchar_t *wide = beam_windows_path(name);
    if (!wide) return NULL;
    if (wcschr(wide, L'\\') || wcschr(wide, L':')) return wide;
    const wchar_t *path = _wgetenv(L"PATH");
    if (!path) { free(wide); errno = ENOENT; return NULL; }
    for (;;) {
        const wchar_t *end = wcschr(path, L';');
        size_t length = end ? (size_t)(end-path) : wcslen(path);
        size_t capacity = length + wcslen(wide) + 7;
        wchar_t *candidate = malloc(capacity * sizeof(*candidate));
        if (!candidate) { free(wide); return NULL; }
        swprintf(candidate, capacity, L"%.*ls%ls%ls%ls", (int)length, path,
            length ? L"\\" : L"", wide, wcsrchr(wide, L'.') ? L"" : L".exe");
        DWORD attributes = GetFileAttributesW(candidate);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            free(wide); return candidate;
        }
        free(candidate);
        if (!end) break;
        path = end + 1;
    }
    free(wide); errno = ENOENT; return NULL;
}

static int application_exec(char **argv, int *exit_status)
{
    wchar_t *executable = cli_executable(argv[0]);
    if (!executable) return 0;
    /* Each UTF-16 argument can at most double through CRT quoting. */
    wchar_t *command = calloc(32768, sizeof(*command));
    if (!command) { free(executable); return 0; }
    size_t used = 0;
    for (size_t i = 0; argv[i]; ++i) {
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[i], -1, NULL, 0);
        if (!count || used + 2*(size_t)count + 4 >= 32768) { errno = E2BIG; goto invalid; }
        wchar_t *arg = malloc((size_t)count * sizeof(*arg));
        if (!arg) goto invalid;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[i], -1, arg, count)) {
            free(arg); errno = EILSEQ; goto invalid;
        }
        if (i) command[used++] = L' ';
        command[used++] = L'"';
        size_t slashes = 0;
        for (wchar_t *p = arg;; ++p) {
            if (*p == L'\\') { ++slashes; continue; }
            size_t copies = (*p == L'"' || !*p) ? slashes*2 : slashes;
            while (copies--) command[used++] = L'\\';
            slashes = 0;
            if (!*p) break;
            if (*p == L'"') command[used++] = L'\\';
            command[used++] = *p;
        }
        command[used++] = L'"'; free(arg);
    }
    STARTUPINFOEXW startup = {0}; PROCESS_INFORMATION process = {0};
    HANDLE streams[3] = {0}, job = NULL; SIZE_T size = 0;
    int succeeded = 0; DWORD failure = 0;
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    for (unsigned i = 0; i < 3; ++i) {
        HANDLE source = GetStdHandle(i == 0 ? STD_INPUT_HANDLE : i == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        if (!source || source == INVALID_HANDLE_VALUE ||
            !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &streams[i],
                             0, TRUE, DUPLICATE_SAME_ACCESS)) { failure = GetLastError(); goto done; }
    }
    startup.StartupInfo.hStdInput = streams[0]; startup.StartupInfo.hStdOutput = streams[1];
    startup.StartupInfo.hStdError = streams[2];
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    startup.lpAttributeList = malloc(size);
    if (!startup.lpAttributeList) goto done;
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) {
        free(startup.lpAttributeList); startup.lpAttributeList = NULL; failure = GetLastError(); goto done;
    }
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   streams, sizeof(streams), NULL, NULL)) { failure = GetLastError(); goto done; }
    job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        failure = GetLastError(); goto done;
    }
    if (!CreateProcessW(executable, command, NULL, NULL, TRUE,
        CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT |
        (GetConsoleWindow() ? 0 : DETACHED_PROCESS), NULL, NULL, &startup.StartupInfo, &process)) {
        failure = GetLastError(); goto done;
    }
    if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == (DWORD)-1) {
        failure = GetLastError(); TerminateProcess(process.hProcess, 1); goto done;
    }
    DWORD status;
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &status)) { failure = GetLastError(); goto done; }
    *exit_status = (int)status;
    succeeded = 1;
done:
    /* Retire descendants before releasing command-owned files or handles. */
    if (job) {
        if (!TerminateJobObject(job,1)) { failure = GetLastError(); succeeded = 0; }
        ULONGLONG deadline = GetTickCount64()+5000;
        for (;;) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION counts;
            if (!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&counts,sizeof(counts),NULL)) {
                failure = GetLastError(); succeeded = 0; break;
            }
            if (!counts.ActiveProcesses) break;
            if (GetTickCount64() >= deadline) { failure = ERROR_TIMEOUT; succeeded = 0; break; }
            Sleep(10);
        }
        if (!CloseHandle(job)) { failure = GetLastError(); succeeded = 0; }
    }
    if (process.hProcess) { WaitForSingleObject(process.hProcess, 5000); CloseHandle(process.hProcess); }
    if (process.hThread) CloseHandle(process.hThread);
    if (startup.lpAttributeList) { DeleteProcThreadAttributeList(startup.lpAttributeList); free(startup.lpAttributeList); }
    for (unsigned i = 0; i < 3; ++i) if (streams[i]) CloseHandle(streams[i]);
    free(executable); free(command);
    if (!succeeded && failure) _dosmaperr(failure);
    return succeeded;
invalid:
    free(executable); free(command); return 0;
}
#endif
