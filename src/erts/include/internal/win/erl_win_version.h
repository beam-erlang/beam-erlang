/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ERL_WIN_VERSION_H
#define ERL_WIN_VERSION_H
#include <windows.h>

/* An embedding executable's compatibility manifest must not change the
 * reported kernel version or prevent admission of otherwise supported APIs. */
static __inline int erts_get_windows_version(OSVERSIONINFOW *version)
{
    typedef LONG (WINAPI *get_version_t)(OSVERSIONINFOW *);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    get_version_t get_version = ntdll ? (get_version_t)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    if (!get_version) return 0;
    ZeroMemory(version, sizeof(*version));
    version->dwOSVersionInfoSize = sizeof(*version);
    return get_version(version) == 0;
}
#endif
