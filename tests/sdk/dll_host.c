/* SPDX-License-Identifier: Apache-2.0 */
#include <windows.h>
#include <stdio.h>

static int beam_main(int argc, char **argv)
{
    wchar_t path[32768];
    wchar_t *last;
    HMODULE module;
    int (*entry)(int, char **);
    DWORD count = GetModuleFileNameW(NULL, path, 32768);
    if (!count || count >= 32768) return 1;
    last = wcsrchr(path, L'\\');
    if (!last || (size_t) (last - path) + 22 >= 32768) return 1;
    wcscpy(last + 1, L"beam_sdk_runtime.dll");
    module = LoadLibraryW(path);
    if (!module) { fprintf(stderr, "Cannot load SDK VM DLL (%lu)\n", GetLastError()); return 1; }
    entry = (int (*)(int, char **)) GetProcAddress(module, "beam_main");
    if (!entry || !GetProcAddress(module, "beam_windows_wait_for_console_key")) return 1;
    return entry(argc, argv);
}

#include "host_main.h"
