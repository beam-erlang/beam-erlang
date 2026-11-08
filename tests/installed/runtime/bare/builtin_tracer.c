/* SPDX-License-Identifier: Apache-2.0 */
#include <erl_nif.h>

static ErlNifResourceType *resource_type;
static ErlNifMutex *resource_mutex;
static unsigned resource_count;

unsigned builtin_ets_resource_live(void)
{
    unsigned count;
    if (resource_mutex) enif_mutex_lock(resource_mutex);
    count = resource_count;
    if (resource_mutex) enif_mutex_unlock(resource_mutex);
    return count;
}

static void resource_destroy(ErlNifEnv *env, void *object)
{
    (void)env; (void)object;
    enif_mutex_lock(resource_mutex);
    --resource_count;
    enif_mutex_unlock(resource_mutex);
}

static int load(ErlNifEnv *env, void **private, ERL_NIF_TERM info)
{
    (void)private; (void)info;
    resource_mutex = enif_mutex_create("ets_probe_resources");
    if (!resource_mutex) return 1;
    resource_type = enif_open_resource_type(env, NULL, "ets_probe_resource",
                        resource_destroy, ERL_NIF_RT_CREATE, NULL);
    if (!resource_type) {
        enif_mutex_destroy(resource_mutex); resource_mutex = NULL;
        return 1;
    }
    resource_count = 0;
    return 0;
}

static void unload(ErlNifEnv *env, void *private)
{
    (void)env; (void)private;
    enif_mutex_destroy(resource_mutex);
    resource_mutex = NULL;
    resource_type = NULL;
}

static ERL_NIF_TERM resource(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    void *object = enif_alloc_resource(resource_type, 1);
    ERL_NIF_TERM result;
    (void)argc; (void)argv;
    if (!object) return enif_raise_exception(env, enif_make_atom(env, "enomem"));
    enif_mutex_lock(resource_mutex);
    ++resource_count;
    enif_mutex_unlock(resource_mutex);
    result = enif_make_resource(env, object);
    enif_release_resource(object);
    return result;
}

static ERL_NIF_TERM resources(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    (void)argc; (void)argv;
    return enif_make_uint(env, builtin_ets_resource_live());
}

static ERL_NIF_TERM enabled(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifPid receiver;
    (void)argc;
    return enif_make_atom(env, enif_get_local_pid(env, argv[1], &receiver) &&
                              enif_is_process_alive(env, &receiver) ? "trace" : "remove");
}

static ERL_NIF_TERM trace(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifPid receiver;
    (void)argc;
    if (enif_get_local_pid(env, argv[1], &receiver)) {
        ERL_NIF_TERM message = enif_make_tuple4(env, enif_make_atom(env, "trace"),
                                              argv[2], argv[0], argv[3]);
        enif_send(env, &receiver, NULL, message);
    }
    return enif_make_atom(env, "ok");
}

static ErlNifFunc functions[] = {{"enabled", 3, enabled}, {"trace", 5, trace},
                               {"resource", 0, resource}, {"resources", 0, resources}};
ERL_NIF_INIT(builtin_tracer, functions, load, NULL, NULL, unload)
