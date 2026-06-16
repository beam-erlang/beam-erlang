/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/beam.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <fenv.h>
#include <unistd.h>
#include "host_observe.h"
#include "fixtures.h"
#include "support.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"async:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==BEAM_OK)
static const unsigned char empty[] = {131,106};
static const unsigned char ok[] = {131,119,2,'o','k'};
static unsigned callbacks;
static pthread_t owner;
static beam_vm *current;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static beam_status callback(void *context, beam_bytes request, beam_host_reply *reply)
{
    (void)context;(void)request;
    CHECK(pthread_equal(owner,pthread_self()));
    beam_result result={0};
    CHECK(beam_vm_call(current,"bare_async","answer",(beam_bytes){empty,sizeof empty},&result)==BEAM_INVALID_STATE);
    CHECK(beam_host_reply_complete(reply,(beam_bytes){ok,sizeof ok},0)==BEAM_INVALID_STATE);
    ++callbacks;
    return beam_host_reply_copy(reply,(beam_bytes){ok,sizeof ok});
}
static void *wake(void *argument)
{
    beam_vm *vm=argument;
    beam_vm_state_info info;
    CHECK(beam_vm_get_state(vm,&info)==BEAM_WRONG_THREAD);
    struct timespec delay={0,20000000};nanosleep(&delay,NULL);
    OK(beam_vm_wake(vm));
    return NULL;
}
static beam_result take(beam_vm *vm)
{
    beam_result result={0};
    for(double deadline=now()+5;;) {
        beam_pump_result pumped={.struct_size=sizeof pumped};
        OK(beam_vm_pump(vm,100,1,&pumped));
        CHECK(pumped.callbacks<=1);
        if(pumped.stop_requested) OK(beam_vm_stop(vm,NULL));
        beam_status status=beam_vm_call_take(vm,&result);
        if(status==BEAM_OK) return result;
        CHECK(status==BEAM_BUSY && now()<deadline);
    }
}
static void answer(beam_result *result)
{
    CHECK(result->kind==BEAM_RETURN && result->value.size==3 &&
          result->value.data[0]==131 && result->value.data[1]==97 && result->value.data[2]==42);
}
static void *run(void *unused)
{
    (void)unused;owner=pthread_self();beam_system *system=NULL;beam_error error={0};
    sigset_t blocked,original,observed;sigemptyset(&blocked);sigaddset(&blocked,SIGUSR1);
    CHECK(!pthread_sigmask(SIG_BLOCK,&blocked,&original));CHECK(!fesetround(FE_DOWNWARD));
    CHECK(beam_init(&system,&error)==BEAM_WRONG_THREAD && !system);
    beam_system_options system_options;beam_system_options_init(&system_options);
    system_options.owner=BEAM_SYSTEM_CALLING_THREAD;
    OK(beam_init_with_options(&system_options,&system,&error));
    beam_vm_options options;beam_vm_options_init(&options);
    beam_entry entry={"bare_async","answer",{empty,sizeof empty}};
    options.execution=BEAM_PROCESS_OWNED;options.initial_root=&entry;
    CHECK(beam_vm_init(system,&options,&current,&error)==BEAM_UNSUPPORTED && !current);
    beam_vm_options_init(&options);
    beam_result saved={0};
    int descriptors=beam_test_host_entries("/proc/self/fd");
    int threads=beam_test_host_threads(getpid());CHECK(descriptors>=0 && threads>0);
    for(int cycle=0;cycle<4;++cycle) {
        OK(beam_vm_init(system,&options,&current,&error));
        OK(beam_vm_load(current,(beam_bytes){bare_async,sizeof bare_async}));
        OK(beam_vm_load(current,(beam_bytes){host_module,sizeof host_module}));
        OK(beam_vm_set_host_handler(current,callback,NULL));
        OK(beam_vm_start(current,&error));
        unsigned char copied[]={131,106};
        char module_name[]="bare_async", function_name[]="answer";
        OK(beam_vm_call_begin(current,module_name,function_name,(beam_bytes){copied,sizeof copied}));
        memset(module_name,0,sizeof module_name);memset(function_name,0,sizeof function_name);
        memset(copied,0,sizeof copied);
        CHECK(beam_vm_call_begin(current,"bare_async","answer",(beam_bytes){empty,sizeof empty})==BEAM_BUSY);
        beam_module_info module={.struct_size=sizeof module};
        CHECK(beam_vm_get_module(current,"bare_async",&module)==BEAM_BUSY);
        beam_result result=take(current);answer(&result);beam_result_release(&result);
        CHECK(beam_vm_call_take(current,&result)==BEAM_INVALID_STATE);
        CHECK(fegetround()==FE_DOWNWARD);
        CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&observed) && sigismember(&observed,SIGUSR1));
        OK(beam_vm_call(current,"bare_async","idle",(beam_bytes){empty,sizeof empty},&result));
        answer(&result);beam_result_release(&result);
        for(double deadline=now()+5;callbacks<(unsigned)(cycle+1);) {
            beam_pump_result pumped={.struct_size=sizeof pumped};
            OK(beam_vm_pump(current,100,1,&pumped));CHECK(now()<deadline);
        }
        pthread_t thread;CHECK(!pthread_create(&thread,NULL,wake,current));
        beam_pump_result pumped={.struct_size=sizeof pumped};
        double started=now();OK(beam_vm_pump(current,5000,1,&pumped));CHECK(now()-started<2);
        CHECK(!pthread_join(thread,NULL));
        OK(beam_vm_call_begin(current,"bare_async","crash",(beam_bytes){empty,sizeof empty}));
        result=take(current);CHECK(result.kind==BEAM_EXCEPTION);beam_result_release(&result);
        OK(beam_vm_call(current,"bare_async","answer",(beam_bytes){empty,sizeof empty},&saved));
        if(cycle==1) {
            OK(beam_vm_call_begin(current,"bare_async","halt",(beam_bytes){empty,sizeof empty}));
            result=take(current);CHECK(result.kind==BEAM_HALT && result.halt_status==7);beam_result_release(&result);
        } else if(cycle==3) {
            OK(beam_vm_call_begin(current,"bare_async","answer",(beam_bytes){empty,sizeof empty}));
            for(double deadline=now()+5;;) {
                beam_pump_result ready={.struct_size=sizeof ready};
                OK(beam_vm_pump(current,100,1,&ready));CHECK(now()<deadline);
                if(ready.call_ready) break;
            }
            /* Completion retains the slot; stopping preserves the recorded return. */
            CHECK(beam_vm_call_begin(current,"bare_async","answer",(beam_bytes){empty,sizeof empty})==BEAM_BUSY);
            CHECK(beam_vm_set_schedulers_online(current,1,1,NULL)==BEAM_BUSY);
            OK(beam_vm_stop(current,NULL));
            OK(beam_vm_call_take(current,&result));answer(&result);beam_result_release(&result);
        } else {
            OK(beam_vm_call_begin(current,"bare_async","waiting",(beam_bytes){empty,sizeof empty}));
            OK(beam_vm_stop(current,NULL));
            if(cycle==0) CHECK(beam_vm_call_take(current,&result)==BEAM_STOPPED);
            /* This cycle deliberately leaves the stopped slot unconsumed. */
        }
        OK(beam_vm_terminate(&current,NULL));answer(&saved);
        CHECK(beam_test_host_entries("/proc/self/fd")==descriptors);
        CHECK(beam_test_host_threads(getpid())==threads);
        if(cycle<3) beam_result_release(&saved);
    }
    OK(beam_terminate(&system));answer(&saved);beam_result_release(&saved);
    CHECK(fegetround()==FE_DOWNWARD);
    CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&observed) && sigismember(&observed,SIGUSR1));
    CHECK(!pthread_sigmask(SIG_SETMASK,&original,NULL));CHECK(!fesetround(FE_TONEAREST));
    return NULL;
}
int main(void)
{
    pthread_t thread;CHECK(!pthread_create(&thread,NULL,run,NULL));
    CHECK(!pthread_join(thread,NULL));
    puts("Asynchronous owner-thread lifetimes completed.");return 0;
}
