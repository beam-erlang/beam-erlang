/* SPDX-License-Identifier: Apache-2.0 */
#ifdef BEAM_FILE_CONTRACT_TEST
/* Only the independently deployed acceptance unit has these bounded controls. */
#include <stdatomic.h>
#include <time.h>
static atomic_int test_action, test_delay, test_after, test_budget, test_entered;
void beam_file_test_reset(void)
{
    atomic_store(&test_action,0);
    atomic_store(&test_delay,0);
    atomic_store(&test_after,0);
    atomic_store(&test_budget,0);
    atomic_store(&test_entered,0);
}
ERL_NIF_TERM beam_file_test_control(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    const ERL_NIF_TERM *a;
    int n, ms, budget;
    char name[32];
    if (enif_is_identical(argv[0], enif_make_atom(env,"entered")))
        return enif_make_int(env, atomic_load(&test_entered));
    if (!enif_get_tuple(env,argv[0],&n,&a) || n != 3 ||
        !enif_get_atom(env,a[0],name,sizeof(name),ERL_NIF_LATIN1) ||
        !enif_get_int(env,a[1],&ms) || ms < 0 || ms > 1000 ||
        !enif_get_int(env,a[2],&budget) || budget < 0) return enif_make_badarg(env);
    atomic_store(&test_delay,ms);
    atomic_store(&test_after,strstr(name,"after") != NULL);
    atomic_store(&test_budget,budget);
    atomic_store(&test_entered,0);
    atomic_store(&test_action,!strncmp(name,"read",4) ? 1 : !strncmp(name,"write",5) ? 2 :
        !strncmp(name,"close",5) ? 3 : !strncmp(name,"open",4) ? 4 : 0);
    return enif_make_atom(env,"ok");
}
static int test_match(const char *action)
{
    int value = !strcmp(action,"read") ? 1 : !strcmp(action,"write") ? 2 : !strcmp(action,"close") ? 3 : 4;
    return value == atomic_load(&test_action);
}
static void test_wait(void)
{
    int ms = atomic_load(&test_delay);
    atomic_fetch_add(&test_entered,1);
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec wait = {ms/1000,(ms%1000)*1000000};
    while (nanosleep(&wait,&wait) < 0 && errno == EINTR) {}
#endif
}
int beam_file_test_before(const char *action)
{
    if (!test_match(action) || atomic_load(&test_after)) return 0;
    if (atomic_fetch_sub(&test_budget,1) > 0) return 0;
    if (atomic_load(&test_delay)) { test_wait(); return 0; }
    return 1;
}
void beam_file_test_after(const char *action)
{
    if (test_match(action) && atomic_load(&test_after) && atomic_fetch_sub(&test_budget,1) <= 0)
        test_wait();
}
#endif
