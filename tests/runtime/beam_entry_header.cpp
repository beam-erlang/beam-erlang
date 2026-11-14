/* The public process entry is the first include, without VM configuration. */
#include <beam/beam.h>

int (*beam_public_process_entry(void))(int, char **)
{
    return beam_main;
}
