/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_WINDOWS_ENTRY_H
#define BEAM_WINDOWS_ENTRY_H
#ifdef _WIN32
#include "beam_platform.h"
#include <io.h>
#include <fcntl.h>
static int beam_utf8_main(int argc, char **argv);
int wmain(int argc, wchar_t **wide)
{
    char **owned = calloc(2*((size_t)argc + 1), sizeof(*owned));
    char **argv = owned ? owned + argc + 1 : NULL;
    int result = 1;
    if (!argv) return 1;
    for (int i = 0; i < argc; ++i) {
        argv[i] = owned[i] = beam_windows_utf8(wide[i]);
        if (!argv[i]) goto done;
    }
    /* Command-owned CRT streams preserve bytes on redirected input/output. */
    for (int i = 0; i < 3; ++i) if (_get_osfhandle(i) != -1 && _setmode(i, _O_BINARY) == -1) goto done;
    result = beam_utf8_main(argc, argv);
done:
    for (int i = 0; i < argc; ++i) free(owned[i]);
    free(owned); return result;
}
#define main beam_utf8_main
#endif
#endif
