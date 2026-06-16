/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_SOCKET_BACKEND_H
#define BEAM_SOCKET_BACKEND_H

#include "erl_nif.h"

/* The two NIF entry libraries retain one shared socket implementation. */
ERL_NAPI_EXPORT ErlNifEntry *beam_prim_socket_nif_init(ERL_NIF_INIT_ARGS);
ERL_NAPI_EXPORT ErlNifEntry *beam_prim_net_nif_init(ERL_NIF_INIT_ARGS);

#endif
