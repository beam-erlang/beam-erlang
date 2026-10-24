/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <beam.h>
#include "bytecode_input.h"
#include "host_observe.h"
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "facilities line %d: %s (%s)\n", __LINE__, #x, error.message); exit(1); } } while (0)
static beam_error error;
static int entries(const char *path)
{
#ifdef __APPLE__
    return beam_test_host_entries(path);
#else
    DIR *dir = opendir(path); int count = 0; struct dirent *e;
    CHECK(dir);
    while ((e = readdir(dir))) if (e->d_name[0] != '.') ++count;
    closedir(dir); return count;
#endif
}
static beam_buffer read_bytes(const char *path)
{
    FILE *file = fopen(path, "rb"); long n; beam_buffer b = {0};
    CHECK(file && !fseek(file, 0, SEEK_END) && (n = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET));
    b.data = malloc(n); b.size = n;
    CHECK(b.data && fread(b.data, 1, b.size, file) == b.size && !fclose(file));
    return b;
}
typedef struct context {
    beam_vm *vm;
    beam_buffer helper;
    unsigned output, code;
} context;
static beam_status handler(void *opaque, beam_bytes request, beam_host_reply *reply)
{
    context *c = opaque; beam_vm_state_info state;
    CHECK(beam_vm_get_state(c->vm, &state) == BEAM_INVALID_STATE);
    const unsigned char terminal_request[] = {131,119,8,'c','o','m','p','l','e','t','e'};
    if (request.size == sizeof(terminal_request) && !memcmp(request.data,terminal_request,sizeof(terminal_request))) {
        unsigned char invalid[] = {131,97}, value[] = {131,97,42};
        CHECK(beam_host_reply_complete(reply,(beam_bytes){invalid,sizeof(invalid)},6)==BEAM_INVALID_TERM);
        CHECK(beam_host_reply_complete(reply,(beam_bytes){value,sizeof(value)},-1)==BEAM_INVALID_ARGUMENT);
        CHECK(beam_host_reply_complete(reply,(beam_bytes){value,sizeof(value)},6)==BEAM_OK);
        value[2]=99;
        CHECK(beam_host_reply_copy(reply,(beam_bytes){value,sizeof(value)})==BEAM_INVALID_STATE);
        return BEAM_OK;
    }
    CHECK(request.size > 4 && request.data[0] == 131 && request.data[1] == 104);
    const unsigned char *p = request.data + 3;
    size_t n;
    if (*p == 119) { n = p[1]; p += 2; }
    else { CHECK(*p == 118); n = ((unsigned)p[1] << 8) | p[2]; p += 3; }
    CHECK(n <= request.size - (size_t)(p-request.data));
    if (n == 6 && !memcmp(p, "output", 6)) {
        ++c->output;
        const unsigned char ok[] = {131, 119, 2, 'o', 'k'};
        return beam_host_reply_copy(reply, (beam_bytes){ok, sizeof(ok)});
    }
    CHECK(n == 4 && !memcmp(p, "code", 4)); ++c->code;
    size_t size = c->helper.size;
    unsigned char *bytes = malloc(size + 12); CHECK(bytes);
    unsigned char header[] = {131,104,2,119,2,'o','k',109,size>>24,size>>16,size>>8,size};
    memcpy(bytes, header, sizeof(header)); memcpy(bytes+sizeof(header), c->helper.data, size);
    beam_status status = beam_host_reply_copy(reply, (beam_bytes){bytes, size+sizeof(header)});
    free(bytes); return status;
}
static beam_result call(beam_vm *vm, const char *name, beam_bytes args)
{
    beam_result r = {0}; CHECK(beam_vm_call(vm, "facility_worker", name, args, &r) == BEAM_OK); return r;
}
static unsigned integer(const beam_result *r)
{
    CHECK(r->kind == BEAM_RETURN && r->value.size >= 3 && r->value.data[0] == 131);
    const unsigned char *p = r->value.data;
    if (p[1] == 97 && r->value.size == 3) return p[2];
    CHECK(p[1] == 98 && r->value.size == 6);
    return (unsigned)p[2]<<24 | (unsigned)p[3]<<16 | (unsigned)p[4]<<8 | p[5];
}
int main(int argc, char **argv)
{
    CHECK(argc == 5);
    beam_system *system = NULL; beam_composition *composition = NULL;
    beam_buffer code = read_bytes(argv[2]); context c = {.helper = read_bytes(argv[3])};
    beam_module_spec module = {"facility_worker", {code.data, code.size}, "copied:facility-worker"};
    bytecode_input base = {0};
    CHECK(bytecode_input_read(&base, argv[1], INPUT_BASE | INPUT_SERVICES, &error) == BEAM_OK);
    beam_module_spec modules[14];
    memcpy(modules, base.modules, base.count * sizeof(*base.modules)); modules[base.count] = module;
    char zlib_path[4096], helper[4096];
    CHECK(snprintf(zlib_path, sizeof(zlib_path), "%s/" BEAM_TEST_ZLIB_RELATIVE, argv[1]) < (int)sizeof(zlib_path));
    CHECK(snprintf(helper, sizeof(helper), "%s/libexec/beam/erl_child_setup", argv[1]) < (int)sizeof(helper));
    unsigned services = !strcmp(argv[4], "host") ? BEAM_SERVICE_OUTPUT | BEAM_SERVICE_CODE :
        !strcmp(argv[4], "zlib") ? BEAM_SERVICE_ZLIB : BEAM_SERVICE_PROGRAM;
    beam_component_spec components[] = {{sizeof(components[0]), BEAM_COMPONENT_ZLIB, zlib_path}};
    beam_composition_options preparation = {.struct_size=sizeof(preparation), .modules=modules, .module_count=base.count+1,
        .components=components, .component_count=services==BEAM_SERVICE_ZLIB ? 1 : 0, .services=services,
        .program_helper=services==BEAM_SERVICE_PROGRAM ? helper : NULL};
    CHECK(beam_init(&system, &error) == BEAM_OK);
    int descriptors=entries("/proc/self/fd"), threads=entries("/proc/self/task");
    CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
    beam_composition_release(&composition);
    CHECK(entries("/proc/self/fd") == descriptors && entries("/proc/self/task") == threads);
    CHECK(beam_composition_prepare(&preparation, &composition, &error) == BEAM_OK);
    memset(helper, '?', strlen(helper)); /* configuration is already copied */
    free(code.data);
    bytecode_input_release(&base);
    unsigned char empty[]={131,106}, args[]={131,108,0,0,0,1,97,services,106};
    beam_vm_report saved={.struct_size=sizeof(saved)};
    beam_result completed={0};
    for (int pass=0;pass<3;++pass) {
        beam_vm_options options; beam_vm_options_init(&options);
        options.composition=composition;
        CHECK(beam_vm_init(system, &options, &c.vm, &error) == BEAM_OK);
        CHECK(beam_vm_set_host_handler(c.vm, handler, &c) == BEAM_OK);
        CHECK(beam_vm_start(c.vm, &error) == BEAM_OK);
        CHECK(beam_vm_activate(c.vm, &error) == BEAM_OK);
        beam_result r=call(c.vm,"fresh",(beam_bytes){empty,sizeof(empty)});
        CHECK(integer(&r)==42); beam_result_release(&r);
        unsigned pid=0;
        for (int i=0;i<2;++i) {
            r=call(c.vm,"exercise",(beam_bytes){args,sizeof(args)});
            CHECK(integer(&r)==42); beam_result_release(&r);
        }
        if (services==BEAM_SERVICE_PROGRAM) {
            r=call(c.vm,"pid",(beam_bytes){empty,sizeof(empty)});
            pid=integer(&r); CHECK(pid>0 && !kill(pid,0)); beam_result_release(&r);
        }
        if (services==BEAM_SERVICE_ZLIB) CHECK(beam_zlib_stream_count()>0);
        if (pass==0) {
            r=call(c.vm,"cleanup",(beam_bytes){empty,sizeof(empty)});
            CHECK(integer(&r)==42); beam_result_release(&r);
        }
        if (pass==1) {
            const char *name=services==BEAM_SERVICE_PROGRAM ? "program" : services==BEAM_SERVICE_ZLIB ? "zlib" : "output";
            unsigned char fault[64]={131,108,0,0,0,1,119,0}; size_t n=strlen(name);
            fault[7]=n; memcpy(fault+8,name,n); fault[8+n]=106;
            r=call(c.vm,"fail",(beam_bytes){fault,9+n}); CHECK(r.kind==BEAM_HALT); beam_result_release(&r);
            beam_vm_report report={.struct_size=sizeof(report)};
            CHECK(beam_vm_get_report(c.vm,&report)==BEAM_OK && report.cause==BEAM_TERMINAL_COMPONENT_FAILURE &&
                !strcmp(report.component,name) && report.failure.message[0]);
            beam_activation_info info={.struct_size=sizeof(info)};
            CHECK(beam_vm_get_activation(c.vm,&info)==BEAM_OK && info.state==BEAM_ACTIVATION_FAILED);
        }
        if (pass==2) {
            completed=call(c.vm,"complete",(beam_bytes){empty,sizeof(empty)});
            CHECK(completed.kind==BEAM_COMPLETE && completed.halt_status==6 && completed.value.size==3 && completed.value.data[2]==42);
        }
        beam_vm_report report={.struct_size=sizeof(report)};
        CHECK(beam_vm_stop(c.vm,NULL)==BEAM_OK);
        CHECK(beam_vm_get_report(c.vm,&report)==BEAM_OK && report.has_cleanup_error==(pass==0));
        CHECK(beam_vm_terminate_report(&c.vm,NULL,&report)==BEAM_OK && !c.vm && report.has_cleanup_error==(pass==0));
        if (!pass) saved=report;
        if (pass==2) CHECK(report.cause==BEAM_TERMINAL_HOST_REQUEST && report.halt_status==6);
        if (pid) CHECK(kill(pid,0)==-1 && errno==ESRCH);
        struct timespec delay={0,1000000};
        for (int i=0;entries("/proc/self/task")!=threads && i<1000;++i) nanosleep(&delay,NULL);
        CHECK(entries("/proc/self/fd")==descriptors && entries("/proc/self/task")==threads && !beam_zlib_stream_count());
    }
    CHECK(!strcmp(saved.cleanup.message,"facility-cleanup"));
    CHECK(services != (BEAM_SERVICE_OUTPUT|BEAM_SERVICE_CODE) || (c.output==6 && c.code==6));
    beam_composition_release(&composition); free(c.helper.data);
    CHECK(beam_terminate(&system)==BEAM_OK && !strcmp(saved.cleanup.message,"facility-cleanup"));
    CHECK(completed.value.size==3 && completed.value.data[2]==42);
    beam_result_release(&completed);
    puts("persistent facilities passed");
    return 0;
}
