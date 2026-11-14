/* Private Windows process ownership, launch receipts and run containment.
 * Never infer ownership from a PID alone, and never terminate a process tree
 * using a global executable name. */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "beam_emulator_windows_process.h"

struct identity {
    DWORD pid, ppid;
    uint64_t birth;
    wchar_t path[32768];
    wchar_t sid[256];
};

static int failure(const char *operation)
{
    fprintf(stderr, "{\"error\":\"%s\",\"winerror\":%lu}\n",
            operation, (unsigned long) GetLastError());
    return 1;
}

static wchar_t *wide(const char *text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    wchar_t *result;
    if (!count) return NULL;
    result = malloc((size_t) count * sizeof(*result));
    if (!result) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, count)) {
        free(result); return NULL;
    }
    return result;
}

static void json_wide(FILE *out, const wchar_t *value)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                   NULL, 0, NULL, NULL);
    unsigned char *text = bytes ? malloc((size_t) bytes) : NULL, *p;
    fputc('"', out);
    if (text && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                   (char *) text, bytes, NULL, NULL)) {
        for (p = text; *p; ++p) {
            if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
            else if (*p < 0x20) fprintf(out, "\\u%04x", *p);
            else fputc(*p, out);
        }
    }
    free(text);
    fputc('"', out);
}

static int token_sid(HANDLE process, wchar_t *buffer, size_t capacity)
{
    HANDLE token;
    DWORD bytes = 0;
    TOKEN_USER *user = NULL;
    LPWSTR sid = NULL;
    int ok = 0;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return 0;
    GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
    user = malloc(bytes);
    if (user && GetTokenInformation(token, TokenUser, user, bytes, &bytes) &&
        ConvertSidToStringSidW(user->User.Sid, &sid) && wcslen(sid) < capacity) {
        wcscpy_s(buffer, capacity, sid);
        ok = 1;
    }
    if (sid) LocalFree(sid);
    free(user);
    CloseHandle(token);
    return ok;
}

static DWORD parent_pid(DWORD pid)
{
    PROCESSENTRY32W entry;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD parent = 0;
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ProcessID == pid) { parent = entry.th32ParentProcessID; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return parent;
}

static int identity_handle(HANDLE process, DWORD pid, struct identity *id, int suspended)
{
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER birth;
    DWORD capacity = sizeof(id->path) / sizeof(id->path[0]);
    wchar_t current_sid[256];
    memset(id, 0, sizeof(*id));
    if (!suspended && WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        SetLastError(ERROR_NOT_FOUND); return 0;
    }
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user) ||
        !QueryFullProcessImageNameW(process, 0, id->path, &capacity) ||
        !token_sid(process, id->sid, sizeof(id->sid) / sizeof(id->sid[0])) ||
        !token_sid(GetCurrentProcess(), current_sid,
                   sizeof(current_sid) / sizeof(current_sid[0]))) return 0;
    if (wcscmp(id->sid, current_sid)) { SetLastError(ERROR_ACCESS_DENIED); return 0; }
    birth.LowPart = created.dwLowDateTime;
    birth.HighPart = created.dwHighDateTime;
    id->birth = birth.QuadPart;
    id->pid = pid;
    id->ppid = parent_pid(pid);
    return 1;
}

static int read_identity(DWORD pid, struct identity *id)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                 FALSE, pid);
    int ok;
    if (!process) return 0;
    ok = identity_handle(process, pid, id, 0);
    CloseHandle(process);
    return ok;
}

static void print_identity(FILE *out, const struct identity *id)
{
    fprintf(out, "\"pid\":%lu,\"ppid\":%lu,\"pgid\":0,\"uid\":",
            (unsigned long) id->pid, (unsigned long) id->ppid);
    json_wide(out, id->sid);
    /* Preserve every FILETIME tick. birth_usec is the legacy field name, and
     * its Windows unit is explicitly recorded rather than truncating 100ns. */
    fprintf(out, ",\"birth_sec\":%" PRIu64 ",\"birth_usec\":%" PRIu64
            ",\"birth_ticks\":%" PRIu64 ",\"birth_unit\":\"windows-filetime-100ns\",\"path\":",
            id->birth / 10000000, id->birth % 10000000, id->birth);
    json_wide(out, id->path);
}

static int number(const char *text, uint64_t maximum, uint64_t *value)
{
    unsigned long long parsed;
    char *end;
    if (!text || *text < '0' || *text > '9') return 0;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || *end || parsed > maximum) return 0;
    *value = parsed;
    return 1;
}

static HANDLE verified_process(char **args, DWORD access, struct identity *id)
{
    uint64_t pid, seconds, ticks;
    wchar_t *expected;
    HANDLE process;
    if (!number(args[0], UINT32_MAX, &pid) || pid <= 1 ||
        !number(args[1], UINT64_MAX / 10000000, &seconds) ||
        !number(args[2], 9999999, &ticks) || !(expected = wide(args[3]))) {
        SetLastError(ERROR_INVALID_PARAMETER); return NULL;
    }
    process = OpenProcess(access | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                          FALSE, (DWORD) pid);
    if (process && (!identity_handle(process, (DWORD) pid, id, 0) ||
                    id->birth != seconds * 10000000 + ticks ||
                    _wcsicmp(id->path, expected))) {
        CloseHandle(process); process = NULL; SetLastError(ERROR_ACCESS_DENIED);
    }
    free(expected);
    return process;
}

static int valid_job_name(const wchar_t *name)
{
    return !wcsncmp(name, L"Local\\beam-full-", 16) && wcslen(name) < 200;
}

static HANDLE open_job(const wchar_t *name)
{
    if (!valid_job_name(name)) { SetLastError(ERROR_INVALID_NAME); return NULL; }
    return OpenJobObjectW(JOB_OBJECT_ASSIGN_PROCESS | JOB_OBJECT_QUERY, FALSE, name);
}

static int assign_owned_job(HANDLE job, HANDLE process)
{
    BOOL member;
    if (!IsProcessInJob(process, job, &member)) return 0;
    if (member) return 1;
    /* CTest may already supply an outer host Job. Let Windows validate nesting;
     * membership in another Job is never evidence of ownership by this run. */
    if (!AssignProcessToJobObject(job, process)) return 0;
    if (!IsProcessInJob(process, job, &member)) return 0;
    if (!member) SetLastError(ERROR_ACCESS_DENIED);
    return member != FALSE;
}

static int assign_environment_job(HANDLE process)
{
    wchar_t name[256];
    HANDLE job;
    BOOL ok;
    DWORD count = GetEnvironmentVariableW(L"BEAM_TEST_JOB_NAME", name, 256);
    if (!count || count >= 256 || !(job = open_job(name))) return 0;
    ok = assign_owned_job(job, process);
    CloseHandle(job);
    return ok;
}

int beam_test_windows_contain_launcher(void)
{
    if (!assign_environment_job(GetCurrentProcess()))
        return failure("contain private launcher in run Job");
    return 0;
}

static int journal_identity(const struct identity *id)
{
    const char *nonce = getenv("BEAM_TEST_RUN_NONCE");
    wchar_t journal[32768], full[32768], serialization[32768];
    DWORD count, written;
    HANDLE file;
    OVERLAPPED lock;
    FILE *temporary;
    HANDLE serialization_file;
    int serialization_descriptor;
    long bytes;
    char *record;
    int ok = 0;
    const char *p;
    if (!nonce || !*nonce) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    for (p = nonce; *p; ++p) if (!((*p >= 'a' && *p <= 'f') || (*p >= '0' && *p <= '9'))) {
        SetLastError(ERROR_INVALID_PARAMETER); return 0;
    }
    count = GetEnvironmentVariableW(L"BEAM_TEST_PROCESS_JOURNAL", journal, 32768);
    if (!count || count >= 32768 || !GetFullPathNameW(journal, 32768, full, NULL) ||
        _wcsicmp(full, journal)) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (wcslen(full) > 32700) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return 0; }
    swprintf_s(serialization, 32768, L"%s.serialize-%lu", full, GetCurrentProcessId());
    serialization_file = CreateFileW(serialization, GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                                      NULL);
    if (serialization_file == INVALID_HANDLE_VALUE) return 0;
    serialization_descriptor = _open_osfhandle((intptr_t) serialization_file,
                                               _O_RDWR | _O_BINARY);
    if (serialization_descriptor < 0) { CloseHandle(serialization_file); return 0; }
    temporary = _fdopen(serialization_descriptor, "w+b");
    if (!temporary) { _close(serialization_descriptor); return 0; }
    fprintf(temporary, "{\"event\":\"exec\",\"nonce\":\"%s\",", nonce);
    print_identity(temporary, id);
    fputs("}\n", temporary);
    bytes = ftell(temporary);
    record = bytes > 0 ? malloc((size_t) bytes) : NULL;
    rewind(temporary);
    if (!record || fread(record, 1, (size_t) bytes, temporary) != (size_t) bytes) {
        free(record); fclose(temporary); return 0;
    }
    fclose(temporary);
    file = CreateFileW(full, GENERIC_WRITE | GENERIC_READ | SYNCHRONIZE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(record); return 0; }
    memset(&lock, 0, sizeof(lock));
    {
        BY_HANDLE_FILE_INFORMATION info;
        PSECURITY_DESCRIPTOR security = NULL;
        PSID owner = NULL;
        wchar_t current_sid[256];
        LPWSTR owner_sid = NULL;
        int owned;
        if (!GetFileInformationByHandle(file, &info) ||
            info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            SetLastError(ERROR_ACCESS_DENIED); goto done;
        }
        owned = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                 &owner, NULL, NULL, NULL, &security) == ERROR_SUCCESS &&
                ConvertSidToStringSidW(owner, &owner_sid) &&
                token_sid(GetCurrentProcess(), current_sid, 256) &&
                !wcscmp(owner_sid, current_sid);
        if (owner_sid) LocalFree(owner_sid);
        if (security) LocalFree(security);
        if (!owned) { SetLastError(ERROR_ACCESS_DENIED); goto done; }
    }
    if (!LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &lock)) goto done;
    {
        LARGE_INTEGER end;
        end.QuadPart = 0;
        ok = SetFilePointerEx(file, end, NULL, FILE_END) &&
             WriteFile(file, record, (DWORD) bytes, &written, NULL) &&
         written == (DWORD) bytes && FlushFileBuffers(file);
    }
    if (!UnlockFileEx(file, 0, MAXDWORD, MAXDWORD, &lock)) ok = 0;
done:
    CloseHandle(file);
    free(record);
    return ok;
}

/* Quote each argument according to the Microsoft CRT argv parsing rules.
 * Never invoke cmd.exe to translate candidate arguments. */
static wchar_t *command_line(char **argv)
{
    size_t capacity = 1, used = 0, i;
    wchar_t *line;
    for (i = 0; argv[i]; ++i) capacity += strlen(argv[i]) * 4 + 4;
    line = calloc(capacity, sizeof(*line));
    if (!line) return NULL;
    for (i = 0; argv[i]; ++i) {
        wchar_t *arg = wide(argv[i]), *p;
        size_t slashes = 0;
        if (!arg) { free(line); return NULL; }
        if (i) line[used++] = L' ';
        line[used++] = L'"';
        for (p = arg; ; ++p) {
            if (*p == L'\\') { ++slashes; continue; }
            if (*p == L'"' || !*p) {
                size_t count = slashes * 2 + (*p == L'"' ? 1 : 0);
                while (count--) line[used++] = L'\\';
            } else {
                while (slashes--) line[used++] = L'\\';
            }
            slashes = 0;
            if (!*p) break;
            line[used++] = *p;
        }
        line[used++] = L'"';
        free(arg);
    }
    line[used] = 0;
    return line;
}

static int write_record(const char *path, const struct identity *id)
{
    wchar_t *target = wide(path), temporary[32768], full[32768];
    FILE *stream = NULL;
    HANDLE file;
    int descriptor;
    int ok;
    if (!target || !GetFullPathNameW(target, 32768, full, NULL) ||
        _wcsicmp(target, full) || wcslen(target) > 32700) {
        free(target); SetLastError(ERROR_INVALID_PARAMETER); return 0;
    }
    swprintf_s(temporary, 32768, L"%s.part-%lu", target, GetCurrentProcessId());
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        free(target); return 0;
    }
    descriptor = _open_osfhandle((intptr_t) file, _O_WRONLY | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(file); DeleteFileW(temporary); free(target); return 0;
    }
    stream = _fdopen(descriptor, "wb");
    if (!stream) { _close(descriptor); DeleteFileW(temporary); free(target); return 0; }
    fputc('{', stream); print_identity(stream, id); fputs("}\n", stream);
    ok = !ferror(stream) && fflush(stream) == 0 && _commit(_fileno(stream)) == 0;
    if (fclose(stream)) ok = 0;
    if (ok) ok = MoveFileExW(temporary, target,
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temporary);
    free(target); return ok;
}

int beam_test_windows_start(const char *executable, char **argv, int detached,
                            const char *record_path)
{
    wchar_t *program = wide(executable), *line = command_line(argv);
    wchar_t full[32768];
    STARTUPINFOW startup;
    PROCESS_INFORMATION child;
    struct identity id;
    DWORD result = 1, flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
    BOOL created;
    HANDLE null_input = INVALID_HANDLE_VALUE, null_output = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES inherit = {sizeof(inherit), NULL, TRUE};
    if (!program || !line || !GetFullPathNameW(program, 32768, full, NULL) ||
        _wcsicmp(program, full)) {
        free(program); free(line); SetLastError(ERROR_INVALID_PARAMETER);
        return failure("absolute executable path required");
    }
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(&child, 0, sizeof(child));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (detached == 1) {
        flags |= DETACHED_PROCESS;
        null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &inherit, OPEN_EXISTING, 0, NULL);
        null_output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &inherit, OPEN_EXISTING, 0, NULL);
        startup.hStdInput = null_input;
        startup.hStdOutput = startup.hStdError = null_output;
    }
    created = CreateProcessW(program, line, NULL, NULL, TRUE, flags, NULL, NULL,
                              &startup, &child);
    free(program); free(line);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);
    if (null_output != INVALID_HANDLE_VALUE) CloseHandle(null_output);
    if (!created) return failure("create suspended private child");
    if (!identity_handle(child.hProcess, child.dwProcessId, &id, 1) ||
        _wcsicmp(id.path, full) ||
        !assign_environment_job(child.hProcess) || !journal_identity(&id) ||
        (record_path && !write_record(record_path, &id))) {
        DWORD error = GetLastError();
        TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        SetLastError(error); return failure("contain and receipt private child");
    }
    if (ResumeThread(child.hThread) == (DWORD) -1) {
        TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        return failure("resume private child");
    }
    CloseHandle(child.hThread);
    if (detached) result = 0;
    else if (WaitForSingleObject(child.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(child.hProcess, &result);
    CloseHandle(child.hProcess);
    return (int) result;
}

#ifndef BEAM_TEST_PROCESS_LIBRARY
static int inspect_command(const char *text)
{
    struct identity id;
    uint64_t pid;
    if (!number(text, UINT32_MAX, &pid) || pid <= 1 || !read_identity((DWORD) pid, &id))
        return failure("inspect current-user process");
    fputc('{', stdout); print_identity(stdout, &id); fputs("}\n", stdout);
    return 0;
}

static int list_command(void)
{
    PROCESSENTRY32W entry;
    struct identity id;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int comma = 0;
    if (snapshot == INVALID_HANDLE_VALUE) return failure("snapshot processes");
    entry.dwSize = sizeof(entry);
    fputc('[', stdout);
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ProcessID > 1 && read_identity(entry.th32ProcessID, &id)) {
            if (comma++) fputc(',', stdout);
            fputc('{', stdout); print_identity(stdout, &id); fputc('}', stdout);
        }
    } while (Process32NextW(snapshot, &entry));
    fputs("]\n", stdout); CloseHandle(snapshot);
    return 0;
}

static int listener_command(char **args)
{
    uint64_t pid, port;
    struct identity before, after;
    DWORD bytes = 0, status;
    PMIB_TCPTABLE_OWNER_PID table;
    DWORD i;
    int owned = 0;
    if (!number(args[0], UINT32_MAX, &pid) || pid <= 1 ||
        !number(args[1], 65535, &port) || !port || !read_identity((DWORD) pid, &before))
        return failure("listener owner identity");
    status = GetExtendedTcpTable(NULL, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    table = status == ERROR_INSUFFICIENT_BUFFER ? malloc(bytes) : NULL;
    if (!table) return failure("size TCP listener table");
    status = GetExtendedTcpTable(table, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status == NO_ERROR) for (i = 0; i < table->dwNumEntries; ++i) {
        if (table->table[i].dwOwningPid == pid &&
            ntohs((u_short) table->table[i].dwLocalPort) == port &&
            table->table[i].dwLocalAddr == htonl(INADDR_LOOPBACK)) owned = 1;
    }
    free(table);
    if (!owned || !read_identity((DWORD) pid, &after) || before.birth != after.birth ||
        _wcsicmp(before.path, after.path)) { SetLastError(ERROR_NOT_FOUND); return failure("owned loopback listener"); }
    fputs("{\"owned\":true}\n", stdout); return 0;
}

static int job_server(const char *text)
{
    wchar_t *name = wide(text), sid[256], descriptor[512];
    SECURITY_ATTRIBUTES security;
    PSECURITY_DESCRIPTOR acl = NULL;
    HANDLE job;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
    int ch;
    DWORD attempts, create_error;
    if (!name || !valid_job_name(name) ||
        !token_sid(GetCurrentProcess(), sid, sizeof(sid) / sizeof(sid[0]))) {
        free(name); return failure("private job name or owner");
    }
    swprintf_s(descriptor, sizeof(descriptor) / sizeof(descriptor[0]),
                L"D:P(A;;GA;;;%s)", sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor,
            SDDL_REVISION_1, &acl, NULL)) { free(name); return failure("private job ACL"); }
    security.nLength = sizeof(security); security.lpSecurityDescriptor = acl;
    security.bInheritHandle = FALSE;
    job = CreateJobObjectW(&security, name);
    create_error = GetLastError();
    free(name); LocalFree(acl);
    if (!job) { SetLastError(create_error); return failure("create private job"); }
    if (create_error == ERROR_ALREADY_EXISTS) {
        CloseHandle(job); SetLastError(ERROR_ALREADY_EXISTS); return failure("private job already exists");
    }
    memset(&limits, 0, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job); return failure("contain private job");
    }
    fputs("{\"status\":\"ready\",\"job\":", stdout);
    name = wide(text); json_wide(stdout, name); free(name);
    fputs("}\n", stdout); fflush(stdout);
    while ((ch = getchar()) != EOF) { (void) ch; }
    if (!TerminateJobObject(job, 1)) { CloseHandle(job); return failure("terminate private job"); }
    for (attempts = 0; attempts < 100; ++attempts) {
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                       &accounting, sizeof(accounting), NULL)) break;
        if (!accounting.ActiveProcesses) { CloseHandle(job); fputs("{\"remaining\":0}\n", stdout); return 0; }
        Sleep(50);
    }
    CloseHandle(job); SetLastError(ERROR_TIMEOUT); return failure("private job still active");
}

static int tool_main(int argc, char **argv)
{
    struct identity id;
    HANDLE process, job;
    wchar_t *name;
    uint64_t sig;
    if (argc == 3 && !strcmp(argv[1], "inspect")) return inspect_command(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "list")) return list_command();
    if (argc == 4 && !strcmp(argv[1], "owns-listener")) return listener_command(argv + 2);
    if (argc == 3 && !strcmp(argv[1], "job-server")) return job_server(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "spawn"))
        return beam_test_windows_start(argv[3], argv + 3, 2, argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "run"))
        return beam_test_windows_start(argv[3], argv + 3, 0, argv[2]);
    if (argc == 7 && !strcmp(argv[1], "signal")) {
        if (!number(argv[6], 15, &sig) || (sig != 9 && sig != 15)) {
            SetLastError(ERROR_INVALID_PARAMETER); return failure("TERM or KILL required");
        }
        process = verified_process(argv + 2, PROCESS_TERMINATE, &id);
        if (!process) return failure("verify termination identity");
        if (!TerminateProcess(process, sig == 15 ? 0 : 1)) {
            CloseHandle(process); return failure("terminate owned process");
        }
        WaitForSingleObject(process, 5000); CloseHandle(process);
        fputs("{\"signaled\":true}\n", stdout); return 0;
    }
    if (argc == 7 && !strcmp(argv[1], "assign-job")) {
        process = verified_process(argv + 2, PROCESS_SET_QUOTA | PROCESS_TERMINATE, &id);
        if (!process) return failure("verify job assignment identity");
        name = wide(argv[6]); job = name ? open_job(name) : NULL; free(name);
        if (!job || !assign_owned_job(job, process)) {
            if (job) CloseHandle(job); CloseHandle(process); return failure("assign owned process job");
        }
        CloseHandle(job); CloseHandle(process); fputs("{\"assigned\":true}\n", stdout); return 0;
    }
    SetLastError(ERROR_INVALID_PARAMETER);
    return failure("usage: inspect PID | list | signal PID BIRTH_SEC TICKS PATH 9/15 | owns-listener PID PORT | job-server NAME | assign-job PID BIRTH_SEC TICKS PATH NAME | spawn/run RECORD_JSON EXE ARGS");
}

int wmain(int argc, wchar_t **wide_argv)
{
    char **argv = calloc((size_t) argc + 1, sizeof(*argv));
    int i, result;
    if (!argv) return 1;
    for (i = 0; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
                                       NULL, 0, NULL, NULL);
        if (!bytes || !(argv[i] = malloc((size_t) bytes)) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
                                 argv[i], bytes, NULL, NULL)) return 1;
    }
    result = tool_main(argc, argv);
    for (i = 0; i < argc; ++i) free(argv[i]);
    free(argv); return result;
}
#endif
