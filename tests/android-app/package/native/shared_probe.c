/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_nif.h>
static ERL_NIF_TERM value(ErlNifEnv *env,int argc,const ERL_NIF_TERM argv[]) {
    (void)argc;(void)argv;return enif_make_int(env,77);
}
static ErlNifFunc functions[]={{"value",0,value,0}};
ERL_NIF_INIT(android_shared,functions,NULL,NULL,NULL,NULL)
