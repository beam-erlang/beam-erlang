/* SPDX-License-Identifier: Apache-2.0 */
/* Private build host: an explicit compiler inventory in a bare VM. */
#include "beam_compiler_host.h"
#include <stdio.h>

#ifndef BEAM_BUILD_COMPILER_ROOT
#error "The build compiler requires an explicit module root"
#endif

int main(int argc, char **argv)
{
    beam_system *system = NULL;
    beam_error error = {0};
    beam_status status = beam_init(&system, &error);
    int result;

    if (status != BEAM_OK) {
        fprintf(stderr, "beam build compiler: %s: %s\n",
                beam_status_message(status), error.message);
        return 1;
    }
    result = beam_compiler_execute(system, BEAM_BUILD_COMPILER_ROOT,
                                   NULL, argc - 1, argv + 1, NULL);
    status = beam_terminate(&system);
    if (status != BEAM_OK) {
        fprintf(stderr, "beam build compiler: teardown failed: %s\n",
                beam_status_message(status));
        result = 1;
    }
    return result;
}
