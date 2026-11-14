/* SPDX-License-Identifier: Apache-2.0 */
/* Independent hosts own their CRT entry. Pass UTF-8 to the public runtime
 * entry without depending on a producer executable or its launcher sources. */
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int wmain(int argc, wchar_t **wide_argv)
{
    char **argv = (char **)calloc((size_t)argc + 1, sizeof(*argv));
    int index, status;
    if (!argv) return 1;
    for (index = 0; index < argc; ++index) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       wide_argv[index], -1, NULL, 0, NULL, NULL);
        if (!bytes || !(argv[index] = (char *)malloc((size_t)bytes)) ||
            !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                wide_argv[index], -1, argv[index], bytes, NULL, NULL)) {
            fprintf(stderr, "Cannot encode SDK host argument %d as UTF-8\n", index);
            while (index >= 0) free(argv[index--]);
            free(argv);
            return 1;
        }
    }
    status = beam_main(argc, argv);
    for (index = 0; index < argc; ++index) free(argv[index]);
    free(argv);
    return status;
}
#else
int main(int argc, char **argv)
{
    return beam_main(argc, argv);
}
#endif
