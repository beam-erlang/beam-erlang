/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_ENIF_INTERNAL_H
#define BEAM_ENIF_INTERNAL_H

#include "erl_nif.h"

ERL_NIF_TERM erts_nif_vmake_list(ErlNifEnv *env, unsigned count, va_list args);
ERL_NIF_TERM erts_nif_vmake_tuple(ErlNifEnv *env, unsigned count, va_list args);

#endif
