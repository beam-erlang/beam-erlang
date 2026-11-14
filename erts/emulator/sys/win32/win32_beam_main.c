/*
 * Console entry for the Windows beam executable.
 *
 * Upstream OTP loads the emulator as a DLL from a separate launcher, which
 * calls sys_primitive_init with that module handle. This product boots
 * beam.exe directly, so the entry passes the executable's own module.
 * sys/win32/erl_main.c is the DLL entry and is not linked here.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

void erl_start(int argc, char **argv);
void sys_primitive_init(HMODULE beam);

int wmain(int argc, wchar_t **wide_argv)
{
    char **argv;
    int index;
    sys_primitive_init(GetModuleHandle(NULL));
    /* The VM takes UTF-8 arguments, as in upstream's direct erlexec entry.
     * An ANSI CRT main would lose characters outside the active code page. */
    argv = calloc((size_t) argc + 1, sizeof(*argv));
    if (!argv) return 1;
    for (index = 0; index < argc; ++index) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        wide_argv[index], -1, NULL, 0, NULL, NULL);
        if (!bytes || !(argv[index] = malloc((size_t) bytes)) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                 wide_argv[index], -1, argv[index], bytes, NULL, NULL)) {
            fprintf(stderr, "Cannot encode Windows argument %d as UTF-8\n", index);
            while (index >= 0) free(argv[index--]);
            free(argv);
            return 1;
        }
    }
    erl_start(argc, argv);
    for (index = 0; index < argc; ++index) free(argv[index]);
    free(argv);
    return 0;
}
