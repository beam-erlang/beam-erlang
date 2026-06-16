/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_COMPILER_FILES_H
#define BEAM_COMPILER_FILES_H
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <limits.h>
#include "../../beam_platform.h"
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <winioctl.h>
extern void _dosmaperr(DWORD);
#ifndef PATH_MAX
#define PATH_MAX 32768
#endif
#ifndef SSIZE_MAX
/* The Microsoft CRT read/write calls accept at most INT_MAX bytes. */
#define SSIZE_MAX INT_MAX
#endif
#ifndef ELOOP
#define ELOOP 114
#endif
#ifndef X_OK
#define X_OK 0
#endif
typedef struct _stat64 beam_host_stat;
typedef intptr_t ssize_t;
#define O_CLOEXEC _O_NOINHERIT
#define O_NONBLOCK 0
#define R_OK 4
#define W_OK 2
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define S_ISCHR(mode) (((mode) & _S_IFMT) == _S_IFCHR)
#define S_ISBLK(mode) 0
#define S_ISLNK(mode) (((mode) & _S_IFMT) == 0xa000)
#define beam_host_fstat _fstat64
#define beam_host_read _read
#define beam_host_write _write
#define beam_host_close _close
#define beam_host_pid _getpid
static inline int beam_host_open(const char *path, int flags, ...)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _wopen(wide, flags | _O_BINARY, _S_IREAD | _S_IWRITE);
    free(wide);
    return result;
}
static inline int beam_host_stat_path(const char *path, beam_host_stat *info)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _wstat64(wide, info);
    free(wide);
    return result;
}
static inline int beam_host_access(const char *path, int mode)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _waccess(wide, mode);
    free(wide);
    return result;
}
static inline int beam_host_mkdir(const char *path, int mode)
{
    (void)mode;
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _wmkdir(wide);
    free(wide);
    return result;
}
static inline int beam_host_unlink(const char *path)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _wunlink(wide);
    free(wide);
    return result;
}
static inline int beam_host_rename(const char *from, const char *to)
{
    wchar_t *source = beam_windows_path(from), *target = beam_windows_path(to);
    if (!source || !target) { free(source); free(target); return -1; }
    BOOL result = MoveFileExW(source, target, MOVEFILE_REPLACE_EXISTING);
    DWORD error = GetLastError();
    free(source); free(target);
    if (!result) { _dosmaperr(error); return -1; }
    return 0;
}
static inline char *beam_host_cwd(void)
{
    DWORD size = GetCurrentDirectoryW(0, NULL);
    wchar_t *wide = size ? malloc((size_t)size * sizeof(*wide)) : NULL;
    if (!wide) { errno = size ? ENOMEM : EIO; return NULL; }
    DWORD length = GetCurrentDirectoryW(size, wide);
    if (!length || length >= size) { free(wide); errno = EIO; return NULL; }
    char *result = beam_windows_utf8(wide);
    free(wide);
    return result;
}
static inline char *beam_host_realpath(const char *path, char *out)
{
    char *canonical = beam_realpath(path);
    if (!canonical) return NULL;
    if (strlen(canonical) >= PATH_MAX) { free(canonical); errno = ENAMETOOLONG; return NULL; }
    strcpy(out, canonical); free(canonical); return out;
}
static inline int beam_host_chdir(const char *path)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    int result = _wchdir(wide); free(wide); return result;
}
static inline FILE *beam_host_fopen(const char *path, const char *mode)
{
    wchar_t *wide = beam_windows_path(path), *flags = beam_windows_path(mode);
    if (!wide || !flags) { free(wide); free(flags); return NULL; }
    FILE *result = _wfopen(wide, flags); free(wide); free(flags); return result;
}
static inline struct tm *beam_host_localtime(const time_t *value, struct tm *out)
{ return localtime_s(out, value) ? NULL : out; }
static inline char *beam_host_environment(const char *name)
{
    wchar_t *wide = beam_windows_path(name);
    if (!wide) return NULL;
    const wchar_t *value = _wgetenv(wide);
    free(wide);
    return value ? beam_windows_utf8(value) : NULL;
}
static inline ssize_t beam_host_readlink(const char *path, char *out, size_t capacity)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    HANDLE file = CreateFileW(wide, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (file == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
    union { unsigned char bytes[MAXIMUM_REPARSE_DATA_BUFFER_SIZE]; DWORD alignment; } data;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(file, FSCTL_GET_REPARSE_POINT, NULL, 0,
                             data.bytes, sizeof(data.bytes), &returned, NULL);
    CloseHandle(file);
    if (!ok || returned < 16) { errno = EINVAL; return -1; }
    DWORD tag; USHORT offset, length;
    memcpy(&tag, data.bytes, sizeof(tag));
    size_t prefix = tag == IO_REPARSE_TAG_SYMLINK ? 20 : tag == IO_REPARSE_TAG_MOUNT_POINT ? 16 : 0;
    if (!prefix || returned < prefix) { errno = EINVAL; return -1; }
    /* Prefer the user-facing print name; retain a substitute-only target too. */
    memcpy(&offset, data.bytes + 12, 2); memcpy(&length, data.bytes + 14, 2);
    if (!length) { memcpy(&offset, data.bytes + 8, 2); memcpy(&length, data.bytes + 10, 2); }
    if ((offset | length) & 1 || prefix + offset + length > returned) { errno = EIO; return -1; }
    wchar_t *target = malloc((size_t)length + sizeof(wchar_t));
    if (!target) return -1;
    memcpy(target, data.bytes + prefix + offset, length); target[length / 2] = 0;
    char *text = beam_windows_utf8(target);
    free(target);
    if (!text) return -1;
    size_t size = strlen(text);
    if (size > capacity) size = capacity;
    memcpy(out, text, size); free(text);
    return (ssize_t)size;
}
static inline time_t beam_host_filetime(FILETIME value)
{
    ULARGE_INTEGER time;
    time.LowPart = value.dwLowDateTime; time.HighPart = value.dwHighDateTime;
    return (time_t)(time.QuadPart / 10000000 - 11644473600ULL);
}
static inline int beam_host_lstat(const char *path, beam_host_stat *info)
{
    wchar_t *wide = beam_windows_path(path);
    if (!wide) return -1;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(wide, &data);
    free(wide);
    if (search == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
    FindClose(search);
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (data.dwReserved0 != IO_REPARSE_TAG_SYMLINK && data.dwReserved0 != IO_REPARSE_TAG_MOUNT_POINT))
        return beam_host_stat_path(path, info);
    memset(info, 0, sizeof(*info));
    info->st_mode = 0xa000 | _S_IREAD;
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) info->st_mode |= _S_IWRITE;
    info->st_nlink = 1;
    info->st_size = ((__int64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    info->st_atime = beam_host_filetime(data.ftLastAccessTime);
    info->st_mtime = beam_host_filetime(data.ftLastWriteTime);
    info->st_ctime = beam_host_filetime(data.ftCreationTime);
    return 0;
}
#else
#include <unistd.h>
typedef struct stat beam_host_stat;
#define beam_host_open open
#define beam_host_fstat fstat
#define beam_host_read read
#define beam_host_write write
#define beam_host_close close
#define beam_host_stat_path stat
#define beam_host_lstat lstat
#define beam_host_access access
#define beam_host_mkdir mkdir
#define beam_host_realpath realpath
#define beam_host_chdir chdir
#define beam_host_fopen fopen
#define beam_host_unlink unlink
#define beam_host_rename rename
#define beam_host_readlink readlink
#define beam_host_localtime localtime_r
#define beam_host_pid getpid
#define beam_host_cwd() getcwd(NULL, 0)
static inline char *beam_host_environment(const char *name)
{ const char *value = getenv(name); return value ? strdup(value) : NULL; }
#endif
#endif
