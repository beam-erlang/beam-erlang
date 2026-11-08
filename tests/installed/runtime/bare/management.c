/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <beam/beam.h>
#include <beam/beam_enif.h>
#include <beam/zlib.h>
#include <dirent.h>
#include <errno.h>
#include <fenv.h>
#include <sched.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"
#include "builtin_bindings.h"
#ifdef __APPLE__
#include <unistd.h>
#include "host_observe.h"
#endif

/* Private accounting used only by installed acceptance. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);
extern unsigned long erts_total_code_size;

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "management:%d: %s\n", __LINE__, #test); return 1; \
} } while (0)
#define OK(call) CHECK((call) == BEAM_OK)

static const unsigned char empty_data[] = {131,106};
static const beam_bytes empty = {empty_data, sizeof empty_data};

#ifdef BEAM_TEST_SNAPSHOT_ALLOCATIONS
/* The static consumer interposes only its own unresolved libc references.
 * Injection and ledger access are owner-thread-local, including while workers
 * run. Tracking remains active until copied allocations have been released. */
#define SNAPSHOT_LEDGER_CAPACITY 32768u
#define SNAPSHOT_LEDGER_TOMBSTONE ((void *)(uintptr_t)1)
static void *snapshot_ledger[SNAPSHOT_LEDGER_CAPACITY];
static size_t snapshot_outstanding, snapshot_allocations, snapshot_fail_at;
static int snapshot_overflow, snapshot_injected;
static _Thread_local int snapshot_injection, snapshot_tracking;

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void __real_free(void *pointer);

static size_t snapshot_pointer_hash(void *pointer)
{
    uintptr_t value=(uintptr_t)pointer;
    return ((value>>4)^(value>>17))&(SNAPSHOT_LEDGER_CAPACITY-1);
}

static void snapshot_track(void *pointer)
{
    if(!pointer || !snapshot_injection) return;
    size_t first=SNAPSHOT_LEDGER_CAPACITY;
    size_t index=snapshot_pointer_hash(pointer);
    for(size_t step=0;step<SNAPSHOT_LEDGER_CAPACITY;++step) {
        if(snapshot_ledger[index]==SNAPSHOT_LEDGER_TOMBSTONE && first==SNAPSHOT_LEDGER_CAPACITY)
            first=index;
        if(!snapshot_ledger[index]) {
            if(first!=SNAPSHOT_LEDGER_CAPACITY) index=first;
            snapshot_ledger[index]=pointer;
            ++snapshot_outstanding;
            return;
        }
        index=(index+1)&(SNAPSHOT_LEDGER_CAPACITY-1);
    }
    if(first!=SNAPSHOT_LEDGER_CAPACITY) {
        snapshot_ledger[first]=pointer;
        ++snapshot_outstanding;
    } else snapshot_overflow=1;
}

static int snapshot_fail_allocation(void)
{
    if(!snapshot_injection) return 0;
    ++snapshot_allocations;
    if(snapshot_fail_at && snapshot_allocations==snapshot_fail_at) {
        snapshot_injected=1;
        errno=ENOMEM;
        return 1;
    }
    return 0;
}

void *__wrap_malloc(size_t size)
{
    if(snapshot_fail_allocation()) return NULL;
    void *pointer=__real_malloc(size);
    snapshot_track(pointer);
    return pointer;
}

void *__wrap_calloc(size_t count,size_t size)
{
    if(snapshot_fail_allocation()) return NULL;
    void *pointer=__real_calloc(count,size);
    snapshot_track(pointer);
    return pointer;
}

void __wrap_free(void *pointer)
{
    if(snapshot_tracking && pointer && snapshot_outstanding) {
        size_t index=snapshot_pointer_hash(pointer);
        for(size_t step=0;step<SNAPSHOT_LEDGER_CAPACITY && snapshot_ledger[index];++step) {
            if(snapshot_ledger[index]==pointer) {
                snapshot_ledger[index]=SNAPSHOT_LEDGER_TOMBSTONE;
                --snapshot_outstanding;
                break;
            }
            index=(index+1)&(SNAPSHOT_LEDGER_CAPACITY-1);
        }
    }
    __real_free(pointer);
}

static void snapshot_fault_begin(size_t fail_at)
{
    memset(snapshot_ledger,0,sizeof snapshot_ledger);
    snapshot_allocations=0;
    snapshot_fail_at=fail_at;
    snapshot_overflow=snapshot_injected=0;
    snapshot_tracking=1;
    snapshot_injection=1;
}

static int snapshot_allocation_failures(beam_vm *vm)
{
    beam_module_query query;beam_module_query_init(&query);
    query.fields=BEAM_MODULE_FIELD_EXPORTS|BEAM_MODULE_FIELD_FUNCTIONS|
                 BEAM_MODULE_FIELD_NIFS|BEAM_MODULE_FIELD_ATTRIBUTES|BEAM_MODULE_FIELD_COMPILE;
    beam_module_name name={"bare_managed",sizeof("bare_managed")-1};
    for(int inventory=0;inventory<2;++inventory) {
        beam_module_snapshot snapshot={0};
        CHECK(!snapshot_outstanding);
        snapshot_fault_begin(0);
        beam_status status=inventory?beam_module_inspect_all(vm,&query,&snapshot):
            beam_module_inspect(vm,name,&query,&snapshot);
        snapshot_injection=0;
        size_t allocations=snapshot_allocations;
        CHECK(status==BEAM_OK && allocations && snapshot_outstanding && !snapshot_overflow);
        beam_module_snapshot_release(&snapshot);
        CHECK(!snapshot_outstanding);
        snapshot_tracking=0;
        for(size_t fail_at=1;fail_at<=allocations;++fail_at) {
            /* A poisoned destination detects publication of any partial copy;
             * the sentinel itself is not an owning result and is never freed. */
            beam_module_record sentinel={0};
            snapshot=(beam_module_snapshot){17,&sentinel};
            snapshot_fault_begin(fail_at);
            status=inventory?beam_module_inspect_all(vm,&query,&snapshot):
                beam_module_inspect(vm,name,&query,&snapshot);
            snapshot_injection=0;
            CHECK(status==BEAM_OUT_OF_MEMORY && snapshot_injected &&
                  snapshot.count==17 && snapshot.items==&sentinel &&
                  !snapshot_overflow && !snapshot_outstanding);
            snapshot_tracking=0;
        }
    }
    return 0;
}

static int scheduler_allocation_failures(beam_vm *vm)
{
    beam_scheduler_snapshot snapshot={0};
    CHECK(!snapshot_outstanding);
    snapshot_fault_begin(0);
    beam_status status=beam_scheduler_inspect_all(vm,&snapshot);
    snapshot_injection=0;
    size_t allocations=snapshot_allocations;
    CHECK(status==BEAM_OK && allocations && snapshot_outstanding && !snapshot_overflow);
    beam_scheduler_snapshot_release(&snapshot);
    CHECK(!snapshot_outstanding);
    snapshot_tracking=0;
    for(size_t fail_at=1;fail_at<=allocations;++fail_at) {
        beam_scheduler_record scheduler={0};
        beam_scheduler_queue_record queue={0};
        beam_scheduler_snapshot sentinel={
            .info={.struct_size=19,.schedulers=23},
            .count=17,.items=&scheduler,.queue_count=13,.queues=&queue
        };
        snapshot=sentinel;
        unsigned char unchanged[sizeof snapshot];
        memcpy(unchanged,&snapshot,sizeof snapshot);
        snapshot_fault_begin(fail_at);
        status=beam_scheduler_inspect_all(vm,&snapshot);
        snapshot_injection=0;
        CHECK(status==BEAM_OUT_OF_MEMORY && snapshot_injected &&
              !memcmp(&snapshot,unchanged,sizeof snapshot) &&
              !snapshot_overflow && !snapshot_outstanding);
        snapshot_tracking=0;
    }
    return 0;
}
#endif

static double seconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static int threads(void)
{
#ifdef __APPLE__
    return beam_test_host_threads(getpid());
#else
    DIR *dir = opendir("/proc/self/task");
    struct dirent *entry;
    int count = 0;
    if (!dir) return -1;
    while ((entry = readdir(dir)))
        if (entry->d_name[0] != '.') ++count;
    closedir(dir);
    return count;
#endif
}

static int joined_thread_count(int baseline)
{
    double deadline = seconds() + 1;
    int count;
    /* Linux can expose an exiting task in /proc after pthread_join wakes:
     * clear_child_tid precedes final task release. Native allocation and
     * synchronization ledgers are checked separately before this census. */
    do {
        count = threads();
        if (count == baseline || count < 0) return count;
        sched_yield();
    } while (seconds() < deadline);
    return count;
}

static void diagnose_threads(int baseline)
{
    DIR *dir = opendir("/proc/self/task");
    struct dirent *entry;
    if (dir) {
        while ((entry = readdir(dir))) {
            char path[512], line[1024];
            FILE *status;
            if (entry->d_name[0] == '.') continue;
            snprintf(path,sizeof path,"/proc/self/task/%s/stat",entry->d_name);
            status = fopen(path,"r");
            if (status) {
                if (fgets(line,sizeof line,status)) fputs(line,stderr);
                fclose(status);
            }
            snprintf(path,sizeof path,"/proc/self/task/%s/status",entry->d_name);
            status = fopen(path,"r");
            fprintf(stderr,"task %s:%s\n",entry->d_name,status ? "" : " gone");
            if (!status) continue;
            while (fgets(line,sizeof line,status)) {
                if (!strncmp(line,"Name:",5) || !strncmp(line,"State:",6) ||
                    !strncmp(line,"Pid:",4) || !strncmp(line,"Threads:",8))
                    fputs(line,stderr);
            }
            fclose(status);
        }
        closedir(dir);
    }
    double start = seconds();
    int count;
    do {
        count = threads();
        if (count == baseline) break;
    } while (seconds() - start < 0.1);
    fprintf(stderr,"task recount after %.9fs: %d, baseline %d\n",
            seconds()-start,count,baseline);
}

static int number(const unsigned char *data, size_t size, size_t *position,
                  unsigned *out)
{
    size_t p = *position;
    if (p >= size) return 0;
    if (data[p] == 97 && p + 2 <= size) {
        *out = data[p + 1];
        *position = p + 2;
        return 1;
    }
    if (data[p] == 98 && p + 5 <= size) {
        *out = (unsigned)data[p+1] << 24 | (unsigned)data[p+2] << 16 |
               (unsigned)data[p+3] << 8 | data[p+4];
        *position = p + 5;
        return 1;
    }
    return 0;
}

static int atom_ok(const beam_result *result)
{
    const unsigned char *d = result->value.data;
    size_t n = result->value.size;
    return result->kind == BEAM_RETURN &&
        ((n == 5 && d[0] == 131 && (d[1] == 119 || d[1] == 115) &&
          d[2] == 2 && !memcmp(d+3,"ok",2)) ||
         (n == 6 && d[0] == 131 && (d[1] == 118 || d[1] == 100) &&
          d[2] == 0 && d[3] == 2 && !memcmp(d+4,"ok",2)));
}

static int call_ok(beam_vm *vm, const char *module, const char *function,
                   beam_bytes arguments)
{
    beam_result result = {0};
    OK(beam_vm_call(vm, module, function, arguments, &result));
    if (!atom_ok(&result)) {
        fprintf(stderr, "%s:%s outcome %d ETF:", module, function, result.kind);
        for (size_t i=0; i<result.value.size; ++i)
            fprintf(stderr, "%02x", result.value.data[i]);
        fputc('\n', stderr);
        BeamEnv *env = beam_enif_alloc_env();
        BeamTerm term;
        if (env && beam_enif_binary_to_term(env,result.value.data,result.value.size,&term,0) == result.value.size)
            beam_enif_fprintf(stderr,"%T\n",term);
        if (env) beam_enif_free_env(env);
        beam_result_release(&result);
        return 1;
    }
    beam_result_release(&result);
    return 0;
}

static int call_number(beam_vm *vm, const char *module, const char *function,
                       unsigned *out)
{
    beam_result result = {0};
    size_t p = 1;
    OK(beam_vm_call(vm,module,function,empty,&result));
    CHECK(result.kind == BEAM_RETURN && result.value.size && result.value.data[0] == 131);
    CHECK(number(result.value.data,result.value.size,&p,out) && p == result.value.size);
    beam_result_release(&result);
    return 0;
}

static int module_flags(beam_vm *vm, const char *name, unsigned flags)
{
    beam_module_info info = {sizeof info, 0xdead};
    OK(beam_module_get_info(vm, name, &info));
    CHECK(info.flags == flags && info.struct_size == sizeof info);
    return 0;
}

static int listed(const beam_module_list *list, const char *name)
{
    for (size_t i=0; i<list->count; ++i)
        if (!strcmp(list->names[i],name)) return 1;
    return 0;
}

static int sorted(const beam_module_list *list)
{
    for (size_t i=1; i<list->count; ++i)
        if (strcmp(list->names[i-1],list->names[i]) >= 0) return 0;
    return 1;
}

static beam_module_name module_name(const char *name)
{
    return (beam_module_name){name, strlen(name)};
}

static int has_function(beam_module_functions functions, const char *name,
                         unsigned arity)
{
    size_t size = strlen(name);
    for (size_t i=0; i<functions.count; ++i)
        if (functions.items[i].arity == arity &&
            functions.items[i].name.size == size &&
            !memcmp(functions.items[i].name.data, name, size))
            return 1;
    return 0;
}

static int metadata_list(beam_bytes bytes)
{
    BeamEnv *env = beam_enif_alloc_env();
    BeamTerm term;
    int valid = env && bytes.data && bytes.size &&
        beam_enif_binary_to_term(env, bytes.data, bytes.size, &term, 0) == bytes.size &&
        beam_enif_is_list(env, term);
    if (env) beam_enif_free_env(env);
    return valid;
}

static unsigned metadata_generation(beam_bytes bytes)
{
    BeamEnv *env=beam_enif_alloc_env();
    BeamTerm list,head,tail;
    unsigned generation=0;
    if(!env) return 0;
    if(beam_enif_binary_to_term(env,bytes.data,bytes.size,&list,0)==bytes.size) {
        while(beam_enif_get_list_cell(env,list,&head,&tail)) {
            const BeamTerm *tuple;
            int arity;
            char name[64];
            if(beam_enif_get_tuple(env,head,&arity,&tuple) && arity==2 &&
               beam_enif_get_atom(env,tuple[0],name,sizeof name,ERL_NIF_UTF8) &&
               !strcmp(name,"inspection_generation") &&
               beam_enif_get_list_cell(env,tuple[1],&head,&list)) {
                beam_enif_get_uint(env,head,&generation);
                break;
            }
            list=tail;
        }
    }
    beam_enif_free_env(env);
    return generation;
}

static int inspect_initial(beam_vm *vm, beam_module_snapshot *saved)
{
    beam_module_query query;
    beam_module_snapshot snapshot = {0};
    beam_module_query_init(NULL);
    beam_module_query_init(&query);
    CHECK(query.struct_size == sizeof query &&
          query.scopes == BEAM_MODULE_SCOPE_LOADED && !query.fields);
    OK(beam_module_inspect(vm,module_name("bare_managed"),&query,&snapshot));
    CHECK(snapshot.count == 1 && snapshot.items[0].flags == BEAM_MODULE_CURRENT);
    CHECK(snapshot.items[0].current.loaded_code_bytes > 0 &&
          snapshot.items[0].current.function_table_count > 0 &&
          !snapshot.items[0].fields && !snapshot.items[0].exports.count &&
          !snapshot.items[0].current.functions.count &&
          !snapshot.items[0].current.nifs.count &&
          !snapshot.items[0].current.attributes_etf.data &&
          !snapshot.items[0].current.compile_info_etf.data);
    const beam_module_record *original = snapshot.items;
    query.struct_size = 0;
    CHECK(beam_module_inspect_all(vm,&query,&snapshot) == BEAM_INVALID_ARGUMENT &&
          snapshot.count == 1 && snapshot.items == original);
    beam_module_query_init(&query);
    query.scopes = 0;
    CHECK(beam_module_inspect_all(vm,&query,&snapshot) == BEAM_INVALID_ARGUMENT);
    query.scopes = 1u << 10;
    CHECK(beam_module_inspect_all(vm,&query,&snapshot) == BEAM_INVALID_ARGUMENT);
    beam_module_query_init(&query);
    query.fields = 1u << 10;
    CHECK(beam_module_inspect_all(vm,&query,&snapshot) == BEAM_INVALID_ARGUMENT);
    beam_module_query_init(&query);
    CHECK(beam_module_inspect_all(vm,NULL,&snapshot) == BEAM_INVALID_ARGUMENT);
    CHECK(beam_module_inspect_all(vm,&query,NULL) == BEAM_INVALID_ARGUMENT);
    CHECK(beam_module_inspect(vm,(beam_module_name){NULL,0},&query,&snapshot) == BEAM_INVALID_ARGUMENT);
    CHECK(beam_module_inspect(vm,(beam_module_name){"\xc0\xaf",2},&query,&snapshot) == BEAM_INVALID_ARGUMENT);
    CHECK(beam_module_inspect(vm,(beam_module_name){"",0},&query,&snapshot) == BEAM_NOT_FOUND);
    CHECK(beam_module_inspect(vm,(beam_module_name){"bare_managed\0suffix",19},&query,&snapshot) == BEAM_NOT_FOUND);
    CHECK(snapshot.count == 1 && snapshot.items == original);
    unsigned char md5[16]; memcpy(md5, snapshot.items[0].current.md5, sizeof md5);
    beam_module_snapshot_release(&snapshot);
    query.fields = BEAM_MODULE_FIELD_EXPORTS | BEAM_MODULE_FIELD_FUNCTIONS |
        BEAM_MODULE_FIELD_NIFS | BEAM_MODULE_FIELD_ATTRIBUTES | BEAM_MODULE_FIELD_COMPILE;
    OK(beam_module_inspect(vm,module_name("bare_managed"),&query,&snapshot));
    const beam_module_record *record = snapshot.items;
    CHECK(record->fields == query.fields && !record->current.has_nif &&
          record->current.has_line_table == 1 &&
          !record->current.nifs.count && !record->current.nifs.items &&
          record->current.function_table_count >= record->current.functions.count &&
          has_function(record->exports,"generation",0) &&
          has_function(record->current.functions,"generation",0) &&
          has_function(record->current.functions,"busy",0) &&
          !has_function(record->exports,"busy",0) &&
          !memcmp(md5, record->current.md5, sizeof md5) &&
          metadata_list(record->current.attributes_etf) &&
          metadata_generation(record->current.attributes_etf) == 1 &&
          metadata_list(record->current.compile_info_etf));
    CHECK(!record->old.loaded_code_bytes && !record->old.function_table_count &&
          !record->old.functions.count && !record->old.attributes_etf.data &&
          !record->pending.loaded_code_bytes);
    if (saved) *saved = snapshot;
    else beam_module_snapshot_release(&snapshot);
    snapshot = (beam_module_snapshot){0};
    beam_module_snapshot_release(&snapshot);
    return 0;
}

static int counter_state(beam_vm *vm, const char *operation,
                          unsigned generation, unsigned state)
{
    beam_result result = {0};
    BeamEnv *env = beam_enif_alloc_env();
    BeamTerm term;
    const BeamTerm *tuple;
    int arity;
    unsigned observed_generation, observed_state;
    CHECK(env);
    OK(beam_vm_call(vm,"bare_management",operation,empty,&result));
    CHECK(result.kind == BEAM_RETURN &&
          beam_enif_binary_to_term(env,result.value.data,result.value.size,&term,0) == result.value.size &&
          beam_enif_get_tuple(env,term,&arity,&tuple) && arity == 2 &&
          beam_enif_get_uint(env,tuple[0],&observed_generation) &&
          beam_enif_get_uint(env,tuple[1],&observed_state) &&
          observed_generation == generation && observed_state == state);
    beam_enif_free_env(env);
    beam_result_release(&result);
    return 0;
}

static int unload_idle(beam_vm *vm, const char *name);

static int hot_management(beam_vm *vm)
{
    beam_module_query query;
    beam_module_query_init(&query);
    query.fields = BEAM_MODULE_FIELD_EXPORTS | BEAM_MODULE_FIELD_FUNCTIONS |
                   BEAM_MODULE_FIELD_ATTRIBUTES;
    beam_module_snapshot snapshot = {0};
    beam_module_name name = module_name("bare_managed");
    unsigned generation;
    OK(beam_module_replace(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1},NULL));
    CHECK(!call_ok(vm,"bare_managed","start_counter",empty));
    CHECK(!counter_state(vm,"counter_step",1,1));
    OK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL));
    CHECK(!call_number(vm,"bare_managed","generation",&generation) && generation == 2);
    OK(beam_module_inspect(vm,name,&query,&snapshot));
    CHECK(snapshot.items[0].flags == (BEAM_MODULE_CURRENT | BEAM_MODULE_OLD) &&
          memcmp(snapshot.items[0].current.md5,snapshot.items[0].old.md5,16) &&
          has_function(snapshot.items[0].old.functions,"counter",1) &&
          metadata_generation(snapshot.items[0].current.attributes_etf) == 2 &&
          metadata_generation(snapshot.items[0].old.attributes_etf) == 1);
    unsigned char current_md5[16]; memcpy(current_md5,snapshot.items[0].current.md5,16);
    beam_module_snapshot_release(&snapshot);
    CHECK(!counter_state(vm,"counter_step",1,2));
    CHECK(beam_module_soft_purge(vm,name,NULL) == BEAM_BUSY);
    CHECK(!counter_state(vm,"counter_step",1,3));
    CHECK(beam_module_replace(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1},NULL) == BEAM_BUSY);
    CHECK(beam_module_delete(vm,name,NULL) == BEAM_BUSY);
    OK(beam_module_inspect(vm,name,&query,&snapshot));
    CHECK(!memcmp(current_md5,snapshot.items[0].current.md5,16));
    beam_module_snapshot_release(&snapshot);
    CHECK(!counter_state(vm,"counter_switch",2,3));
    OK(beam_module_soft_purge(vm,name,NULL));
    CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
    CHECK(!counter_state(vm,"counter_step",2,4));
    CHECK(!call_ok(vm,"bare_management","stop_worker",empty));
    OK(beam_module_delete(vm,name,NULL));
    CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_OLD));
    OK(beam_module_inspect(vm,name,&query,&snapshot));
    CHECK(snapshot.items[0].flags == BEAM_MODULE_OLD && !snapshot.items[0].exports.count &&
          !snapshot.items[0].current.loaded_code_bytes && snapshot.items[0].old.loaded_code_bytes);
    beam_module_snapshot_release(&snapshot);
    query.scopes = BEAM_MODULE_SCOPE_CALLABLE;
    CHECK(beam_module_inspect(vm,name,&query,&snapshot) == BEAM_NOT_FOUND);
    /* Old-only replacement preserves the retired version until an explicit purge. */
    OK(beam_module_replace(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1},NULL));
    CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT | BEAM_MODULE_OLD));
    OK(beam_module_soft_purge(vm,name,NULL));
    OK(beam_module_soft_purge(vm,name,NULL));
    CHECK(!unload_idle(vm,"bare_managed"));
    CHECK(beam_module_delete(vm,name,NULL) == BEAM_NOT_FOUND);
    OK(beam_module_soft_purge(vm,name,NULL));
    return 0;
}

static unsigned beam_u32(const unsigned char *bytes)
{
    return (unsigned)bytes[0]<<24 | (unsigned)bytes[1]<<16 |
           (unsigned)bytes[2]<<8 | bytes[3];
}

static void beam_put_u32(unsigned char *bytes, unsigned value)
{
    bytes[0]=(unsigned char)(value>>24); bytes[1]=(unsigned char)(value>>16);
    bytes[2]=(unsigned char)(value>>8); bytes[3]=(unsigned char)value;
}

/* Vary the compiler-produced module atom and optional retained chunks. Every
 * executable assertion still uses the ordinary loader and actual code tables. */
static beam_bytes module_variant(beam_module_name name, int strip_metadata)
{
    const unsigned char *input=bare_managed_1;
    size_t input_size=sizeof bare_managed_1, from=12, to=12;
    unsigned char *output=calloc(input_size+name.size+8,1);
    int renamed=0;
    if(!output || input_size<12 || memcmp(input,"FOR1",4) || memcmp(input+8,"BEAM",4))
        goto invalid;
    memcpy(output,input,12);
    while(from+8<=input_size) {
        size_t size=beam_u32(input+from+4), padded=(size+3)&~(size_t)3;
        if(padded>input_size-from-8) goto invalid;
        if(strip_metadata && (!memcmp(input+from,"Attr",4) ||
                              !memcmp(input+from,"CInf",4) ||
                              !memcmp(input+from,"Line",4))) {
            from+=8+padded;
            continue;
        }
        if(!memcmp(input+from,"AtU8",4)) {
            if(size<5) goto invalid;
            int compact=(beam_u32(input+from+8)&0x80000000u)!=0;
            unsigned encoded=input[from+12];
            /* OTP 29's negative atom count selects compact BEAM lengths.
             * These fixture module names fit the one-byte unsigned form. */
            if(compact && ((encoded&15)!=0 || name.size>=16)) goto invalid;
            size_t original_size=compact?encoded>>4:encoded;
            if(name.size>255 || original_size>size-5 ||
               original_size!=strlen("bare_managed") ||
               memcmp(input+from+13,"bare_managed",original_size)) goto invalid;
            size_t new_size=size-original_size+name.size;
            memcpy(output+to,input+from,12);
            beam_put_u32(output+to+4,(unsigned)new_size);
            output[to+12]=(unsigned char)(compact?name.size<<4:name.size);
            memcpy(output+to+13,name.data,name.size);
            memcpy(output+to+13+name.size,input+from+13+original_size,size-5-original_size);
            to+=8+((new_size+3)&~(size_t)3);
            renamed=1;
        } else {
            memcpy(output+to,input+from,8+padded);
            to+=8+padded;
        }
        from+=8+padded;
    }
    if(from!=input_size || !renamed || to-8>UINT_MAX) goto invalid;
    beam_put_u32(output+4,(unsigned)(to-8));
    return (beam_bytes){output,to};
invalid:
    free(output);
    return (beam_bytes){0};
}

static int unusual_names(beam_vm *vm)
{
    static const char nul_name[]="bare\0managed";
    beam_module_name names[]={{"",0},{nul_name,sizeof nul_name-1}};
    beam_module_query query; beam_module_query_init(&query);
    query.fields=BEAM_MODULE_FIELD_EXPORTS|BEAM_MODULE_FIELD_FUNCTIONS|
                 BEAM_MODULE_FIELD_ATTRIBUTES|BEAM_MODULE_FIELD_COMPILE;
    for(size_t i=0;i<sizeof names/sizeof *names;++i) {
        beam_bytes bytes=module_variant(names[i],i==0);
        CHECK(bytes.data);
        beam_status status=beam_module_load(vm,bytes);
        free((void *)bytes.data);
        OK(status);
        beam_module_snapshot snapshot={0};
        OK(beam_module_inspect(vm,names[i],&query,&snapshot));
        CHECK(snapshot.count==1 && snapshot.items[0].name.size==names[i].size &&
              !memcmp(snapshot.items[0].name.data,names[i].data,names[i].size) &&
              snapshot.items[0].flags==BEAM_MODULE_CURRENT &&
              has_function(snapshot.items[0].exports,"generation",0));
        if(!i) CHECK(!snapshot.items[0].current.has_line_table &&
                     !snapshot.items[0].current.attributes_etf.data &&
                     !snapshot.items[0].current.attributes_etf.size &&
                     !snapshot.items[0].current.compile_info_etf.data &&
                     !snapshot.items[0].current.compile_info_etf.size);
        beam_module_snapshot_release(&snapshot);
    }
    beam_module_list legacy={0};
    CHECK(beam_module_list_names(vm,&legacy)==BEAM_UNSUPPORTED && !legacy.count && !legacy.names);
    beam_module_snapshot snapshot={0};
    OK(beam_module_inspect_all(vm,&query,&snapshot));
    CHECK(snapshot.count>=2 && !snapshot.items[0].name.size);
    unsigned found=0;
    for(size_t i=0;i<snapshot.count;++i)
        if(snapshot.items[i].name.size==sizeof nul_name-1 &&
           !memcmp(snapshot.items[i].name.data,nul_name,sizeof nul_name-1)) ++found;
    CHECK(found==1);
    beam_module_snapshot_release(&snapshot);
    for(size_t i=0;i<sizeof names/sizeof *names;++i) {
        OK(beam_module_delete(vm,names[i],NULL));
        OK(beam_module_soft_purge(vm,names[i],NULL));
        CHECK(beam_module_inspect(vm,names[i],&query,&snapshot)==BEAM_NOT_FOUND);
    }
    return 0;
}

static int purge_gate_management(beam_vm *vm)
{
    CHECK(!call_ok(vm,"bare_management","unauthorized_purge",empty));
    const char *controls[]={"gate_reserved_kill","gate_spoof","gate_fairness"};
    for(size_t i=0;i<sizeof controls/sizeof *controls;++i) {
        OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        OK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL));
        CHECK(!call_ok(vm,"bare_management",controls[i],empty));
        beam_status status=beam_module_soft_purge(vm,module_name("bare_managed"),NULL);
        CHECK(status==(!i?BEAM_SYSTEM_ERROR:BEAM_OK));
        if(i==2) CHECK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL)==BEAM_BUSY);
        CHECK(!call_ok(vm,"bare_management","finish_gate_control",empty));
        OK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!unload_idle(vm,"bare_managed"));
    }
    OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
    OK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL));
    static const unsigned char dirty_cpu[]={131,108,0,0,0,1,119,9,'d','i','r','t','y','_','c','p','u',106};
    CHECK(!call_ok(vm,"bare_management","start_dirty",(beam_bytes){dirty_cpu,sizeof dirty_cpu}));
    CHECK(!call_ok(vm,"bare_management","gate_active_kill",empty));
    CHECK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL)==BEAM_SYSTEM_ERROR);
    CHECK(!call_ok(vm,"bare_management","finish_gate_control",empty));
    CHECK(!call_ok(vm,"bare_management","stop_dirty",empty));
    OK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL));
    CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
    CHECK(!unload_idle(vm,"bare_managed"));
    return 0;
}

static int concurrent_module_observation(beam_vm *vm)
{
    beam_module_query query; beam_module_query_init(&query);
    query.fields=BEAM_MODULE_FIELD_EXPORTS|BEAM_MODULE_FIELD_FUNCTIONS|
                 BEAM_MODULE_FIELD_NIFS|BEAM_MODULE_FIELD_ATTRIBUTES|BEAM_MODULE_FIELD_COMPILE;
    beam_module_snapshot snapshot={0};
    unsigned char md5[2][16];
    OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
    OK(beam_module_inspect(vm,module_name("bare_managed"),&query,&snapshot));
    memcpy(md5[0],snapshot.items[0].current.md5,16);
    beam_module_snapshot_release(&snapshot);
    OK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL));
    OK(beam_module_inspect(vm,module_name("bare_managed"),&query,&snapshot));
    memcpy(md5[1],snapshot.items[0].current.md5,16);
    beam_module_snapshot_release(&snapshot);
    OK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL));

    BeamEnv *env=beam_enif_alloc_env();
    BeamTerm modules[2];
    BeamBinary arguments;
    CHECK(env);
    unsigned char *bytes=beam_enif_make_new_binary(env,sizeof bare_managed_1,&modules[0]);
    CHECK(bytes); memcpy(bytes,bare_managed_1,sizeof bare_managed_1);
    bytes=beam_enif_make_new_binary(env,sizeof bare_managed_2,&modules[1]);
    CHECK(bytes); memcpy(bytes,bare_managed_2,sizeof bare_managed_2);
    BeamTerm list=beam_enif_make_list_from_array(env,modules,2);
    CHECK(beam_enif_term_to_binary(env,list,&arguments));
    CHECK(!call_ok(vm,"bare_management","start_code_changes",(beam_bytes){arguments.data,arguments.size}));
    beam_enif_release_binary(&arguments);
    beam_enif_free_env(env);

    unsigned before,after,observations=0,seen=0;
    CHECK(!call_number(vm,"bare_management","code_change_progress",&before));
    double deadline=seconds()+10;
    while((observations<24 || seen!=3) && seconds()<deadline) {
        beam_status status=beam_module_inspect_all(vm,&query,&snapshot);
        if(status==BEAM_BUSY) continue;
        OK(status);
        const beam_module_record *record=NULL;
        for(size_t i=0;i<snapshot.count;++i)
            if(snapshot.items[i].name.size==strlen("bare_managed") &&
               !memcmp(snapshot.items[i].name.data,"bare_managed",strlen("bare_managed")))
                record=&snapshot.items[i];
        CHECK(record && record->fields==query.fields &&
              (record->flags==BEAM_MODULE_CURRENT ||
               record->flags==(BEAM_MODULE_CURRENT|BEAM_MODULE_OLD)));
        unsigned generation=metadata_generation(record->current.attributes_etf);
        CHECK(generation>=1 && generation<=2 &&
              !memcmp(record->current.md5,md5[generation-1],16) &&
              has_function(record->exports,"version_two_only",0)==(generation==2) &&
              has_function(record->current.functions,"version_two_only",0)==(generation==2));
        seen|=1u<<(generation-1);
        if(record->flags & BEAM_MODULE_OLD) {
            unsigned old=metadata_generation(record->old.attributes_etf);
            CHECK(old>=1 && old<=2 && old!=generation &&
                  !memcmp(record->old.md5,md5[old-1],16) &&
                  has_function(record->old.functions,"version_two_only",0)==(old==2));
        }
        ++observations;
        beam_module_snapshot_release(&snapshot);
    }
    CHECK(!call_number(vm,"bare_management","stop_code_changes",&after));
    CHECK(observations>=24 && seen==3 && after>before);
    CHECK(!unload_idle(vm,"bare_managed"));
    return 0;
}

static int unload_idle(beam_vm *vm, const char *name)
{
    double deadline = seconds() + 10;
    beam_status status;
    do {
        status = beam_module_unload(vm,name,NULL);
    } while (status == BEAM_BUSY && seconds() < deadline);
    if (status != BEAM_OK) {
        fprintf(stderr,"unload %s: %s\n",name,beam_status_message(status));
        return 1;
    }
    return 0;
}

static int scheduler_inventory(const beam_scheduler_snapshot *snapshot,
                                const beam_scheduler_info *policy)
{
    const beam_scheduler_info *info=&snapshot->info;
    CHECK(info->struct_size==sizeof *info &&
          info->schedulers==policy->schedulers &&
          info->schedulers_online==policy->schedulers_online &&
          info->dirty_cpu_schedulers==policy->dirty_cpu_schedulers &&
          info->dirty_cpu_schedulers_online==policy->dirty_cpu_schedulers_online &&
          info->dirty_io_schedulers==policy->dirty_io_schedulers);
    CHECK(snapshot->items && snapshot->queues &&
          snapshot->count==(size_t)info->schedulers+info->dirty_cpu_schedulers+
                           info->dirty_io_schedulers &&
          snapshot->queue_count==(size_t)info->schedulers+2);
    const beam_scheduler_kind kinds[]={BEAM_SCHEDULER_NORMAL,
        BEAM_SCHEDULER_DIRTY_CPU,BEAM_SCHEDULER_DIRTY_IO};
    const unsigned totals[]={info->schedulers,info->dirty_cpu_schedulers,
        info->dirty_io_schedulers};
    const unsigned online[]={info->schedulers_online,info->dirty_cpu_schedulers_online,
        info->dirty_io_schedulers};
    size_t scheduler=0,queue=0;
    for(unsigned kind=0;kind<3;++kind) {
        for(unsigned index=1;index<=totals[kind];++index) {
            const beam_scheduler_record *item=&snapshot->items[scheduler++];
            CHECK(item->id.kind==kinds[kind] && item->id.index==index &&
                  item->selected_online==(index<=online[kind]) &&
                  item->queue.kind==kinds[kind] &&
                  item->queue.index==(kind?1:index));
        }
        unsigned count=kind?1:totals[kind];
        for(unsigned index=1;index<=count;++index) {
            const beam_scheduler_queue_record *item=&snapshot->queues[queue++];
            CHECK(item->id.kind==kinds[kind] && item->id.index==index);
        }
    }
    CHECK(scheduler==snapshot->count && queue==snapshot->queue_count);
    return 0;
}

static int scheduler_historical(const beam_scheduler_snapshot *snapshot,
                                 const beam_scheduler_info *policy,
                                 const size_t *lengths)
{
    CHECK(!scheduler_inventory(snapshot,policy));
    for(size_t i=0;i<snapshot->queue_count;++i)
        CHECK(snapshot->queues[i].queued_tasks==lengths[i]);
    return 0;
}

static int verify_counts(beam_vm *vm, const beam_vm_options *options,
                         unsigned normal, unsigned dirty, int running)
{
    beam_scheduler_info info = {sizeof info,0};
    OK(beam_scheduler_get_info(vm,&info));
    CHECK(info.schedulers == options->schedulers && info.schedulers_online == normal &&
          info.dirty_cpu_schedulers == options->dirty_cpu_schedulers &&
          info.dirty_cpu_schedulers_online == dirty &&
          info.dirty_io_schedulers == options->dirty_io_schedulers);
    if (running) {
        beam_scheduler_snapshot snapshot={0};
        OK(beam_scheduler_inspect_all(vm,&snapshot));
        CHECK(!scheduler_inventory(&snapshot,&info));
        /* Queue values are real observations, with no required utilization or
         * nonzero/zero condition while the VM continues scheduling. */
        beam_scheduler_snapshot_release(&snapshot);
        beam_result result = {0};
        unsigned values[5];
        size_t p = 3;
        OK(beam_vm_call(vm,"bare_management","counts",empty,&result));
        CHECK(result.kind == BEAM_RETURN && result.value.size >= 3 &&
              !memcmp(result.value.data,(unsigned char[]){131,104,5},3));
        for (unsigned i=0; i<5; ++i)
            CHECK(number(result.value.data,result.value.size,&p,&values[i]));
        CHECK(p == result.value.size && values[0] == info.schedulers &&
              values[1] == normal && values[2] == info.dirty_cpu_schedulers &&
              values[3] == dirty && values[4] == info.dirty_io_schedulers);
        beam_result_release(&result);
    }
    return 0;
}

static int binary_call(beam_vm *vm, const char *function,
                       const unsigned char *data, size_t size)
{
    unsigned char *args = malloc(size + 12);
    CHECK(args && size <= UINT32_MAX);
    memcpy(args,(unsigned char[]){131,108,0,0,0,1,109},7);
    for (unsigned i=0; i<4; ++i) args[7+i] = (unsigned char)(size >> (24-8*i));
    memcpy(args+11,data,size);
    args[size+11] = 106;
    int failed = call_ok(vm,"bare_management",function,(beam_bytes){args,size+12});
    free(args);
    return failed;
}

static size_t gzip_bytes(unsigned char *output, size_t capacity,
                         const unsigned char *input, size_t size)
{
    beam_zlib_stream stream = {0};
    if (size > UINT_MAX || capacity > UINT_MAX ||
        beam_zlib_deflate_init2(&stream, 6, BEAM_ZLIB_DEFLATED, 31, 8,
                               BEAM_ZLIB_DEFAULT_STRATEGY) != BEAM_ZLIB_OK)
        return 0;
    stream.next_in = (unsigned char *)input;
    stream.avail_in = (unsigned)size;
    stream.next_out = output;
    stream.avail_out = (unsigned)capacity;
    int result = beam_zlib_deflate(&stream, BEAM_ZLIB_FINISH);
    size_t written = stream.total_out;
    if (beam_zlib_deflate_end(&stream) != BEAM_ZLIB_OK || result != BEAM_ZLIB_STREAM_END)
        return 0;
    return written;
}

static int gzip_checks(beam_vm *vm, int stress)
{
    size_t raw = sizeof(bare_gzip), capacity = beam_zlib_compress_bound(raw) + 64;
    unsigned char *packet = malloc(8 + raw + capacity * 3);
    CHECK(packet);
    memcpy(packet + 8, bare_gzip, raw);
    size_t gzip = gzip_bytes(packet + 8 + raw, capacity, bare_gzip, raw);
    CHECK(gzip);
    size_t first = gzip_bytes(packet + 8 + raw + gzip, capacity, bare_gzip, raw/2);
    size_t second = gzip_bytes(packet + 8 + raw + gzip + first, capacity, bare_gzip + raw/2, raw - raw/2);
    CHECK(first && second);
    for (unsigned i=0; i<4; ++i) {
        packet[i] = (unsigned char)(raw >> (24-8*i));
        packet[4+i] = (unsigned char)(gzip >> (24-8*i));
    }
    int failed = binary_call(vm,"gzip_checks",packet,8+raw+gzip+first+second);
    free(packet);
    CHECK(!failed);
    packet = malloc(capacity);
    CHECK(packet);
    const unsigned char invalid[] = "gzip containing invalid BEAM";
    gzip = gzip_bytes(packet, capacity, invalid, sizeof(invalid));
    failed = !gzip || binary_call(vm,"gzip_invalid",packet,gzip);
    CHECK(!failed);
    gzip = capacity;
    CHECK(beam_zlib_compress_z(packet, &gzip, bare_gzip, raw) == BEAM_ZLIB_OK);
    failed = binary_call(vm,"gzip_invalid",packet,gzip);
    free(packet);
    CHECK(!failed);
    for (size_t size=16384; size<=32768; size*=2) {
        unsigned char *zeros = calloc(1,size);
        capacity = beam_zlib_compress_bound(size)+64;
        packet = malloc(capacity+4);
        CHECK(zeros && packet);
        gzip = gzip_bytes(packet+4,capacity,zeros,size);
        for (unsigned i=0; i<4; ++i) packet[i]=(unsigned char)(size>>(24-8*i));
        failed = !gzip || binary_call(vm,"gzip_size",packet,gzip+4);
        free(zeros); free(packet);
        CHECK(!failed);
    }
    if (stress) {
        size_t size=64*1024*1024;
        unsigned char *zeros=calloc(1,size);
        capacity=beam_zlib_compress_bound(size)+64;
        packet=malloc(capacity);
        CHECK(zeros && packet);
        gzip=gzip_bytes(packet,capacity,zeros,size);
        free(zeros);
        failed = !gzip || binary_call(vm,"gzip_interrupt",packet,gzip);
        free(packet);
        CHECK(!failed);
    }
    return 0;
}

static int resources_clean(void)
{
    size_t a,b,c,d, vk,sk,vc,sc,blocks,slots,vl,sl;
    beam_runtime_memory_usage(&a,&b,&c,&d);
    ethr_runtime_usage(&vk,&sk,&vc,&sc,&blocks,&slots);
    erts_sys_resource_usage(&vl,&sl);
    CHECK(!a && !b && !c && !d && !vk && !vc && !vl);
    return 0;
}

#include "management_process.h"

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error = {0};
    beam_module_list saved_list = {0};
    beam_module_snapshot saved_snapshot = {0};
    beam_scheduler_snapshot saved_schedulers = {0};
    beam_scheduler_info saved_scheduler_policy = {0};
    beam_process_snapshot saved_process = {0};
    size_t *saved_queue_lengths = NULL;
    beam_result saved_result = {0};
    int baseline = threads(), halt = -1;
    OK(beam_init(&system,&error));
    for (unsigned cycle=0; cycle<12; ++cycle) {
        beam_module_list list = {0};
        beam_module_info info = {sizeof info,0};
        unsigned value;
        beam_vm_options_init(&options);

        options.schedulers = cycle % 2 ? 4 : 8;
        options.schedulers_online = options.schedulers;
        options.dirty_cpu_schedulers = cycle % 2 ? 2 : 4;
        options.async_threads = 1 + cycle % 2;
        options.dirty_io_schedulers = 1 + cycle % 2;
        options.process_limit = 4096 + cycle * 1024;
        options.atom_limit = 8192 + cycle * 1024;
        beam_composition *state_composition = NULL;
        const beam_composition_options state_selection = {.struct_size=sizeof state_selection,
            .builtin_binding_count=BEAM_TEST_MODERN_BINDING_COUNT,.builtin_bindings=beam_test_modern_bindings};
        OK(beam_composition_prepare(&state_selection, &state_composition, &error));
        options.composition = state_composition;
        OK(beam_vm_init(system,&options,&vm,&error));
        beam_composition_release(&state_composition);
        CHECK(threads() == baseline);
        CHECK(!verify_counts(vm,&options,options.schedulers,1,0));
        CHECK(beam_scheduler_set_online(vm,1,1,&error) == BEAM_INVALID_STATE);
        beam_scheduler_record scheduler_sentinel={0};
        beam_scheduler_snapshot unstarted_schedulers={.count=17,.items=&scheduler_sentinel};
        CHECK(beam_scheduler_inspect_all(vm,&unstarted_schedulers)==BEAM_INVALID_STATE &&
              unstarted_schedulers.count==17 && unstarted_schedulers.items==&scheduler_sentinel);
        OK(beam_module_list_names(vm,&list));
        CHECK(list.count == 6 && sorted(&list) && listed(&list,"erlang") &&
              listed(&list,"erts_internal") && listed(&list,"beam_internal") &&
              !listed(&list,"atomics") && !listed(&list,"counters") && !listed(&list,"persistent_term"));
        beam_module_list_release(&list);
        const char *protected[] = {"erlang", "erts_internal", "erts_code_purger",
            "erts_literal_area_collector", "beam_internal", "erts_dirty_process_signal_handler"};
        for (size_t i=0; i<sizeof protected/sizeof protected[0]; ++i) {
            CHECK(!module_flags(vm,protected[i],BEAM_MODULE_CURRENT|BEAM_MODULE_PROTECTED));
            CHECK(beam_module_unload(vm,protected[i],&error) == BEAM_UNSUPPORTED);
        }
        CHECK(beam_module_unload(vm,"never_loaded",&error) == BEAM_NOT_FOUND);
        CHECK(!module_flags(vm,"never_loaded",0));
        const char *bad[] = {"", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
        for (size_t i=0; i<sizeof bad/sizeof bad[0]; ++i) {
            info.flags = 0xdead;
            CHECK(beam_module_get_info(vm,bad[i],&info) == BEAM_INVALID_ARGUMENT &&
                  info.flags == 0xdead);
            CHECK(beam_module_unload(vm,bad[i],NULL) == BEAM_INVALID_ARGUMENT);
        }
        char long_name[257]; memset(long_name,'a',256); long_name[256]=0;
        CHECK(beam_module_get_info(vm,long_name,&info) == BEAM_INVALID_ARGUMENT);
        CHECK(!module_flags(vm,"never_loaded_\xce\xbb_\xe6\xa8\xa1\xe5\x9d\x97",0));
        char unicode_name[4*256+1];
        for (unsigned i=0; i<256; ++i)
            memcpy(unicode_name+4*i,"\xf0\x9f\x9a\x80",4);
        unicode_name[4*255]=0;
        CHECK(!module_flags(vm,unicode_name,0));
        memcpy(unicode_name+4*255,"\xf0\x9f\x9a\x80",4);
        unicode_name[4*256]=0;
        info.flags = 0xdead;
        CHECK(beam_module_get_info(vm,unicode_name,&info) == BEAM_INVALID_ARGUMENT &&
              info.flags == 0xdead);
        info.struct_size = 0;
        CHECK(beam_module_get_info(vm,"erlang",&info) == BEAM_INVALID_ARGUMENT);
        info.struct_size = sizeof info;
        OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        CHECK(fesetround(FE_DOWNWARD) == 0);
        OK(beam_module_unload(vm,"bare_managed",&error));
        CHECK(fegetround() == FE_DOWNWARD);
        CHECK(fesetround(FE_TONEAREST) == 0);
        CHECK(!module_flags(vm,"bare_managed",0));
        CHECK(threads() == baseline);
        OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        OK(beam_module_load(vm,(beam_bytes){bare_management,sizeof bare_management}));
        CHECK(beam_module_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}) == BEAM_ALREADY_LOADED);
        CHECK(!inspect_initial(vm,!cycle ? &saved_snapshot : NULL));
#ifdef BEAM_TEST_SNAPSHOT_ALLOCATIONS
        if(!cycle) CHECK(!snapshot_allocation_failures(vm));
#endif
        CHECK(threads() == baseline);
        CHECK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL) == BEAM_INVALID_STATE);
        CHECK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL) == BEAM_INVALID_STATE);
        CHECK(beam_module_delete(vm,module_name("bare_managed"),NULL) == BEAM_INVALID_STATE);
        if (!cycle) OK(beam_module_list_names(vm,&saved_list));
        OK(beam_vm_set_host_handler(vm,NULL,NULL));
        OK(beam_vm_start(vm,&error));
        int live_threads = threads();
        CHECK(live_threads > baseline);
        CHECK(!verify_counts(vm,&options,options.schedulers,1,0));
        if(!cycle) {
            OK(beam_scheduler_inspect_all(vm,&saved_schedulers));
            saved_scheduler_policy=saved_schedulers.info;
            CHECK(!scheduler_inventory(&saved_schedulers,&saved_scheduler_policy));
            saved_queue_lengths=malloc(saved_schedulers.queue_count*sizeof *saved_queue_lengths);
            CHECK(saved_queue_lengths);
            for(size_t i=0;i<saved_schedulers.queue_count;++i)
                saved_queue_lengths[i]=saved_schedulers.queues[i].queued_tasks;
#ifdef BEAM_TEST_SNAPSHOT_ALLOCATIONS
            CHECK(!scheduler_allocation_failures(vm));
#endif
        }
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        OK(beam_module_list_names(vm,&list));
        beam_module_list_release(&list);
        OK(beam_scheduler_set_online(vm,options.schedulers,1,&error));
        /* Native management does not admit an application call or freeze
         * host-handler registration. */
        OK(beam_vm_set_host_handler(vm,NULL,NULL));
        CHECK(!verify_counts(vm,&options,options.schedulers,1,1));
        if (!cycle) CHECK(!check_process_management(vm,&saved_process));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        if (!cycle) OK(beam_vm_call(vm,"bare_managed","literal",empty,&saved_result));
        CHECK(!gzip_checks(vm, !cycle));

        unsigned before,after;
        CHECK(!call_number(vm,"bare_management","atom_count",&before));
        for (unsigned n=0; n<20; ++n) {
            char name[80]; snprintf(name,sizeof name,"unknown_management_%u_%u",cycle,n);
            CHECK(!module_flags(vm,name,0));
            CHECK(beam_module_unload(vm,name,NULL) == BEAM_NOT_FOUND);
            beam_module_query query; beam_module_query_init(&query);
            beam_module_snapshot missing = {0};
            CHECK(beam_module_inspect(vm,module_name(name),&query,&missing) == BEAM_NOT_FOUND &&
                  !missing.count && !missing.items);
        }
        CHECK(!call_number(vm,"bare_management","atom_count",&after) && before == after);
        static const unsigned char dirty_cpu[] =
            {131,108,0,0,0,1,119,9,'d','i','r','t','y','_','c','p','u',106};
        static const unsigned char dirty_io[] =
            {131,108,0,0,0,1,119,8,'d','i','r','t','y','_','i','o',106};
        beam_bytes dirty_type = cycle % 2
            ? (beam_bytes){dirty_io,sizeof dirty_io}
            : (beam_bytes){dirty_cpu,sizeof dirty_cpu};
        CHECK(!call_ok(vm,"bare_management","start_dirty",dirty_type));
        CHECK(beam_module_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","stop_dirty",empty));
        CHECK(!unload_idle(vm,"bare_managed"));
        OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        CHECK(!call_ok(vm,"bare_managed","start_busy",empty));
        CHECK(beam_module_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","stop_worker",empty));
        CHECK(!call_ok(vm,"bare_managed","start_stack",empty));
        CHECK(beam_module_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!call_ok(vm,"bare_management","stop_worker",empty));
        CHECK(!call_ok(vm,"bare_management","retain",empty));
        CHECK(!unload_idle(vm,"bare_managed"));
        CHECK(!module_flags(vm,"bare_managed",0));
        CHECK(!call_ok(vm,"bare_management","retained",empty));
        CHECK(!call_ok(vm,"bare_management","release",empty));
        OK(beam_module_list_names(vm,&list));
        CHECK(!listed(&list,"bare_managed") && sorted(&list));
        beam_module_list_release(&list);
        OK(beam_module_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 2);
        CHECK(!binary_call(vm,"native_load",bare_managed_1,sizeof bare_managed_1));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT|BEAM_MODULE_OLD));
        CHECK(!unload_idle(vm,"bare_managed"));
        OK(beam_module_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
        CHECK(!binary_call(vm,"native_load",bare_managed_1,sizeof bare_managed_1));
        CHECK(!call_ok(vm,"bare_management","start_purge",empty));
        /* These calls must refuse without waiting for finish_purge, which is
         * a later owner-thread action, or mutating code/the pending purge. */
        CHECK(beam_module_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        beam_module_query busy_query; beam_module_query_init(&busy_query);
        beam_module_snapshot busy_snapshot = {0};
        CHECK(beam_module_inspect(vm,module_name("bare_managed"),&busy_query,&busy_snapshot) == BEAM_BUSY);
        CHECK(beam_module_inspect_all(vm,&busy_query,&busy_snapshot) == BEAM_BUSY);
        CHECK(!busy_snapshot.items && !busy_snapshot.count);
        CHECK(beam_module_get_info(vm,"bare_managed",&info) == BEAM_BUSY);
        CHECK(beam_module_list_names(vm,&list) == BEAM_BUSY && !list.names);
        CHECK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL) == BEAM_BUSY);
        CHECK(beam_module_delete(vm,module_name("bare_managed"),NULL) == BEAM_BUSY);
        CHECK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL) == BEAM_BUSY);
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","finish_purge",empty));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!unload_idle(vm,"bare_managed"));
        unsigned long code_baseline = erts_total_code_size;
        for (unsigned reload=0; reload<6; ++reload) {
            OK(beam_module_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
            CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 2);
            CHECK(!unload_idle(vm,"bare_managed"));
            CHECK(erts_total_code_size == code_baseline);
        }
        CHECK(!hot_management(vm));
        CHECK(!unusual_names(vm));
        if(!cycle) CHECK(!purge_gate_management(vm));
        if(!cycle) CHECK(!concurrent_module_observation(vm));
        CHECK(!binary_call(vm,"pending_on_load",bare_on_load,sizeof bare_on_load));
        OK(beam_module_get_info(vm,"bare_on_load",&info));
        CHECK(info.flags & BEAM_MODULE_ON_LOAD);
        CHECK(beam_module_unload(vm,"bare_on_load",&error) == BEAM_UNSUPPORTED);
        beam_module_query pending_query; beam_module_query_init(&pending_query);
        pending_query.fields = BEAM_MODULE_FIELD_FUNCTIONS | BEAM_MODULE_FIELD_ATTRIBUTES |
                               BEAM_MODULE_FIELD_COMPILE;
        beam_module_snapshot pending = {0};
        OK(beam_module_inspect(vm,module_name("bare_on_load"),&pending_query,&pending));
        CHECK(pending.count == 1 && (pending.items[0].flags & BEAM_MODULE_ON_LOAD) &&
              pending.items[0].pending.loaded_code_bytes > 0 &&
              pending.items[0].pending.function_table_count > 0 &&
              pending.items[0].pending.functions.count > 0);
        beam_module_snapshot_release(&pending);
        for (size_t i=0; i<sizeof protected/sizeof protected[0]; ++i) {
            CHECK(beam_module_delete(vm,module_name(protected[i]),NULL) == BEAM_UNSUPPORTED);
            CHECK(beam_module_soft_purge(vm,module_name(protected[i]),NULL) == BEAM_UNSUPPORTED);
        }

        unsigned n = options.schedulers, d = options.dirty_cpu_schedulers;
        CHECK(beam_scheduler_set_online(vm,0,1,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(strstr(error.message,"schedulers_online"));
        CHECK(beam_scheduler_set_online(vm,n,d+1,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(strstr(error.message,"dirty_cpu_schedulers_online"));
        CHECK(beam_scheduler_set_online(vm,1,2,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(!verify_counts(vm,&options,n,1,1));
        OK(beam_scheduler_set_online(vm,n/2,d-1 ? d-1 : 1,&error));
        CHECK(!verify_counts(vm,&options,n/2,d-1 ? d-1 : 1,1));
        OK(beam_scheduler_set_online(vm,n,1,&error));
        CHECK(!verify_counts(vm,&options,n,1,1));
        OK(beam_scheduler_set_online(vm,n,d,&error));
        OK(beam_scheduler_set_online(vm,n,d,&error));
        CHECK(!verify_counts(vm,&options,n,d,1));
        CHECK(!call_ok(vm,"bare_management","block",empty));
        OK(beam_scheduler_set_online(vm,1,1,&error));
        OK(beam_scheduler_set_online(vm,n,d,&error));
        CHECK(!call_ok(vm,"bare_management","unblock",empty));
        CHECK(!verify_counts(vm,&options,n,d,1));
        CHECK(!call_ok(vm,"bare_management","block_normal",empty));
        OK(beam_scheduler_set_online(vm,2,1,&error));
        CHECK(!call_ok(vm,"bare_management","unblock",empty));
        CHECK(!verify_counts(vm,&options,2,1,1));
        const unsigned char pair[] = {131,108,0,0,0,2,97,3,97,2,106};
        CHECK(!call_ok(vm,"bare_management","set_counts",(beam_bytes){pair,sizeof pair}));
        CHECK(!verify_counts(vm,&options,3,2,1));
        CHECK(!call_ok(vm,"bare_management","start_changes",empty));
        for (unsigned change=0; change<50; ++change) {
            beam_status result = beam_scheduler_set_online(vm,n,d,&error);
            CHECK(result == BEAM_OK || result == BEAM_BUSY);
        }
        CHECK(!call_ok(vm,"bare_management","stop_changes",empty));
        OK(beam_scheduler_set_online(vm,1,1,&error));
        CHECK(!verify_counts(vm,&options,1,1,1));
        CHECK(threads() == live_threads);
        if (cycle == 11) {
            OK(beam_module_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
            OK(beam_module_replace(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2},NULL));
            CHECK(!call_ok(vm,"bare_management","start_dirty",dirty_type));
            CHECK(!call_ok(vm,"bare_management","gate_active_halt",empty));
            CHECK(beam_module_soft_purge(vm,module_name("bare_managed"),NULL)==BEAM_STOPPED);
        } else if (cycle % 3 == 2) {
            CHECK(!call_ok(vm,"bare_management","background_halt",empty));
            double deadline = seconds() + 10;
            beam_status changed;
            do {
                changed = beam_scheduler_set_online(vm,n,d,NULL);
                if (changed == BEAM_STOPPED) break;
                CHECK(changed == BEAM_OK || changed == BEAM_BUSY);
                changed = beam_scheduler_set_online(vm,1,1,NULL);
                CHECK(changed == BEAM_OK || changed == BEAM_BUSY || changed == BEAM_STOPPED);
            } while (changed != BEAM_STOPPED && seconds() < deadline);
            CHECK(changed == BEAM_STOPPED);
        }
        OK(beam_vm_stop(vm,&halt));
        CHECK(halt == (cycle == 11 ? 43 : cycle % 3 == 2 ? 31 : 0));
        beam_scheduler_info snapshot = {sizeof snapshot,0};
        OK(beam_scheduler_get_info(vm,&snapshot));
        CHECK(snapshot.schedulers == n && snapshot.dirty_cpu_schedulers == d);
        CHECK(beam_scheduler_set_online(vm,1,1,NULL) == BEAM_STOPPED);
        beam_scheduler_snapshot stopped_schedulers={.count=17,.items=&scheduler_sentinel};
        CHECK(beam_scheduler_inspect_all(vm,&stopped_schedulers)==BEAM_STOPPED &&
              stopped_schedulers.count==17 && stopped_schedulers.items==&scheduler_sentinel);
        CHECK(!check_process_stopped(vm));
        CHECK(beam_module_get_info(vm,"erlang",&info) == BEAM_STOPPED);
        CHECK(beam_module_list_names(vm,&list) == BEAM_STOPPED && !list.names);
        CHECK(beam_module_unload(vm,"bare_management",NULL) == BEAM_STOPPED);
        beam_module_query stopped_query; beam_module_query_init(&stopped_query);
        beam_module_snapshot stopped_snapshot = {0};
        CHECK(beam_module_inspect_all(vm,&stopped_query,&stopped_snapshot) == BEAM_STOPPED);
        CHECK(beam_module_inspect(vm,module_name("bare_management"),&stopped_query,&stopped_snapshot) == BEAM_STOPPED);
        OK(beam_vm_terminate(&vm,&halt));
        CHECK(!resources_clean());
        int remaining_threads = joined_thread_count(baseline);
        if (remaining_threads != baseline) {
            fprintf(stderr,"cycle %u: threads %d, baseline %d\n",cycle,remaining_threads,baseline);
            diagnose_threads(baseline);
        }
        CHECK(remaining_threads == baseline);
        CHECK(listed(&saved_list,"bare_managed") && sorted(&saved_list));
        CHECK(saved_result.kind == BEAM_RETURN && saved_result.value.size > 3);
        CHECK(saved_snapshot.count == 1 && saved_snapshot.items[0].flags == BEAM_MODULE_CURRENT &&
              has_function(saved_snapshot.items[0].current.functions,"generation",0) &&
              saved_snapshot.items[0].current.attributes_etf.data &&
              saved_snapshot.items[0].current.compile_info_etf.data);
        CHECK(!scheduler_historical(&saved_schedulers,&saved_scheduler_policy,saved_queue_lengths));
    }
#ifdef BEAM_TEST_PROCESS_RETIREMENT
    CHECK(!check_process_stop_retirement(system));
#endif
    OK(beam_terminate(&system));
    CHECK(!check_process_history(&saved_process));
    beam_process_snapshot_release(&saved_process);
    CHECK(!scheduler_historical(&saved_schedulers,&saved_scheduler_policy,saved_queue_lengths));
    beam_scheduler_snapshot_release(&saved_schedulers);
    beam_scheduler_snapshot_release(&saved_schedulers);
    beam_scheduler_snapshot_release(NULL);
    const unsigned char cleared_schedulers[sizeof saved_schedulers]={0};
    CHECK(!memcmp(&saved_schedulers,cleared_schedulers,sizeof saved_schedulers));
    free(saved_queue_lengths);
    CHECK(listed(&saved_list,"bare_managed"));
    CHECK(saved_snapshot.count == 1 &&
          saved_snapshot.items[0].name.size == strlen("bare_managed") &&
          !memcmp(saved_snapshot.items[0].name.data,"bare_managed",strlen("bare_managed")) &&
          has_function(saved_snapshot.items[0].exports,"generation",0));
    beam_module_snapshot_release(&saved_snapshot);
    beam_module_snapshot_release(&saved_snapshot);
    beam_module_snapshot_release(NULL);
    CHECK(!saved_snapshot.items && !saved_snapshot.count);
    beam_module_list_release(&saved_list);
    beam_module_list_release(&saved_list);
    beam_module_list_release(NULL);
    CHECK(!saved_list.names && !saved_list.count);
    beam_result_release(&saved_result);
    puts("Module and scheduler management completed; C continued.");
    return 0;
}
