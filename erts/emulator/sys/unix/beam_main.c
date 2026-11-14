/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <beam/beam.h>
#include "sys.h"
#include "erl_vm.h"
#include "global.h"

/* Keep the public process entry beside upstream's Unix startup code: the
 * signal stack must be initialized before any scheduler thread can start.
 */
int beam_main(int argc, char **argv)
{
    sys_init_signal_stack();
    erl_start(argc, argv);
    return 0;
}
