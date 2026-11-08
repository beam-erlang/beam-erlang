#include <erl_nif.h>
#include <beam/native.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "packet_probe.h"
static ErlNifResourceType *resource;
#ifdef STATIC_ERLANG_NIF
int probe_report_cleanup, probe_reclaimed;
#endif
static void event(char c) {
    const char *path = getenv("NIF_ACCEPTANCE_LOG");
    if (path) { FILE *f=fopen(path,"a"); if (!f) abort(); fputc(c,f); fclose(f); }
}
static void destroy(ErlNifEnv *env, void *value) {
    (void)env; (void)value; event('D');
#ifdef STATIC_ERLANG_NIF
    if (probe_report_cleanup) {
        if (!beam_native_stop_requested()) abort();
        beam_native_report_cleanup(41, "injected destructor diagnostic");
        probe_reclaimed = 1;
    }
#endif
}
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
static void unload(ErlNifEnv *env, void *private) {
    (void)env; enif_free(private); event('U');
#ifdef STATIC_ERLANG_NIF
    if (probe_report_cleanup) {
        if (probe_reclaimed != 1 || !beam_native_stop_requested()) abort();
        beam_native_report_cleanup(42, "later unload diagnostic");
        probe_reclaimed = 2;
    }
#endif
}
static ERL_NIF_TERM hold(ErlNifEnv *env,int n,const ERL_NIF_TERM a[]) {
    (void)n; (void)a;
    ErlNifPid self, copied;
    if (!enif_self(env,&self) ||
        !enif_get_local_pid(NULL,enif_make_pid(env,&self),&copied) ||
        !enif_is_identical(enif_make_pid(env,&self),enif_make_pid(env,&copied)) ||
        enif_get_local_pid(NULL,enif_make_atom(env,"undefined"),&copied) ||
        enif_get_local_pid(NULL,enif_make_list(env,0),&copied))
        return enif_make_badarg(env);
    ErlNifEnv *owned = enif_alloc_env();
    if (!owned) return enif_make_badarg(env);
    ERL_NIF_TERM first = enif_make_copy(env, enif_make_list1(owned, enif_make_int(owned, 1)));
    enif_clear_env(owned);
    ERL_NIF_TERM second = enif_make_copy(env, enif_make_list1(owned, enif_make_int(owned, 2)));
    enif_free_env(owned);
    if (!enif_is_identical(first, enif_make_list1(env, enif_make_int(env, 1))) ||
        !enif_is_identical(second, enif_make_list1(env, enif_make_int(env, 2))))
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

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
static int approach_stack_reserve(unsigned depth)
{
    volatile unsigned char frame[1024];
    int result;
    frame[0] = (unsigned char)depth;
    result = beam_native_stack_has_space(32 * 1024);
    if (result != 1)
        return result == 0 && depth > 0;
    if (depth == 8192)
        return 0;
    result = approach_stack_reserve(depth + 1);
    /* Keep every frame live until its child has returned. */
    return result && frame[0] == (unsigned char)depth;
}

static void *stack_worker(void *result)
{
    *(int *)result = beam_native_stack_has_space(0);
    return NULL;
}

static ERL_NIF_TERM stack_check(ErlNifEnv *env,int n,const ERL_NIF_TERM a[])
{
    ErlNifTid thread;
    int worker_result = 7;
    (void)n; (void)a;
    if (beam_native_stack_has_space(0) != 1 ||
        beam_native_stack_has_space(10 * 1024) != 1 ||
        beam_native_stack_has_space(SIZE_MAX) != 0 ||
        !approach_stack_reserve(0) ||
        enif_thread_create("native_stack_query", &thread, stack_worker,
                           &worker_result, NULL) != 0)
        return enif_make_badarg(env);
    if (enif_thread_join(thread, NULL) != 0 || worker_result != -1)
        return enif_make_badarg(env);
    return enif_make_atom(env, "ok");
}
#ifdef BAD_FUNCTION
static ErlNifFunc functions[]={{"absent",0,hold,0}};
#else
static ERL_NIF_TERM latin1_name(ErlNifEnv *env,int n,const ERL_NIF_TERM a[])
{
    (void)n; (void)a;
    return enif_make_atom(env,"ok");
}
static ErlNifFunc functions[]={{"hold",0,hold,0},{"fatal",1,fatal,0},
                              {"caf\xe9",0,latin1_name,0},
                              {"stack_check",0,stack_check,0},
                              {"stack_check_dirty",0,stack_check,ERL_NIF_DIRTY_JOB_CPU_BOUND},
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
