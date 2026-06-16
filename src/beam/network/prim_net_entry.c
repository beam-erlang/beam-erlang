/* SPDX-License-Identifier: Apache-2.0 */
#include "socket_backend.h"

ERL_NAPI_EXPORT ErlNifEntry *nif_init(void)
{
    return beam_prim_net_nif_init();
}
