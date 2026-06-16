/* SPDX-License-Identifier: Apache-2.0 */
#include <beam.h>
#include <stdio.h>

int main(void)
{
    beam_system *system = NULL;
    beam_error error = {0};
    if (beam_init(&system, &error) != BEAM_OK) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    beam_terminate(&system);
    puts("Runtime SDK only");
    return 0;
}
