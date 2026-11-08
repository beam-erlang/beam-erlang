/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_nif.h>
#include <erl_driver.h>
#ifndef NATIVE_ANSWER
#define NATIVE_ANSWER 19
#endif
static ERL_NIF_TERM value(ErlNifEnv *env, int count, const ERL_NIF_TERM arguments[])
{
    (void)count; (void)arguments;
    ErlNifEnv *owned = enif_alloc_env();
    if (!owned) return enif_make_badarg(env);
    enif_clear_env(owned);
    ERL_NIF_TERM result = enif_make_copy(env, enif_make_int(owned, NATIVE_ANSWER));
    enif_free_env(owned);
    return result;
}
static ErlNifFunc functions[] = {{"value",0,value,0}};
ERL_NIF_INIT(pkg_acceptance,functions,NULL,NULL,NULL,NULL)
