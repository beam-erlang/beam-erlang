/* Ordinary native command fixtures for the retained port_SUITE commands.
 * huge_env lists the working directory, pipe_limit_env runs true, and
 * unregister_name needs a live "sleep 5" process. Keep these private: no shell,
 * compatibility runtime, candidate fallback, or fabricated command output.
 * The supported ls subset is an ordinary listing with path operands and --;
 * unsupported options fail. sleep accepts one finite nonnegative seconds value.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <io.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int output_text(FILE *stream, const wchar_t *value)
{
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                    NULL, 0, NULL, NULL);
    char *text;
    int result;
    if (!count || !(text = malloc((size_t) count))) return 0;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                             text, count, NULL, NULL)) {
        free(text);
        return 0;
    }
    result = fwrite(text, 1, (size_t) count - 1, stream) == (size_t) count - 1;
    free(text);
    return result;
}

static int listing_error(const wchar_t *path, DWORD error)
{
    fputs("ls: cannot access '", stderr);
    output_text(stderr, path);
    fprintf(stderr, "' (Windows error %lu)\n", (unsigned long) error);
    return 2;
}

static int name_order(const void *left, const void *right)
{
    return wcscmp(*(const wchar_t *const *) left,
                  *(const wchar_t *const *) right);
}

static int list_path(const wchar_t *path, int header)
{
    DWORD attributes = GetFileAttributesW(path), error;
    WIN32_FIND_DATAW entry;
    HANDLE search;
    wchar_t *pattern, **names = NULL;
    size_t length, count = 0, capacity = 0, index;
    int result = 0;
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return listing_error(path, GetLastError());
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return output_text(stdout, path) && fputc('\n', stdout) != EOF ? 0 : 2;
    if (header && !(output_text(stdout, path) && fputs(":\n", stdout) >= 0))
        return 2;
    length = wcslen(path);
    pattern = malloc((length + 3) * sizeof(*pattern));
    if (!pattern) return listing_error(path, ERROR_NOT_ENOUGH_MEMORY);
    memcpy(pattern, path, length * sizeof(*pattern));
    if (length && path[length - 1] != L'\\' && path[length - 1] != L'/')
        pattern[length++] = L'\\';
    pattern[length++] = L'*';
    pattern[length] = L'\0';
    search = FindFirstFileW(pattern, &entry);
    free(pattern);
    if (search == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? 0 : listing_error(path, error);
    }
    do {
        /* ls's default hidden-name rule is the leading dot, not Win32's
         * hidden attribute. This also omits the synthetic . and .. entries. */
        if (entry.cFileName[0] == L'.') continue;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            wchar_t **grown = realloc(names, next * sizeof(*grown));
            if (!grown) {
                result = listing_error(path, ERROR_NOT_ENOUGH_MEMORY);
                break;
            }
            names = grown;
            capacity = next;
        }
        names[count] = _wcsdup(entry.cFileName);
        if (!names[count]) {
            result = listing_error(path, ERROR_NOT_ENOUGH_MEMORY);
            break;
        }
        ++count;
    } while (FindNextFileW(search, &entry));
    error = GetLastError();
    FindClose(search);
    if (!result && error != ERROR_NO_MORE_FILES)
        result = listing_error(path, error);
    if (!result) {
        if (count > 1) qsort(names, count, sizeof(*names), name_order);
        for (index = 0; index < count; ++index) {
            if (!output_text(stdout, names[index]) || fputc('\n', stdout) == EOF) {
                result = 2;
                break;
            }
        }
    }
    for (index = 0; index < count; ++index) free(names[index]);
    free(names);
    return result;
}

static int list_command(int argc, wchar_t **argv)
{
    int index = 1, result = 0, status, operands;
    if (index < argc && !wcscmp(argv[index], L"--")) ++index;
    else if (index < argc && argv[index][0] == L'-') {
        fputs("ls: unsupported option\n", stderr);
        return 2;
    }
    operands = argc - index;
    if (!operands) result = list_path(L".", 0);
    else {
        for (; index < argc; ++index) {
            status = list_path(argv[index], operands > 1);
            if (status) result = status;
        }
    }
    if (fflush(stdout) == EOF) result = 2;
    return result;
}

static int sleep_command(int argc, wchar_t **argv)
{
    wchar_t *end;
    double seconds, milliseconds;
    ULONGLONG duration, started, elapsed;
    if (argc != 2 || !*argv[1]) goto invalid;
    errno = 0;
    seconds = wcstod(argv[1], &end);
    if (errno == ERANGE || end == argv[1] || *end || !_finite(seconds) || seconds < 0)
        goto invalid;
    milliseconds = ceil(seconds * 1000.0);
    /* A duration at or above 2**64 cannot be represented by the native clock. */
    if (!_finite(milliseconds) || milliseconds >= 18446744073709551616.0)
        goto invalid;
    duration = (ULONGLONG) milliseconds;
    started = GetTickCount64();
    for (elapsed = 0; elapsed < duration; elapsed = GetTickCount64() - started) {
        ULONGLONG remaining = duration - elapsed;
        Sleep(remaining >= (ULONGLONG) INFINITE ? INFINITE - 1 : (DWORD) remaining);
    }
    return 0;
invalid:
    fputs("sleep: expected one finite nonnegative number of seconds\n", stderr);
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *name = argv[0], *cursor;
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    for (cursor = name; *cursor; ++cursor)
        if (*cursor == L'\\' || *cursor == L'/') name = cursor + 1;
    /* The true utility deliberately ignores operands and always succeeds. */
    if (!_wcsicmp(name, L"true.exe") || !_wcsicmp(name, L"true")) return 0;
    if (!_wcsicmp(name, L"ls.exe") || !_wcsicmp(name, L"ls"))
        return list_command(argc, argv);
    if (!_wcsicmp(name, L"sleep.exe") || !_wcsicmp(name, L"sleep"))
        return sleep_command(argc, argv);
    fputs("Private command fixture must be named true, ls, or sleep\n", stderr);
    return 2;
}
