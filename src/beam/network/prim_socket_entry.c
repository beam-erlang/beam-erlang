/* SPDX-License-Identifier: Apache-2.0 */
#include "socket_backend.h"

ERL_NAPI_EXPORT ErlNifEntry *nif_init(ERL_NIF_INIT_ARGS)
{
    return beam_prim_socket_nif_init(
#ifdef _WIN32
        callbacks
#endif
    );
}
