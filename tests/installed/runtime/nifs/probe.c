#include <erl_nif.h>
#include <beam/native.h>
#include <stdio.h>
#include <stdlib.h>
#include "packet_probe.h"
static ErlNifResourceType *resource;
static void event(char c) {
    const char *path = getenv("NIF_ACCEPTANCE_LOG");
    if (path) { FILE *f=fopen(path,"a"); if (!f) abort(); fputc(c,f); fclose(f); }
}
static void destroy(ErlNifEnv *env, void *value) { (void)env; (void)value; event('D'); }
static int load(ErlNifEnv *env, void **private, ERL_NIF_TERM info) {
    if (!enif_is_identical(info,enif_make_atom(env,"undefined"))) return 1;
#ifdef FAIL_LOAD
    return 1;
#endif
    *private=enif_alloc(32); if (!*private) return 1;
    resource=enif_open_resource_type(env,NULL,"probe_resource",destroy,ERL_NIF_RT_CREATE,NULL);
    if (!resource) { enif_free(*private); return 1; }
    event('L'); return 0;
}
static void unload(ErlNifEnv *env, void *private) { (void)env; enif_free(private); event('U'); }
static ERL_NIF_TERM hold(ErlNifEnv *env,int n,const ERL_NIF_TERM a[]) {
    (void)n; (void)a;
    ErlNifPid self, copied;
    if (!enif_self(env,&self) ||
        !enif_get_local_pid(NULL,enif_make_pid(env,&self),&copied) ||
        !enif_is_identical(enif_make_pid(env,&self),enif_make_pid(env,&copied)) ||
        enif_get_local_pid(NULL,enif_make_atom(env,"undefined"),&copied) ||
        enif_get_local_pid(NULL,enif_make_list(env,0),&copied))
        return enif_make_badarg(env);
    void *value=enif_alloc_resource(resource,1); ERL_NIF_TERM result=enif_make_resource(env,value);
    enif_release_resource(value); return result;
}
static ERL_NIF_TERM fatal(ErlNifEnv *env,int n,const ERL_NIF_TERM a[]) {
    int mode;
    if (n != 1 || !enif_get_int(env, a[0], &mode)) return enif_make_badarg(env);
    beam_native_fail((beam_native_failure_mode)mode,
                     "public-native-failure mode=%d term=%T\n",
                     mode, enif_make_atom(env, "native_failure_marker"));
}
#ifdef BAD_FUNCTION
static ErlNifFunc functions[]={{"absent",0,hold,0}};
#else
static ErlNifFunc functions[]={{"hold",0,hold,0},{"fatal",1,fatal,0},
                              {"packet",6,packet,0},{"packet_invalid",0,packet_invalid,0}};
#endif
#ifdef BAD_ABI
#undef ERL_NIF_MAJOR_VERSION
#define ERL_NIF_MAJOR_VERSION 999
#endif
#ifdef BAD_MODULE
ERL_NIF_INIT(wrong,functions,load,NULL,NULL,unload)
#else
ERL_NIF_INIT(probe,functions,load,NULL,NULL,unload)
#endif
