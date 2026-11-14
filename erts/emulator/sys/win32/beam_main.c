/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <windows.h>

void erl_start(int argc, char **argv);
void sys_primitive_init(HMODULE beam);

/* Every static host embeds the preload resources in its own executable.
 * Match the standalone console VM initialization without the upstream DLL
 * entry or its separate launcher. Arguments are already encoded as UTF-8.
 */
int beam_main(int argc, char **argv)
{
    sys_primitive_init(GetModuleHandle(NULL));
    erl_start(argc, argv);
    return 0;
}
