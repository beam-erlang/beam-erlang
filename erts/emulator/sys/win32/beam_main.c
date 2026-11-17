/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <windows.h>

void erl_start(int argc, char **argv);
void sys_primitive_init(HMODULE beam);
int sys_get_key(int fd);

/* The installed runtime can be linked into an executable or a DLL. Canonical
 * preload resources belong to that module, which need not be the process EXE.
 * Arguments are already encoded as UTF-8.
 */
int beam_main(int argc, char **argv)
{
    HMODULE module = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR) (const void *) &beam_main, &module)) {
        return 1;
    }
    sys_primitive_init(module);
    erl_start(argc, argv);
    return 0;
}

int beam_windows_wait_for_console_key(void)
{
    return sys_get_key(0);
}
