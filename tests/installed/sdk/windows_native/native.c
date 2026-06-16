/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_nif.h>
#include <erl_driver.h>
#ifndef NATIVE_ANSWER
#define NATIVE_ANSWER 19
#endif
static ERL_NIF_TERM value(ErlNifEnv *env, int count, const ERL_NIF_TERM arguments[])
{
    (void)count; (void)arguments;
    return enif_make_int(env,NATIVE_ANSWER);
}
static ErlNifFunc functions[] = {{"value",0,value,0}};
ERL_NIF_INIT(pkg_acceptance,functions,NULL,NULL,NULL,NULL)
