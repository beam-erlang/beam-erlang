/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <beam/beam.h>
#include "native_counts.h"
#include <beam/boot.h>
#include "bytecode_input.h"
#include "host_observe.h"
#include <unistd.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"contracts line %d: %s (%s)\n",__LINE__,#x,error.message); exit(1); } } while (0)
static beam_error error;
static int entries(const char *path) {
#ifdef __APPLE__
    return beam_test_host_entries(path);
#else
    DIR *d=opendir(path); struct dirent *e; int n=0; CHECK(d);
    while ((e=readdir(d))) if(e->d_name[0]!='.') ++n;
    closedir(d); return n;
#endif
}
static beam_buffer read_bytes(const char *path) {
    FILE *f=fopen(path,"rb"); long n; beam_buffer b={0};
    CHECK(f && !fseek(f,0,SEEK_END) && (n=ftell(f))>0 && !fseek(f,0,SEEK_SET));
    b.data=malloc(n);b.size=n;CHECK(b.data && fread(b.data,1,n,f)==(size_t)n && !fclose(f));return b;
}
static unsigned outputs;
static beam_status handler(void *ctx, beam_bytes request, beam_host_reply *reply) {
    (void)ctx;
    unsigned char ok[]={131,119,2,'o','k'};
    unsigned char fail[]={131,104,2,119,5,'e','r','r','o','r',119,2,'i','o'};
    unsigned char code[]={131,104,2,119,2,'o','k',109,0,0,0,12,'n','o','t',' ','b','y','t','e','c','o','d','e'};
    CHECK(request.size>10);
    struct timespec delay={0,40000000}; nanosleep(&delay,NULL);
    /* The first tuple atom is output or code. ETF uses small UTF-8 atoms. */
    const unsigned char *p=request.data+3;
    size_t n = p[0]==119 ? p[1] : ((size_t)p[1]<<8)|p[2];
    p += p[0]==119 ? 2 : 3;
    if (n==6 && !memcmp(p,"output",6)) {
        ++outputs;
        return beam_host_reply_copy(reply,(beam_bytes){outputs%2 ? ok : fail,outputs%2 ? sizeof(ok) : sizeof(fail)});
    }
    CHECK(n==4 && !memcmp(p,"code",4));
    return beam_host_reply_copy(reply,(beam_bytes){code,sizeof(code)});
}
static void check_result(const beam_result *r) {
    if (r->kind!=BEAM_RETURN || r->value.size!=3 || r->value.data[2]!=42) {
        fprintf(stderr,"contract result kind %d: ",r->kind);
        if(r->value.data) fwrite(r->value.data,1,r->value.size,stderr);
        fputc('\n',stderr);exit(1);
    }
}
int main(int argc,char **argv) {
    CHECK(argc==6);
    const int faults=strstr(argv[4],"faults")!=NULL, returning=strstr(argv[4],"returning")!=NULL;
    beam_buffer bytes=read_bytes(argv[2]);
    beam_module_spec module={"contract_worker",{bytes.data,bytes.size},"copied:contracts"};
    bytecode_input base = {0}; CHECK(bytecode_input_read(&base, argv[1], returning ? INPUT_RETURNING : INPUT_BASE | INPUT_SERVICES, &error)==BEAM_OK);
    beam_module_spec modules[14]; memcpy(modules,base.modules,base.count * sizeof(*base.modules)); modules[base.count]=module;
    beam_return_module returning_modules[14]={{0}};
    for(size_t i=0;i<base.count+1;++i) {
        returning_modules[i].name=modules[i].name; returning_modules[i].bytes=modules[i].bytes;
        returning_modules[i].origin=modules[i].origin;
    }
    char paths[4][4096];
    const char *names[]={"file","network"};
    beam_library *libraries[2]={0};
    for(int i=0;i<2;++i) {
        CHECK(snprintf(paths[i],sizeof(paths[i]),"%s/%s",argv[1],beam_test_component(names[i]))<(int)sizeof(paths[i]));
        CHECK(beam_library_open(i==0&&faults?argv[5]:paths[i],&libraries[i],&error)==BEAM_OK);
    }
    size_t size=strlen(argv[3]); unsigned char *arg=calloc(1,size+12); CHECK(arg);
    unsigned char header[]={131,108,0,0,0,1,109,size>>24,size>>16,size>>8,size};
    memcpy(arg,header,sizeof(header));memcpy(arg+sizeof(header),argv[3],size);arg[size+11]=106;
    beam_system *system=NULL; CHECK(beam_init(&system,&error)==BEAM_OK);
    int descriptors=entries("/proc/self/fd"),threads=entries("/proc/self/task");
    beam_result saved={0};
    beam_vm_report saved_cleanup={0};
    for(int pass=0;pass<3;++pass) {
        if(returning) {
            beam_return_options options={.struct_size=sizeof(options)};
            beam_startup_action startup[]={{"beam_file","start",{0}},{"beam_socket","start",{0}}};
            beam_host_options plan={.struct_size=sizeof(plan),.library_count=2,.libraries=libraries,.startup_count=2,.startup=startup};
            options.host=&plan;
            options.modules=returning_modules;options.module_count=base.count+1;
            options.entry_module="contract_worker";options.entry_function=faults ? "returning_fault" : "returning";options.arguments=(beam_bytes){arg,size+12};
            beam_return_boot *boot=NULL; beam_outcome out={.struct_size=sizeof(out)};
            CHECK(beam_return_prepare(&options,&boot,&error)==BEAM_OK);
            CHECK(beam_return_run(system,&boot,&out,&error)==BEAM_OK && !boot);
            if(out.kind!=BEAM_OUTCOME_RETURN) { fprintf(stderr,"returning contract kind %d\n",out.kind); if(out.term.data) fwrite(out.term.data,1,out.term.size,stderr); exit(1); }
            CHECK(out.term.size==3 && out.term.data[2]==42 && out.has_cleanup_error==faults);
            if(faults) CHECK(strstr(out.cleanup_error,"file close failed"));
            beam_outcome_release(&out);
        } else {
            beam_composition_options prep={.struct_size=sizeof(prep),.modules=modules,.module_count=base.count+1};
            beam_startup_action startup[]={{"beam_file","start",{0}},{"beam_socket","start",{0}},{"beam_output","start",{0}},{"beam_code","start",{0}}};
            beam_host_options plan={.struct_size=sizeof(plan),.composition=&prep,.library_count=2,.libraries=libraries,.startup_count=4,.startup=startup};
            beam_host *comp=NULL;beam_vm *vm=NULL;beam_vm_options options;beam_vm_options_init(&options);
            CHECK(beam_host_prepare(&plan,&comp,&error)==BEAM_OK);
            options.composition=beam_host_composition(comp);options.dirty_io_schedulers=1;
            CHECK(beam_vm_init(system,&options,&vm,&error)==BEAM_OK);
            CHECK(beam_vm_set_host_handler(vm,handler,NULL)==BEAM_OK);
            CHECK(beam_vm_start(vm,&error)==BEAM_OK && beam_host_start(comp,vm,&error)==BEAM_OK);
            beam_host_release(&comp);
            const char *calls[]={"files","sockets","callbacks","native_faults","abandon","busy","cleanup_fail"};
            for(int i=0;i<7;++i) {
                if((i==3 || i>=5) && !faults) continue;
                if(i==6 && pass!=1) continue;
                beam_result r={0}; CHECK(beam_vm_call(vm,"contract_worker",calls[i],(beam_bytes){arg,size+12},&r)==BEAM_OK);
                check_result(&r);
                if(!pass && i==0) saved=r; else beam_result_release(&r);
            }
            beam_vm_report report={.struct_size=sizeof(report)};
            CHECK(beam_vm_terminate_report(&vm,NULL,&report)==BEAM_OK && !vm && report.has_cleanup_error==(faults && pass==1));
            if(faults) {
                struct stat info; CHECK(!stat(argv[3],&info) && info.st_size==65536);
                if(pass==1) { CHECK(report.cleanup.native_code==EIO); saved_cleanup=report; }
            }
        }
        CHECK(!beam_file_descriptor_count() && !beam_socket_count());
        struct timespec delay={0,1000000};
        for(int i=0;i<1000 && entries("/proc/self/task")!=threads;++i)nanosleep(&delay,NULL);
        CHECK(entries("/proc/self/fd")==descriptors && entries("/proc/self/task")==threads);
        if(saved.value.data) check_result(&saved);
    }
    CHECK(beam_terminate(&system)==BEAM_OK);if(saved.value.data)check_result(&saved);
    if(faults && !returning) CHECK(saved_cleanup.cleanup.native_code==EIO && strstr(saved_cleanup.cleanup.message,"file close failed"));
    beam_result_release(&saved);free(bytes.data);free(arg);bytecode_input_release(&base);
    for(size_t i=0;i<2;++i)beam_library_release(&libraries[i]);
    puts("service contracts passed");return 0;
}
