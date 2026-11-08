/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_MANAGEMENT_PROCESS_TEST_H
#define BEAM_MANAGEMENT_PROCESS_TEST_H

#include <beam/process.h>

/* Included by the existing bare management owner after its helpers. */
enum { PROCESS_TEST_ALL_FIELDS = (1u << 6) - 1 };
static const char process_saved_name[] = {'n','a','t','i','v','e',0,'p','r','o','c'};

static int process_name_is(beam_process_atom_name name, const char *data, size_t size)
{
    return name.size == size && (!size || (name.data && !memcmp(name.data,data,size)));
}

static int process_call(beam_vm *vm, BeamEnv *env, const char *function,
                        BeamTerm arguments, BeamTerm *value)
{
    BeamBinary encoded = {0};
    beam_result result = {0};
    CHECK(beam_enif_term_to_binary(env,arguments,&encoded));
    beam_status status=beam_vm_call(vm,"bare_management",function,
        (beam_bytes){encoded.data,encoded.size},&result);
    beam_enif_release_binary(&encoded);
    CHECK(status==BEAM_OK && result.kind==BEAM_RETURN && result.value.size);
    size_t consumed=beam_enif_binary_to_term(env,result.value.data,result.value.size,value,0);
    size_t size=result.value.size;
    beam_result_release(&result);
    CHECK(consumed==size);
    return 0;
}

static int process_call_expect(beam_vm *vm, BeamEnv *env, const char *function,
                               BeamTerm arguments, BeamTerm expected)
{
    BeamTerm value;
    CHECK(!process_call(vm,env,function,arguments,&value));
    CHECK(beam_enif_is_identical(value,expected));
    return 0;
}

static int process_snapshot_fields(const beam_process_snapshot *snapshot,
                                   const BeamPid *pid, unsigned fields,
                                   const char *name, size_t name_size)
{
    CHECK(!beam_enif_compare_pids(&snapshot->pid,pid) &&
          snapshot->fields==fields && !snapshot->is_system_process);
    if(fields & BEAM_PROCESS_FIELD_STATE)
        CHECK(snapshot->state>=BEAM_PROCESS_RUNNABLE && snapshot->state<=BEAM_PROCESS_EXITING);
    else CHECK(!snapshot->state);
    if(!(fields & BEAM_PROCESS_FIELD_REDUCTIONS)) CHECK(!snapshot->reductions);
    if(fields & BEAM_PROCESS_FIELD_MEMORY) CHECK(snapshot->memory_bytes>0);
    else CHECK(!snapshot->memory_bytes);
    if(!(fields & BEAM_PROCESS_FIELD_MESSAGE_QUEUE)) CHECK(!snapshot->message_queue_len);
    if(fields & BEAM_PROCESS_FIELD_INITIAL_CALL) {
        CHECK(process_name_is(snapshot->initial_call.module,"bare_management",15) &&
              process_name_is(snapshot->initial_call.function,"process_boot",12) &&
              snapshot->initial_call.arity==2);
    } else CHECK(!snapshot->initial_call.module.data && !snapshot->initial_call.module.size &&
                 !snapshot->initial_call.function.data && !snapshot->initial_call.function.size &&
                 !snapshot->initial_call.arity);
    if(fields & BEAM_PROCESS_FIELD_REGISTERED_NAME)
        CHECK(snapshot->has_registered_name && process_name_is(snapshot->registered_name,name,name_size));
    else CHECK(!snapshot->has_registered_name && !snapshot->registered_name.data &&
               !snapshot->registered_name.size);
    return 0;
}

#ifdef BEAM_TEST_SNAPSHOT_ALLOCATIONS
static int process_snapshot_allocation_failures(beam_vm *vm, const BeamPid *pid)
{
    beam_process_query query;
    beam_process_query_init(&query);
    query.fields=PROCESS_TEST_ALL_FIELDS;
    for(unsigned inventory=0;inventory<2;++inventory) {
        beam_process_list list={0};
        beam_process_snapshot snapshot={0};
        CHECK(!snapshot_outstanding);
        snapshot_fault_begin(0);
        beam_status status=inventory ? beam_process_list_pids(vm,&list) :
            beam_process_inspect(vm,pid,&query,&snapshot);
        snapshot_injection=0;
        size_t allocations=snapshot_allocations;
        CHECK(status==BEAM_OK && allocations && snapshot_outstanding && !snapshot_overflow);
        beam_process_list_release(&list);
        beam_process_snapshot_release(&snapshot);
        CHECK(!snapshot_outstanding);
        snapshot_tracking=0;
        for(size_t fail_at=1;fail_at<=allocations;++fail_at) {
            BeamPid sentinel_pid=*pid;
            list=(beam_process_list){17,&sentinel_pid};
            snapshot=(beam_process_snapshot){.fields=0xdead,.memory_bytes=0xbeef};
            snapshot_fault_begin(fail_at);
            status=inventory ? beam_process_list_pids(vm,&list) :
                beam_process_inspect(vm,pid,&query,&snapshot);
            snapshot_injection=0;
            CHECK(status==BEAM_OUT_OF_MEMORY && snapshot_injected &&
                  !snapshot_overflow && !snapshot_outstanding);
            if(inventory) CHECK(list.count==17 && list.pids==&sentinel_pid);
            else CHECK(snapshot.fields==0xdead && snapshot.memory_bytes==0xbeef &&
                       !snapshot.registered_name.data && !snapshot.initial_call.module.data);
            snapshot_tracking=0;
        }
    }
    return 0;
}

static int process_spawn_allocation_failures(beam_vm *vm, BeamEnv *env,
                                             const BeamPid *unchanged)
{
    beam_process_spawn_options options;
    beam_process_spawn_options_init(&options);
    BeamTerm name=beam_enif_make_atom(env,"managed_oom_spawn");
    BeamTerm arguments=beam_enif_make_list(env,2,name,beam_enif_make_int(env,17));
    for(size_t fail_at=1;fail_at<=2;++fail_at) {
        BeamPid output=*unchanged, lookup;
        snapshot_fault_begin(fail_at);
        beam_status status=beam_process_spawn(vm,"bare_management","process_boot",
            env,arguments,&options,&output,NULL);
        snapshot_injection=0;
        CHECK(status==BEAM_OUT_OF_MEMORY && snapshot_injected &&
              !snapshot_overflow && !snapshot_outstanding &&
              !beam_enif_compare_pids(&output,unchanged));
        snapshot_tracking=0;
        CHECK(!beam_enif_whereis_pid(NULL,name,&lookup));
        unsigned length=0;
        CHECK(beam_enif_get_list_length(env,arguments,&length) && length==2);
    }
    return 0;
}
#endif

static int process_invalid_inputs(beam_vm *vm, BeamEnv *env, const BeamPid *worker)
{
    beam_process_query valid,bad_size,bad_fields;
    beam_process_query_init(&valid);
    bad_size=bad_fields=valid;
    bad_size.struct_size=sizeof valid+1;
    bad_fields.fields=PROCESS_TEST_ALL_FIELDS+1;
    BeamPid undefined;
    beam_enif_set_pid_undefined(&undefined);
    const struct {
        const BeamPid *target;
        const beam_process_query *query;
        int null_output;
    } inspections[]={
        {worker,&bad_size,0},{worker,&bad_fields,0},{NULL,&valid,0},
        {&undefined,&valid,0},{worker,NULL,0},{worker,&valid,1}
    };
    for(size_t i=0;i<sizeof inspections/sizeof inspections[0];++i) {
        beam_process_snapshot snapshot={.fields=0xdead,.memory_bytes=0xbeef};
        unsigned char unchanged[sizeof snapshot];
        memcpy(unchanged,&snapshot,sizeof snapshot);
        CHECK(beam_process_inspect(vm,inspections[i].target,inspections[i].query,
            inspections[i].null_output?NULL:&snapshot)==BEAM_INVALID_ARGUMENT &&
            !memcmp(&snapshot,unchanged,sizeof snapshot));
    }

    beam_process_spawn_options options;
    beam_process_spawn_options_init(&options);
    BeamTerm arguments=beam_enif_make_list(env,2,
        beam_enif_make_atom(env,"managed_invalid_spawn"),beam_enif_make_int(env,17));
    const struct {
        const char *module,*function;
        int bad_size,null_output;
    } spawns[]={
        {"bare_management","process_boot",1,0},
        {"\xc0\xaf","process_boot",0,0},{"bare_management","\xed\xa0\x80",0,0},
        {NULL,"process_boot",0,0},{"bare_management",NULL,0,0},
        {"","process_boot",0,0},{"bare_management","",0,0},
        {"bare_management","process_boot",0,1}
    };
    for(size_t i=0;i<sizeof spawns/sizeof spawns[0];++i) {
        beam_process_spawn_options selected=options;
        if(spawns[i].bad_size) selected.struct_size=sizeof selected+1;
        BeamPid output=*worker;
        unsigned length=0;
        CHECK(beam_process_spawn(vm,spawns[i].module,spawns[i].function,env,arguments,
            &selected,spawns[i].null_output?NULL:&output,NULL)==BEAM_INVALID_ARGUMENT &&
            !beam_enif_compare_pids(&output,worker));
        CHECK(beam_enif_get_list_length(env,arguments,&length) && length==2);
    }
    beam_enif_clear_env(env);
    return 0;
}

static int check_process_management(beam_vm *vm, beam_process_snapshot *saved)
{
    BeamEnv *env=beam_enif_alloc_env(), *arguments_env=beam_enif_alloc_env();
    CHECK(env && arguments_env);
    BeamTerm ok=beam_enif_make_atom(env,"ok"), default_leader_term;
    CHECK(!process_call(vm,env,"process_default_leader",beam_enif_make_list(env,0),&default_leader_term));
    BeamPid default_leader, worker, child;
    CHECK(beam_enif_get_local_pid(env,default_leader_term,&default_leader));
    beam_enif_set_pid_undefined(&worker);
    beam_process_spawn_options options;
    beam_process_spawn_options_init(&options);
    CHECK(options.struct_size==sizeof options && beam_enif_is_pid_undefined(&options.group_leader));
    BeamTerm payload=beam_enif_make_tuple(arguments_env,3,
        beam_enif_make_atom(arguments_env,"native_payload"),
        beam_enif_make_ref(arguments_env),
        beam_enif_make_list(arguments_env,2,beam_enif_make_int(arguments_env,5),
                           beam_enif_make_int(arguments_env,9)));
    BeamTerm expected_payload=beam_enif_make_copy(env,payload);
    BeamTerm arguments=beam_enif_make_list(arguments_env,2,
        beam_enif_make_atom(arguments_env,"managed_native_process"),payload);
    OK(beam_process_spawn(vm,"bare_management","process_boot",arguments_env,arguments,
                         &options,&worker,NULL));
    CHECK(!beam_enif_is_pid_undefined(&worker));
    BeamTerm head,tail;
    unsigned length=0;
    CHECK(beam_enif_get_list_length(arguments_env,arguments,&length) && length==2 &&
          beam_enif_get_list_cell(arguments_env,arguments,&head,&tail) &&
          beam_enif_is_identical(head,beam_enif_make_atom(arguments_env,"managed_native_process")) &&
          beam_enif_get_list_cell(arguments_env,tail,&head,&tail) &&
          beam_enif_is_identical(head,payload));
    beam_enif_clear_env(arguments_env);
    BeamTerm worker_term=beam_enif_make_pid(env,&worker);
    CHECK(!process_call_expect(vm,env,"process_probe",beam_enif_make_list(env,1,worker_term),
        beam_enif_make_tuple(env,2,expected_payload,default_leader_term)));
    CHECK(!process_invalid_inputs(vm,arguments_env,&worker));

    beam_process_query query;
    beam_process_query_init(&query);
    CHECK(query.struct_size==sizeof query && !query.fields);
    beam_process_snapshot snapshot={0};
    for(unsigned fields=0;fields<=PROCESS_TEST_ALL_FIELDS;++fields) {
        query.fields=fields;
        OK(beam_process_inspect(vm,&worker,&query,&snapshot));
        CHECK(!process_snapshot_fields(&snapshot,&worker,fields,"managed_native_process",22));
        beam_process_snapshot_release(&snapshot);
        CHECK(!snapshot.fields && !snapshot.initial_call.module.data && !snapshot.registered_name.data);
    }

    CHECK(!call_ok(vm,"bare_management","process_churn_start",empty));
    unsigned before,after;
    CHECK(!call_number(vm,"bare_management","process_churn_progress",&before));
    for(unsigned iteration=0;iteration<24;++iteration) {
        beam_process_list list={0};
        OK(beam_process_list_pids(vm,&list));
        size_t workers=0, systems=0;
        for(size_t i=0;i<list.count;++i) {
            if(!beam_enif_compare_pids(&list.pids[i],&worker)) ++workers;
            for(size_t j=0;j<i;++j) CHECK(beam_enif_compare_pids(&list.pids[i],&list.pids[j]));
            beam_process_query_init(&query);
            beam_status status=beam_process_inspect(vm,&list.pids[i],&query,&snapshot);
            CHECK(status==BEAM_OK || status==BEAM_NOT_FOUND);
            if(status==BEAM_OK) {
                CHECK(!snapshot.fields && !beam_enif_compare_pids(&snapshot.pid,&list.pids[i]));
                if(snapshot.is_system_process) ++systems;
                beam_process_snapshot_release(&snapshot);
            }
        }
        CHECK(workers==1 && systems>0);
        beam_process_list_release(&list);
        CHECK(!list.count && !list.pids);
    }
    CHECK(!call_number(vm,"bare_management","process_churn_progress",&after) && after>before);
    CHECK(!call_ok(vm,"bare_management","process_churn_stop",empty));

    /* Rename the same process so snapshot bytes cannot be borrowed atoms. */
    static const char unicode_name[]="native_\xce\xbb_\xe6\xa8\xa1\xe5\x9d\x97";
    const char *names[]={"",unicode_name,process_saved_name};
    const size_t sizes[]={0,sizeof unicode_name-1,sizeof process_saved_name};
    for(size_t i=0;i<sizeof names/sizeof names[0];++i) {
        BeamTerm name;
        CHECK(beam_enif_make_new_atom_len(env,names[i],sizes[i],&name,ERL_NIF_UTF8));
        CHECK(!process_call_expect(vm,env,"process_rename",
            beam_enif_make_list(env,2,worker_term,name),ok));
        query.fields=PROCESS_TEST_ALL_FIELDS;
        OK(beam_process_inspect(vm,&worker,&query,&snapshot));
        CHECK(!process_snapshot_fields(&snapshot,&worker,query.fields,names[i],sizes[i]));
        if(i+1==sizeof names/sizeof names[0]) { *saved=snapshot; snapshot=(beam_process_snapshot){0}; }
        else beam_process_snapshot_release(&snapshot);
    }
#ifdef BEAM_TEST_SNAPSHOT_ALLOCATIONS
    CHECK(!process_snapshot_allocation_failures(vm,&worker));
    CHECK(!process_spawn_allocation_failures(vm,arguments_env,&worker));
    beam_enif_clear_env(arguments_env);
#endif

    /* Group-leader override is installed before the native child can run. */
    options.group_leader=worker;
    arguments=beam_enif_make_list(arguments_env,2,beam_enif_make_atom(arguments_env,"undefined"),
                                  beam_enif_make_int(arguments_env,17));
    OK(beam_process_spawn(vm,"bare_management","process_boot",arguments_env,arguments,
                         &options,&child,NULL));
    BeamTerm child_term=beam_enif_make_pid(env,&child);
    CHECK(!process_call_expect(vm,env,"process_probe",beam_enif_make_list(env,1,child_term),
        beam_enif_make_tuple(env,2,beam_enif_make_int(env,17),worker_term)));
    query.fields=BEAM_PROCESS_FIELD_REGISTERED_NAME;
    OK(beam_process_inspect(vm,&child,&query,&snapshot));
    CHECK(!snapshot.has_registered_name && !snapshot.registered_name.data && !snapshot.registered_name.size);
    beam_process_snapshot_release(&snapshot);
    CHECK(!process_call_expect(vm,env,"process_stop",beam_enif_make_list(env,1,child_term),ok));
    options.group_leader=child;
    BeamPid unchanged=worker;
    CHECK(beam_process_spawn(vm,"bare_management","process_boot",arguments_env,arguments,
                            &options,&unchanged,NULL)==BEAM_NOT_FOUND &&
          !beam_enif_compare_pids(&unchanged,&worker));

    /* Bad native argument lists fail without consuming their environment or PID output. */
    beam_process_spawn_options_init(&options);
    BeamTerm invalid=beam_enif_make_list_cell(arguments_env,beam_enif_make_int(arguments_env,1),
                                              beam_enif_make_int(arguments_env,2));
    CHECK(beam_process_spawn(vm,"bare_management","process_boot",arguments_env,invalid,
                            &options,&unchanged,NULL)==BEAM_INVALID_TERM &&
          !beam_enif_compare_pids(&unchanged,&worker));
    CHECK(beam_enif_get_list_cell(arguments_env,invalid,&head,&tail));
    int integer;
    CHECK(beam_enif_get_int(arguments_env,tail,&integer) && integer==2);
    beam_enif_clear_env(arguments_env);

    /* Immediate normal and undef exits must still return publication success. */
    const char *functions[]={"process_return","missing_native_process_export"};
    for(size_t i=0;i<sizeof functions/sizeof functions[0];++i) {
        OK(beam_process_spawn(vm,"bare_management",functions[i],arguments_env,
            beam_enif_make_list(arguments_env,0),&options,&child,NULL));
        CHECK(!beam_enif_is_pid_undefined(&child));
        CHECK(!process_call_expect(vm,env,"process_wait_absent",
            beam_enif_make_list(env,1,beam_enif_make_pid(env,&child)),ok));
    }

    beam_process_list membership={0};
    OK(beam_process_list_pids(vm,&membership));
    size_t occurrences=0;
    for(size_t i=0;i<membership.count;++i)
        if(!beam_enif_compare_pids(&membership.pids[i],&worker)) ++occurrences;
    CHECK(occurrences==1);
    CHECK(!process_call_expect(vm,env,"process_stop",beam_enif_make_list(env,1,worker_term),ok));
    snapshot=(beam_process_snapshot){.fields=0xdead,.memory_bytes=0xbeef};
    CHECK(beam_process_inspect(vm,&worker,&query,&snapshot)==BEAM_NOT_FOUND &&
          snapshot.fields==0xdead && snapshot.memory_bytes==0xbeef);
    beam_process_list_release(&membership);
    beam_process_list_release(NULL);
    beam_process_snapshot_release(NULL);
    beam_process_list empty_list={0};
    beam_process_snapshot empty_snapshot={0};
    beam_process_list_release(&empty_list);
    beam_process_snapshot_release(&empty_snapshot);
    beam_enif_free_env(arguments_env);
    beam_enif_free_env(env);
    return 0;
}

static int check_process_stopped(beam_vm *vm)
{
    BeamPid undefined;
    beam_enif_set_pid_undefined(&undefined);
    beam_process_list list={17,&undefined};
    CHECK(beam_process_list_pids(vm,&list)==BEAM_STOPPED && list.count==17 && list.pids==&undefined);
    return 0;
}

static int check_process_history(const beam_process_snapshot *saved)
{
    /* No live PID or native-term operation is valid here. Check copied facts. */
    CHECK(saved->fields==PROCESS_TEST_ALL_FIELDS && !saved->is_system_process &&
          saved->state>=BEAM_PROCESS_RUNNABLE && saved->state<=BEAM_PROCESS_EXITING &&
          saved->memory_bytes>0 && saved->has_registered_name &&
          process_name_is(saved->registered_name,process_saved_name,sizeof process_saved_name) &&
          process_name_is(saved->initial_call.module,"bare_management",15) &&
          process_name_is(saved->initial_call.function,"process_boot",12) &&
          saved->initial_call.arity==2);
    return 0;
}

#ifdef BEAM_TEST_PROCESS_RETIREMENT
#include <stdatomic.h>

/* Linux static acceptance interposes the existing native boundaries without
 * inspecting private process/options layouts or adding production test hooks.
 * BeamTerm has the native Eterm scalar ABI; options are forwarded opaquely. */
struct process;
extern void erts_bare_halt(int status);
extern void __real_erts_bare_process_inspection_send(BeamTerm target, void *signal);
extern BeamTerm __real_erl_create_process(struct process *parent, BeamTerm module,
                                         BeamTerm function, BeamTerm arguments,
                                         void *options);
static atomic_int process_inspection_stop_armed, process_spawn_stop_armed;
static atomic_uint process_inspection_stop_hits, process_spawn_stop_hits;

void __wrap_erts_bare_process_inspection_send(BeamTerm target, void *signal)
{
    __real_erts_bare_process_inspection_send(target,signal);
    if(atomic_exchange_explicit(&process_inspection_stop_armed,0,memory_order_acq_rel)) {
        atomic_fetch_add_explicit(&process_inspection_stop_hits,1,memory_order_relaxed);
        erts_bare_halt(55);
    }
}

BeamTerm __wrap_erl_create_process(struct process *parent, BeamTerm module,
                                   BeamTerm function, BeamTerm arguments,
                                   void *options)
{
    BeamTerm pid=__real_erl_create_process(parent,module,function,arguments,options);
    if(beam_enif_is_pid(NULL,pid) &&
       atomic_exchange_explicit(&process_spawn_stop_armed,0,memory_order_acq_rel)) {
        atomic_fetch_add_explicit(&process_spawn_stop_hits,1,memory_order_relaxed);
        erts_bare_halt(56);
    }
    return pid;
}

static int check_process_stop_retirement(beam_system *system)
{
    int baseline=threads();
    for(unsigned publication=0;publication<2;++publication) {
        beam_vm *vm=NULL;
        beam_composition *composition=NULL;
        beam_vm_options vm_options;
        beam_composition_options selection={.struct_size=sizeof selection,
            .builtin_binding_count=BEAM_TEST_MODERN_BINDING_COUNT,
            .builtin_bindings=beam_test_modern_bindings};
        beam_vm_options_init(&vm_options);
        vm_options.schedulers=vm_options.schedulers_online=1;
        vm_options.dirty_cpu_schedulers=vm_options.dirty_cpu_schedulers_online=1;
        OK(beam_composition_prepare(&selection,&composition,NULL));
        vm_options.composition=composition;
        OK(beam_vm_init(system,&vm_options,&vm,NULL));
        beam_composition_release(&composition);
        OK(beam_module_load(vm,(beam_bytes){bare_management,sizeof bare_management}));
        OK(beam_vm_start(vm,NULL));
        BeamEnv *env=beam_enif_alloc_env();
        CHECK(env);
        BeamTerm arguments=beam_enif_make_list(env,2,beam_enif_make_atom(env,"undefined"),
                                               beam_enif_make_int(env,17));
        beam_process_spawn_options options;
        beam_process_spawn_options_init(&options);
        BeamPid child;
        beam_enif_set_pid_undefined(&child);
        if(publication) {
            atomic_store_explicit(&process_spawn_stop_armed,1,memory_order_release);
            OK(beam_process_spawn(vm,"bare_management","process_boot",env,arguments,
                                 &options,&child,NULL));
            CHECK(!beam_enif_is_pid_undefined(&child) &&
                  !atomic_load_explicit(&process_spawn_stop_armed,memory_order_acquire) &&
                  atomic_load_explicit(&process_spawn_stop_hits,memory_order_relaxed)==1);
            BeamTerm head,tail;
            int value;
            CHECK(beam_enif_get_list_cell(env,arguments,&head,&tail) &&
                  beam_enif_is_identical(head,beam_enif_make_atom(env,"undefined")) &&
                  beam_enif_get_list_cell(env,tail,&head,&tail) &&
                  beam_enif_get_int(env,head,&value) && value==17 &&
                  beam_enif_is_empty_list(env,tail));
        } else {
            OK(beam_process_spawn(vm,"bare_management","process_boot",env,arguments,
                                 &options,&child,NULL));
            BeamTerm reply;
            CHECK(!process_call(vm,env,"process_probe",
                beam_enif_make_list(env,1,beam_enif_make_pid(env,&child)),&reply));
            int arity;
            const BeamTerm *values;
            CHECK(beam_enif_get_tuple(env,reply,&arity,&values) && arity==2 &&
                  beam_enif_is_identical(values[0],beam_enif_make_int(env,17)));
            beam_process_query query;
            beam_process_query_init(&query);
            query.fields=PROCESS_TEST_ALL_FIELDS;
            beam_process_snapshot snapshot={.fields=0xdead,.memory_bytes=0xbeef};
            unsigned char unchanged[sizeof snapshot];
            memcpy(unchanged,&snapshot,sizeof snapshot);
            /* One normal scheduler executes this send wrapper as aux work.
             * The acknowledged ordinary target cannot handle its enqueued
             * inspection signal before the wrapper requests halt. */
            atomic_store_explicit(&process_inspection_stop_armed,1,memory_order_release);
            CHECK(beam_process_inspect(vm,&child,&query,&snapshot)==BEAM_STOPPED &&
                  !memcmp(&snapshot,unchanged,sizeof snapshot));
            CHECK(!atomic_load_explicit(&process_inspection_stop_armed,memory_order_acquire) &&
                  atomic_load_explicit(&process_inspection_stop_hits,memory_order_relaxed)==1);
        }
        beam_vm_state_info state;
        int expected_halt=publication?56:55, halt=-1;
        OK(beam_vm_get_state(vm,&state));
        CHECK(state.state==BEAM_VM_HALT_REQUESTED && state.halt_status==expected_halt);
        OK(beam_vm_stop(vm,&halt));
        CHECK(halt==expected_halt);
        beam_enif_free_env(env);
        OK(beam_vm_terminate(&vm,&halt));
        CHECK(halt==expected_halt && !resources_clean());
        CHECK(joined_thread_count(baseline)==baseline);
    }
    return 0;
}
#endif

#endif
