/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <stdio.h>

int main(void)
{
    beam_system *system = NULL;
    beam_error error = {0};
    char library[32768];
    size_t required;
    if (beam_init(&system, &error) != BEAM_OK) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    beam_status status = beam_library_path(library, sizeof(library), &required, &error);
    beam_status stopped = beam_terminate(&system);
    if (status != BEAM_OK || stopped != BEAM_OK) return 1;
    printf("WINDOWS_DEPLOYMENT_OK\n%s\n", library);
    return 0;
}
