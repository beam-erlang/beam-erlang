/* SPDX-License-Identifier: Apache-2.0 */
#include "support.h"

int main(int argc, char **argv)
{
    const char *mode, *root, *fixture, *work;
    if (argc < 3)
        fail("usage: returning_acceptance <mode> <prefix> [fixture] [work]");
    mode = argv[1];
    root = argv[2];
    fixture = argc > 3 ? argv[3] : root;
    work = argc > 4 ? argv[4] : fixture;
    {
        char loaded[4096], expected[4096], canonical[4096];
        size_t required = 0;
        beam_error error = {0};
        snprintf(expected, sizeof expected, "%s/lib/libbeam.so", root);
        check(beam_library_path(loaded, sizeof loaded, &required, &error) == BEAM_OK, error.message);
        check(realpath(expected, canonical) != NULL && !strcmp(loaded, canonical), "host uses its deployed libbeam");
    }
    if (strcmp(mode, "pin") == 0)
        pin_mode(root, fixture);
    else if (strcmp(mode, "components") == 0)
        components_mode(root, fixture);
    else if (strcmp(mode, "library") == 0)
        library_mode(root, fixture);
    else if (strcmp(mode, "completion") == 0)
        completion_mode(root, fixture);
    else if (strcmp(mode, "greeting") == 0)
        greeting_mode(root, fixture);
    else if (strcmp(mode, "counter") == 0)
        counter_mode(root, fixture);
    else if (strcmp(mode, "code") == 0)
        code_mode(root, fixture);
    else if (strcmp(mode, "file") == 0)
        file_mode(root, fixture, work);
    else if (strcmp(mode, "echo") == 0)
        echo_mode(root, fixture);
    else if (strcmp(mode, "record") == 0)
        record_mode(root, fixture, work);
    else if (strcmp(mode, "zlib") == 0)
        zlib_mode(root, fixture);
    else if (strcmp(mode, "output") == 0)
        output_mode(root, fixture);
    else
        fail("unknown mode");
    return 0;
}
