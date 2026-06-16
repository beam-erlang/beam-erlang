/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_PLATFORM_H
#define BEAM_PLATFORM_H

/* Private host primitives. No platform types cross the installed C API. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include "beam_windows_dll.h"

typedef DWORD beam_thread_t;
typedef SRWLOCK beam_mutex_t;
typedef CONDITION_VARIABLE beam_cond_t;
#define BEAM_MUTEX_INITIALIZER SRWLOCK_INIT
static inline beam_thread_t beam_thread_self(void) { return GetCurrentThreadId(); }
static inline int beam_thread_equal(beam_thread_t a, beam_thread_t b) { return a == b; }
static inline int beam_mutex_init(beam_mutex_t *m, const void *unused)
{ (void)unused; InitializeSRWLock(m); return 0; }
static inline int beam_mutex_destroy(beam_mutex_t *m) { (void)m; return 0; }
static inline int beam_mutex_lock(beam_mutex_t *m) { AcquireSRWLockExclusive(m); return 0; }
static inline int beam_mutex_unlock(beam_mutex_t *m) { ReleaseSRWLockExclusive(m); return 0; }
static inline int beam_cond_init(beam_cond_t *c, const void *unused)
{ (void)unused; InitializeConditionVariable(c); return 0; }
static inline int beam_cond_destroy(beam_cond_t *c) { (void)c; return 0; }
static inline int beam_cond_signal(beam_cond_t *c) { WakeConditionVariable(c); return 0; }
static inline int beam_cond_wait(beam_cond_t *c, beam_mutex_t *m)
{ return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : (int)GetLastError(); }
static inline unsigned beam_reference_add(unsigned *value, int delta)
{ return (unsigned)(InterlockedExchangeAdd((volatile LONG *)value, delta) + delta); }

static inline int beam_windows_main_thread(void)
{
    DWORD process = GetCurrentProcessId(), first = 0;
    int ambiguous = 0;
    ULONGLONG earliest = ~(ULONGLONG)0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    THREADENTRY32 entry = {sizeof(entry)};
    if (!Thread32First(snapshot, &entry)) { CloseHandle(snapshot); return -1; }
    do {
        if (entry.th32OwnerProcessID != process) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) { CloseHandle(snapshot); return -1; }
        FILETIME created, exited, kernel, user;
        BOOL ok = GetThreadTimes(thread, &created, &exited, &kernel, &user);
        CloseHandle(thread);
        if (!ok) { CloseHandle(snapshot); return -1; }
        ULARGE_INTEGER time;
        time.LowPart = created.dwLowDateTime; time.HighPart = created.dwHighDateTime;
        if (time.QuadPart < earliest) {
            earliest = time.QuadPart; first = entry.th32ThreadID; ambiguous = 0;
        } else if (time.QuadPart == earliest) ambiguous = 1;
    } while (Thread32Next(snapshot, &entry));
    DWORD error = GetLastError();
    CloseHandle(snapshot);
    if (error != ERROR_NO_MORE_FILES || !first || ambiguous) return -1;
    return first == GetCurrentThreadId();
}

static inline wchar_t *beam_windows_text(const char *text)
{
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!size) { errno = EINVAL; return NULL; }
    wchar_t *wide = malloc((size_t)size * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, size)) {
        free(wide); errno = EINVAL; return NULL;
    }
    return wide;
}

static inline wchar_t *beam_windows_path(const char *text)
{
    wchar_t *wide = beam_windows_text(text);
    if (wide) for (wchar_t *p = wide; *p; ++p) if (*p == L'/') *p = L'\\';
    return wide;
}

static inline char *beam_windows_utf8(const wchar_t *wide)
{
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (!size) { errno = EINVAL; return NULL; }
    char *text = malloc((size_t)size);
    if (!text) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, text, size, NULL, NULL)) {
        free(text); errno = EINVAL; return NULL;
    }
    return text;
}

static inline int beam_path_absolute(const char *path)
{
    return path && ((path[0] && path[1] == ':' &&
                     ((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) &&
                     (path[2] == '/' || path[2] == '\\')) ||
                    ((path[0] == '\\' || path[0] == '/') && path[1] == path[0] && path[2]));
}

static inline wchar_t *beam_windows_volume_name(const wchar_t *native, const wchar_t *root)
{
    HANDLE volume = CreateFileW(root, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (volume == INVALID_HANDLE_VALUE) return NULL;
    DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_NT;
    DWORD size = GetFinalPathNameByHandleW(volume, NULL, 0, flags);
    wchar_t *name = size ? malloc(((size_t)size+2)*sizeof(*name)) : NULL;
    DWORD length = name ? GetFinalPathNameByHandleW(volume, name, size+1, flags) : 0;
    CloseHandle(volume);
    if (!length || length > size) { free(name); return NULL; }
    if (name[length-1] != L'\\') name[length++] = L'\\';
    name[length] = 0;
    /* Both identities came from opened handles. An input junction or mount
     * cannot substitute a different volume or directory through lexical paths. */
    if (wcsncmp(native, name, length)) { free(name); return NULL; }
    size_t prefix = wcslen(root), remaining = wcslen(native+length);
    wchar_t *result = malloc((prefix+remaining+2)*sizeof(*result));
    if (result) {
        wcscpy(result, root);
        if (prefix && result[prefix-1] != L'\\') result[prefix++] = L'\\';
        wcscpy(result+prefix, native+length);
    }
    free(name);
    return result;
}

static inline wchar_t *beam_windows_final_name(HANDLE file, const wchar_t *input)
{
    DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    DWORD size = GetFinalPathNameByHandleW(file, NULL, 0, flags);
    if (size) {
        wchar_t *name = malloc(((size_t)size+1)*sizeof(*name));
        if (!name) return NULL;
        DWORD length = GetFinalPathNameByHandleW(file, name, size+1, flags);
        if (length && length <= size) return name;
        free(name); return NULL;
    }
    /* AppContainer tokens can open an admitted file while DOS-volume namespace
     * queries are denied. Resolve its actual NT name and verify a volume-root
     * handle before expressing that same object with a Win32 absolute path. */
    flags = FILE_NAME_NORMALIZED | VOLUME_NAME_NT;
    size = GetFinalPathNameByHandleW(file, NULL, 0, flags);
    wchar_t *native = size ? malloc(((size_t)size+1)*sizeof(*native)) : NULL;
    if (!native) return NULL;
    DWORD length = GetFinalPathNameByHandleW(file, native, size+1, flags);
    if (!length || length > size) { free(native); return NULL; }
    wchar_t *root = malloc(32768*sizeof(*root)), *result = NULL;
    if (root && GetVolumePathNameW(input, root, 32768))
        result = beam_windows_volume_name(native, root);
    free(root);
    for (wchar_t drive = L'A'; !result && drive <= L'Z'; ++drive) {
        wchar_t candidate[] = {drive, L':', L'\\', 0};
        result = beam_windows_volume_name(native, candidate);
    }
    free(native);
    return result;
}

static inline char *beam_realpath(const char *path)
{
    wchar_t *wide = beam_windows_path(path), *canonical;
    if (!wide) return NULL;
    HANDLE file = CreateFileW(wide, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(wide); errno = ENOENT; return NULL; }
    canonical = beam_windows_final_name(file, wide);
    free(wide);
    CloseHandle(file);
    if (!canonical) { errno = EIO; return NULL; }
    /* Keep the host's canonical UTF-8 spelling independent of Win32's device
     * prefix. Canonicalization still follows the opened file, including links. */
    wchar_t *name = canonical;
    if (!wcsncmp(name, L"\\\\?\\UNC\\", 8)) {
        name += 6;
        name[0] = L'\\';
    } else if (!wcsncmp(name, L"\\\\?\\", 4)) name += 4;
    char *result = beam_windows_utf8(name);
    if (result) for (char *p = result; *p; ++p) if (*p == '\\') *p = '/';
    free(canonical);
    return result;
}

static inline void *beam_dlopen(const char *path)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return NULL;
    HMODULE module = beam_windows_load_library(wide,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    DWORD error = GetLastError();
    free(wide);
    SetLastError(error);
    return (void *)module;
}
static inline void *beam_dlsym(void *module, const char *name)
{ return (void *)GetProcAddress((HMODULE)module, name); }
static inline int beam_dlclose(void *module) { return FreeLibrary((HMODULE)module) ? 0 : -1; }
static inline const char *beam_dlerror(void)
{
#if defined(_MSC_VER)
    static __declspec(thread) char message[96];
#else
    static __thread char message[96];
#endif
    snprintf(message, sizeof(message), "Windows loader error %lu", (unsigned long)GetLastError());
    return message;
}
static inline char *beam_image_path(const void *address)
{
    HMODULE module;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &module))
        return NULL;
    wchar_t *wide = malloc(32768 * sizeof(*wide));
    if (!wide) return NULL;
    DWORD size = GetModuleFileNameW(module, wide, 32768);
    if (!size || size == 32768) { free(wide); errno = ENAMETOOLONG; return NULL; }
    char *raw = beam_windows_utf8(wide);
    free(wide);
    if (!raw) return NULL;
    char *result = beam_realpath(raw);
    free(raw);
    return result;
}
#else
#include <dlfcn.h>
#include <pthread.h>
#define beam_thread_t pthread_t
#define beam_thread_self pthread_self
#define beam_thread_equal pthread_equal
#define beam_mutex_t pthread_mutex_t
#define beam_mutex_init pthread_mutex_init
#define beam_mutex_destroy pthread_mutex_destroy
#define beam_mutex_lock pthread_mutex_lock
#define beam_mutex_unlock pthread_mutex_unlock
#define BEAM_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define beam_cond_t pthread_cond_t
#define beam_cond_init pthread_cond_init
#define beam_cond_destroy pthread_cond_destroy
#define beam_cond_signal pthread_cond_signal
#define beam_cond_wait pthread_cond_wait
#define beam_dlopen(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define beam_dlsym dlsym
#define beam_dlclose dlclose
#define beam_dlerror dlerror
#define beam_realpath(path) realpath(path, NULL)
static inline unsigned beam_reference_add(unsigned *value, int delta)
{ return __atomic_add_fetch(value, delta, __ATOMIC_ACQ_REL); }
static inline int beam_path_absolute(const char *path) { return path && path[0] == '/'; }
#endif
#endif
