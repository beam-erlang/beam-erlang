/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/runtime.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_error error = {0};
    if (beam_init(&system, &error) != BEAM_OK) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    beam_terminate(&system);
    if (argc == 2) {
        char path[32768];
        size_t required;
        if (strcmp(beam_build_id(), argv[1]) ||
            beam_library_path(path, sizeof path, &required, &error) != BEAM_OK)
            return 2;
        puts(path);
    } else if (argc != 1) {
        return 2;
    }
    puts("Runtime SDK only");
    return 0;
}
