/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <beam/beam.h>
#include <beam/zlib.h>
#include <dirent.h>
#include <fenv.h>
#include <sched.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "fixtures.h"
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
    OK(beam_vm_get_module(vm, name, &info));
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

static int unload_idle(beam_vm *vm, const char *name)
{
    double deadline = seconds() + 10;
    beam_status status;
    do {
        status = beam_vm_unload(vm,name,NULL);
    } while (status == BEAM_BUSY && seconds() < deadline);
    if (status != BEAM_OK) {
        fprintf(stderr,"unload %s: %s\n",name,beam_status_message(status));
        return 1;
    }
    return 0;
}

static int verify_counts(beam_vm *vm, const beam_vm_options *options,
                         unsigned normal, unsigned dirty, int running)
{
    beam_scheduler_info info = {sizeof info,0};
    OK(beam_vm_get_schedulers(vm,&info));
    CHECK(info.schedulers == options->schedulers && info.schedulers_online == normal &&
          info.dirty_cpu_schedulers == options->dirty_cpu_schedulers &&
          info.dirty_cpu_schedulers_online == dirty &&
          info.dirty_io_schedulers == options->dirty_io_schedulers);
    if (running) {
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

int main(void)
{
    beam_system *system = NULL;
    beam_vm *vm = NULL;
    beam_vm_options options;
    beam_error error = {0};
    beam_module_list saved_list = {0};
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
        OK(beam_vm_init(system,&options,&vm,&error));
        CHECK(threads() == baseline);
        CHECK(!verify_counts(vm,&options,options.schedulers,1,0));
        CHECK(beam_vm_set_schedulers_online(vm,1,1,&error) == BEAM_INVALID_STATE);
        OK(beam_vm_list_modules(vm,&list));
        CHECK(list.count == 9 && sorted(&list) && listed(&list,"erlang") &&
              listed(&list,"atomics") && listed(&list,"counters") &&
              listed(&list,"persistent_term"));
        beam_module_list_release(&list);
        const char *protected[] = {"erlang", "atomics", "counters", "persistent_term"};
        for (size_t i=0; i<sizeof protected/sizeof protected[0]; ++i) {
            CHECK(!module_flags(vm,protected[i],BEAM_MODULE_CURRENT|BEAM_MODULE_PROTECTED));
            CHECK(beam_vm_unload(vm,protected[i],&error) == BEAM_UNSUPPORTED);
        }
        CHECK(beam_vm_unload(vm,"never_loaded",&error) == BEAM_NOT_FOUND);
        CHECK(!module_flags(vm,"never_loaded",0));
        const char *bad[] = {"", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
        for (size_t i=0; i<sizeof bad/sizeof bad[0]; ++i) {
            info.flags = 0xdead;
            CHECK(beam_vm_get_module(vm,bad[i],&info) == BEAM_INVALID_ARGUMENT &&
                  info.flags == 0xdead);
            CHECK(beam_vm_unload(vm,bad[i],NULL) == BEAM_INVALID_ARGUMENT);
        }
        char long_name[257]; memset(long_name,'a',256); long_name[256]=0;
        CHECK(beam_vm_get_module(vm,long_name,&info) == BEAM_INVALID_ARGUMENT);
        CHECK(!module_flags(vm,"never_loaded_\xce\xbb_\xe6\xa8\xa1\xe5\x9d\x97",0));
        char unicode_name[4*256+1];
        for (unsigned i=0; i<256; ++i)
            memcpy(unicode_name+4*i,"\xf0\x9f\x9a\x80",4);
        unicode_name[4*255]=0;
        CHECK(!module_flags(vm,unicode_name,0));
        memcpy(unicode_name+4*255,"\xf0\x9f\x9a\x80",4);
        unicode_name[4*256]=0;
        info.flags = 0xdead;
        CHECK(beam_vm_get_module(vm,unicode_name,&info) == BEAM_INVALID_ARGUMENT &&
              info.flags == 0xdead);
        info.struct_size = 0;
        CHECK(beam_vm_get_module(vm,"erlang",&info) == BEAM_INVALID_ARGUMENT);
        info.struct_size = sizeof info;
        OK(beam_vm_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        CHECK(fesetround(FE_DOWNWARD) == 0);
        OK(beam_vm_unload(vm,"bare_managed",&error));
        CHECK(fegetround() == FE_DOWNWARD);
        CHECK(fesetround(FE_TONEAREST) == 0);
        CHECK(!module_flags(vm,"bare_managed",0));
        CHECK(threads() == baseline);
        OK(beam_vm_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        OK(beam_vm_load(vm,(beam_bytes){bare_management,sizeof bare_management}));
        CHECK(beam_vm_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}) == BEAM_ALREADY_LOADED);
        if (!cycle) OK(beam_vm_list_modules(vm,&saved_list));
        OK(beam_vm_set_host_handler(vm,NULL,NULL));
        OK(beam_vm_start(vm,&error));
        int live_threads = threads();
        CHECK(live_threads > baseline);
        CHECK(!verify_counts(vm,&options,options.schedulers,1,0));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        OK(beam_vm_list_modules(vm,&list));
        beam_module_list_release(&list);
        OK(beam_vm_set_schedulers_online(vm,options.schedulers,1,&error));
        /* Native management does not admit an application call or freeze
         * host-handler registration. */
        OK(beam_vm_set_host_handler(vm,NULL,NULL));
        CHECK(!verify_counts(vm,&options,options.schedulers,1,1));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        if (!cycle) OK(beam_vm_call(vm,"bare_managed","literal",empty,&saved_result));
        CHECK(!gzip_checks(vm, !cycle));

        unsigned before,after;
        CHECK(!call_number(vm,"bare_management","atom_count",&before));
        for (unsigned n=0; n<20; ++n) {
            char name[80]; snprintf(name,sizeof name,"unknown_management_%u_%u",cycle,n);
            CHECK(!module_flags(vm,name,0));
            CHECK(beam_vm_unload(vm,name,NULL) == BEAM_NOT_FOUND);
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
        CHECK(beam_vm_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","stop_dirty",empty));
        CHECK(!unload_idle(vm,"bare_managed"));
        OK(beam_vm_load(vm,(beam_bytes){bare_managed_1,sizeof bare_managed_1}));
        CHECK(!call_ok(vm,"bare_managed","start_busy",empty));
        CHECK(beam_vm_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","stop_worker",empty));
        CHECK(!call_ok(vm,"bare_managed","start_stack",empty));
        CHECK(beam_vm_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!call_ok(vm,"bare_management","stop_worker",empty));
        CHECK(!call_ok(vm,"bare_management","retain",empty));
        CHECK(!unload_idle(vm,"bare_managed"));
        CHECK(!module_flags(vm,"bare_managed",0));
        CHECK(!call_ok(vm,"bare_management","retained",empty));
        CHECK(!call_ok(vm,"bare_management","release",empty));
        OK(beam_vm_list_modules(vm,&list));
        CHECK(!listed(&list,"bare_managed") && sorted(&list));
        beam_module_list_release(&list);
        OK(beam_vm_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 2);
        CHECK(!binary_call(vm,"native_load",bare_managed_1,sizeof bare_managed_1));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT|BEAM_MODULE_OLD));
        CHECK(!unload_idle(vm,"bare_managed"));
        OK(beam_vm_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
        CHECK(!binary_call(vm,"native_load",bare_managed_1,sizeof bare_managed_1));
        CHECK(!call_ok(vm,"bare_management","start_purge",empty));
        /* The purger holds staging permission until finish_purge. Unload
         * must refuse immediately instead of waiting for a later C call.
         * Inspection intentionally is not requested while this lock is held. */
        CHECK(beam_vm_unload(vm,"bare_managed",NULL) == BEAM_BUSY);
        CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 1);
        CHECK(!call_ok(vm,"bare_management","finish_purge",empty));
        CHECK(!module_flags(vm,"bare_managed",BEAM_MODULE_CURRENT));
        CHECK(!unload_idle(vm,"bare_managed"));
        unsigned long code_baseline = erts_total_code_size;
        for (unsigned reload=0; reload<6; ++reload) {
            OK(beam_vm_load(vm,(beam_bytes){bare_managed_2,sizeof bare_managed_2}));
            CHECK(!call_number(vm,"bare_managed","generation",&value) && value == 2);
            CHECK(!unload_idle(vm,"bare_managed"));
            CHECK(erts_total_code_size == code_baseline);
        }
        CHECK(!binary_call(vm,"pending_on_load",bare_on_load,sizeof bare_on_load));
        OK(beam_vm_get_module(vm,"bare_on_load",&info));
        CHECK(info.flags & BEAM_MODULE_ON_LOAD);
        CHECK(beam_vm_unload(vm,"bare_on_load",&error) == BEAM_UNSUPPORTED);

        unsigned n = options.schedulers, d = options.dirty_cpu_schedulers;
        CHECK(beam_vm_set_schedulers_online(vm,0,1,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(strstr(error.message,"schedulers_online"));
        CHECK(beam_vm_set_schedulers_online(vm,n,d+1,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(strstr(error.message,"dirty_cpu_schedulers_online"));
        CHECK(beam_vm_set_schedulers_online(vm,1,2,&error) == BEAM_INVALID_ARGUMENT);
        CHECK(!verify_counts(vm,&options,n,1,1));
        OK(beam_vm_set_schedulers_online(vm,n/2,d-1 ? d-1 : 1,&error));
        CHECK(!verify_counts(vm,&options,n/2,d-1 ? d-1 : 1,1));
        OK(beam_vm_set_schedulers_online(vm,n,1,&error));
        CHECK(!verify_counts(vm,&options,n,1,1));
        OK(beam_vm_set_schedulers_online(vm,n,d,&error));
        OK(beam_vm_set_schedulers_online(vm,n,d,&error));
        CHECK(!verify_counts(vm,&options,n,d,1));
        CHECK(!call_ok(vm,"bare_management","block",empty));
        OK(beam_vm_set_schedulers_online(vm,1,1,&error));
        OK(beam_vm_set_schedulers_online(vm,n,d,&error));
        CHECK(!call_ok(vm,"bare_management","unblock",empty));
        CHECK(!verify_counts(vm,&options,n,d,1));
        CHECK(!call_ok(vm,"bare_management","block_normal",empty));
        OK(beam_vm_set_schedulers_online(vm,2,1,&error));
        CHECK(!call_ok(vm,"bare_management","unblock",empty));
        CHECK(!verify_counts(vm,&options,2,1,1));
        const unsigned char pair[] = {131,108,0,0,0,2,97,3,97,2,106};
        CHECK(!call_ok(vm,"bare_management","set_counts",(beam_bytes){pair,sizeof pair}));
        CHECK(!verify_counts(vm,&options,3,2,1));
        CHECK(!call_ok(vm,"bare_management","start_changes",empty));
        for (unsigned change=0; change<50; ++change) {
            beam_status result = beam_vm_set_schedulers_online(vm,n,d,&error);
            CHECK(result == BEAM_OK || result == BEAM_BUSY);
        }
        CHECK(!call_ok(vm,"bare_management","stop_changes",empty));
        OK(beam_vm_set_schedulers_online(vm,1,1,&error));
        CHECK(!verify_counts(vm,&options,1,1,1));
        CHECK(threads() == live_threads);
        if (cycle % 3 == 2) {
            CHECK(!call_ok(vm,"bare_management","background_halt",empty));
            double deadline = seconds() + 10;
            beam_status changed;
            do {
                changed = beam_vm_set_schedulers_online(vm,n,d,NULL);
                if (changed == BEAM_STOPPED) break;
                CHECK(changed == BEAM_OK || changed == BEAM_BUSY);
                changed = beam_vm_set_schedulers_online(vm,1,1,NULL);
                CHECK(changed == BEAM_OK || changed == BEAM_BUSY || changed == BEAM_STOPPED);
            } while (changed != BEAM_STOPPED && seconds() < deadline);
            CHECK(changed == BEAM_STOPPED);
        }
        OK(beam_vm_stop(vm,&halt));
        CHECK(halt == (cycle % 3 == 2 ? 31 : 0));
        beam_scheduler_info snapshot = {sizeof snapshot,0};
        OK(beam_vm_get_schedulers(vm,&snapshot));
        CHECK(snapshot.schedulers == n && snapshot.dirty_cpu_schedulers == d);
        CHECK(beam_vm_set_schedulers_online(vm,1,1,NULL) == BEAM_STOPPED);
        CHECK(beam_vm_get_module(vm,"erlang",&info) == BEAM_STOPPED);
        CHECK(beam_vm_list_modules(vm,&list) == BEAM_STOPPED && !list.names);
        CHECK(beam_vm_unload(vm,"bare_management",NULL) == BEAM_STOPPED);
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
    }
    OK(beam_terminate(&system));
    CHECK(listed(&saved_list,"bare_managed"));
    beam_module_list_release(&saved_list);
    beam_module_list_release(&saved_list);
    beam_module_list_release(NULL);
    CHECK(!saved_list.names && !saved_list.count);
    beam_result_release(&saved_result);
    puts("Module and scheduler management completed; C continued.");
    return 0;
}
