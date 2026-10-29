/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/runtime.h>
#include <beam/zlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const unsigned char input[] = "runtime-only codec";
    unsigned char packed[128], plain[sizeof(input)];
    size_t packed_size = sizeof(packed), plain_size = sizeof(plain);
    if (beam_zlib_compress_z(packed, &packed_size, input, sizeof(input)) != BEAM_ZLIB_OK ||
        beam_zlib_uncompress_z(plain, &plain_size, packed, packed_size) != BEAM_ZLIB_OK ||
        plain_size != sizeof(input) || memcmp(plain, input, sizeof(input))) return 3;
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
