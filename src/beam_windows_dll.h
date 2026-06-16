/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_WINDOWS_DLL_H
#define BEAM_WINDOWS_DLL_H
#include <windows.h>

/* Malformed/unavailable native inputs must return an error, never display a
 * blocking loader dialog. Preserve the embedding thread's error policy. */
static inline HMODULE beam_windows_load_library(const wchar_t *path, DWORD flags)
{
    DWORD previous = GetThreadErrorMode();
    if (!SetThreadErrorMode(previous | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, NULL))
        return NULL;
    HMODULE module = LoadLibraryExW(path, NULL, flags);
    DWORD error = GetLastError();
    if (!SetThreadErrorMode(previous, NULL)) {
        error = GetLastError();
        if (module) FreeLibrary(module);
        module = NULL;
    }
    SetLastError(error);
    return module;
}
#endif
