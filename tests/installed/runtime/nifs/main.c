#include <beam.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#ifdef __APPLE__
#include "host_observe.h"
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"native check failed line %d: %s (%s)\n",__LINE__,#x,error.message); return 1; } } while(0)
static beam_status check_mapping(void *context, beam_bytes request, beam_host_reply *reply) {
    (void)context; (void)request;
#ifdef __APPLE__
    if (beam_test_host_runtime_images() != 0) return BEAM_INVALID_STATE;
#else
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return BEAM_INVALID_STATE;
    char line[4096];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libbeam.so")) { fclose(maps); return BEAM_INVALID_STATE; }
    }
    fclose(maps);
#endif
    const unsigned char ok[] = {131,119,2,'o','k'};
    return beam_host_reply_copy(reply, (beam_bytes){ok,sizeof(ok)});
}
int main(int argc,char **argv) {
    beam_error error={0}; beam_system *system=NULL; beam_vm *vm=NULL; beam_composition *composition=NULL;
    CHECK(argc==4 || argc==6);
    FILE *f=fopen(argv[1],"rb"); CHECK(f); CHECK(!fseek(f,0,SEEK_END)); long size=ftell(f); CHECK(size>0);
    rewind(f); unsigned char *bytes=malloc(size); CHECK(bytes && fread(bytes,1,size,f)==(size_t)size); fclose(f);
    beam_module_spec module={"probe",{bytes,(size_t)size},argv[1]};
    beam_nif_spec nif={sizeof(nif),"probe",argv[2]};
    beam_composition_options spec={.struct_size=sizeof(spec),.module_count=1,.modules=&module,.nif_count=1,.nifs=&nif};
    beam_module_spec partial_modules[2]; beam_nif_spec partial_nifs[2];
    unsigned char *extra=NULL;
    if(argc==6) {
        FILE *second=fopen(argv[4],"rb"); CHECK(second);
        CHECK(!fseek(second,0,SEEK_END)); long length=ftell(second); CHECK(length>0); rewind(second);
        extra=malloc(length); CHECK(extra && fread(extra,1,length,second)==(size_t)length); fclose(second);
        partial_modules[0]=module;
        partial_modules[1]=(beam_module_spec){"probe2",{extra,(size_t)length},argv[4]};
        partial_nifs[0]=nif; partial_nifs[1]=(beam_nif_spec){sizeof(nif),"probe2",argv[5]};
        spec.modules=partial_modules;spec.module_count=2;spec.nifs=partial_nifs;spec.nif_count=2;
    }
    if (!strcmp(argv[3], "ordinary-foreign-runtime")) {
        spec.nif_count = 0; spec.native_admission = BEAM_NATIVE_NIFS;
        CHECK(beam_composition_prepare(&spec, &composition, &error) == BEAM_OK);
        CHECK(beam_init(&system, &error) == BEAM_OK);
        size_t n = strlen(argv[2]); unsigned char *arg = malloc(n+12); CHECK(arg);
        const unsigned char head[] = {131,108,0,0,0,1,109}; memcpy(arg,head,7);
        arg[7]=(unsigned char)(n>>24); arg[8]=(unsigned char)(n>>16);
        arg[9]=(unsigned char)(n>>8); arg[10]=(unsigned char)n;
        memcpy(arg+11,argv[2],n); arg[11+n]=106;
        beam_entry entry = {"probe", "foreign_runtime", {arg,n+12}};
        beam_vm_options options; beam_vm_options_init(&options);
        options.execution = BEAM_PROCESS_OWNED; options.initial_root = &entry;
        options.composition = composition;
        CHECK(beam_vm_init(system, &options, &vm, &error) == BEAM_OK);
        free(bytes); free(arg); beam_composition_release(&composition);
        CHECK(beam_vm_set_host_handler(vm, check_mapping, NULL) == BEAM_OK);
        beam_vm_enter(vm, &error);
        CHECK(!"Process-owned native rejection returned");
    }
    beam_status status=beam_composition_prepare(&spec,&composition,&error);
    if (!strcmp(argv[3],"foreign-runtime")) {
        CHECK(status==BEAM_INVALID_ARGUMENT && !composition);
        CHECK(!strcmp(error.message,"native library depends on another BEAM runtime image"));
#ifdef __APPLE__
        CHECK(beam_test_host_runtime_images() == 0);
#else
        FILE *maps=fopen("/proc/self/maps","r"); CHECK(maps);
        char line[4096];
        while(fgets(line,sizeof(line),maps)) CHECK(!strstr(line,"libbeam.so"));
        fclose(maps);
#endif
        free(bytes); return 0;
    }
    if (!strcmp(argv[3],"prepare-failure")) { CHECK(status!=BEAM_OK && !composition); free(bytes); return 0; }
    CHECK(status==BEAM_OK); free(bytes); free(extra);
    beam_nif_spec duplicate[]={nif,nif};
    beam_composition *invalid=NULL;
    spec.nifs=duplicate; spec.nif_count=2;
    /* The accepted composition owns copied bytes; inspect its inventory. */
    const beam_module_spec *copied; size_t count;
    CHECK(beam_composition_get_modules(composition,&copied,&count)==BEAM_OK);
    spec.modules=copied;
    CHECK(beam_composition_prepare(&spec,&invalid,&error)==BEAM_INVALID_ARGUMENT && !invalid);
    CHECK(beam_init(&system,&error)==BEAM_OK);
    const unsigned char args[]={131,106};
    for(int i=0;i<2;++i) {
        beam_vm_options options; beam_vm_options_init(&options); options.composition=composition;
        CHECK(beam_vm_init(system,&options,&vm,&error)==BEAM_OK);
        beam_activation_info activation={.struct_size=sizeof(activation)};
        CHECK(beam_vm_get_activation(vm,&activation)==BEAM_OK && activation.state==BEAM_ACTIVATION_PREPARED);
        CHECK(beam_vm_start(vm,&error)==BEAM_OK);
        beam_result result={0};
        CHECK(beam_vm_call(vm,"probe","retain",(beam_bytes){args,sizeof(args)},&result)==BEAM_INVALID_STATE);
        status=beam_vm_activate(vm,&error);
        if (!strcmp(argv[3],"activation-failure") || !strcmp(argv[3],"partial")) {
            CHECK(status!=BEAM_OK);
            CHECK(beam_vm_get_activation(vm,&activation)==BEAM_OK && activation.state==BEAM_ACTIVATION_FAILED && activation.failure.message[0]);
            beam_vm_report report={.struct_size=sizeof(report)};
            CHECK(beam_vm_get_report(vm,&report)==BEAM_OK && report.cause==BEAM_TERMINAL_COMPONENT_FAILURE &&
                  !strcmp(report.component,!strcmp(argv[3],"partial") ? "probe2" : "probe") && report.failure.message[0]);
            CHECK(beam_vm_call(vm,"probe","retain",(beam_bytes){args,sizeof(args)},&result)!=BEAM_OK);
        }
        else {
            CHECK(status==BEAM_OK);
            CHECK(beam_vm_get_activation(vm,&activation)==BEAM_OK && activation.state==BEAM_ACTIVATION_ACTIVE);
            CHECK(beam_vm_call(vm,"probe","packets",(beam_bytes){args,sizeof(args)},&result)==BEAM_OK);
            if(result.kind!=BEAM_RETURN) {
                fprintf(stderr,"packet result kind=%d ETF=",result.kind);
                for(size_t j=0;j<result.value.size;++j) fprintf(stderr,"%02x",result.value.data[j]);
                fputc('\n',stderr);
            }
            CHECK(result.kind==BEAM_RETURN); beam_result_release(&result);
            if (!strncmp(argv[3],"fatal-",6)) {
                struct rlimit core_limit = {0,0};
                CHECK(setrlimit(RLIMIT_CORE,&core_limit)==0);
                CHECK(strlen(argv[3])==7 && argv[3][6]>='0' && argv[3][6]<='2');
                unsigned char fatal_args[]={131,108,0,0,0,1,97,(unsigned char)(argv[3][6]-'0'),106};
                beam_vm_call(vm,"probe","fatal",(beam_bytes){fatal_args,sizeof(fatal_args)},&result);
                CHECK(!"native failure returned");
            }
            CHECK(beam_vm_call(vm,"probe","retain",(beam_bytes){args,sizeof(args)},&result)==BEAM_OK);
            CHECK(result.kind==BEAM_RETURN); beam_result_release(&result);
            CHECK(beam_vm_unload(vm,"probe",&error)==BEAM_BUSY);
            size_t n=strlen(argv[2]); unsigned char *arg=malloc(n+12); CHECK(arg);
            unsigned char head[]={131,108,0,0,0,1,109}; memcpy(arg,head,7);
            arg[7]=(unsigned char)(n>>24);arg[8]=(unsigned char)(n>>16);arg[9]=(unsigned char)(n>>8);arg[10]=(unsigned char)n;
            memcpy(arg+11,argv[2],n);arg[11+n]=106;
            CHECK(beam_vm_call(vm,"probe","implicit",(beam_bytes){arg,n+12},&result)==BEAM_OK);
            int denied=0;
            for(size_t j=0;j+13<=result.value.size;++j) if(!memcmp(result.value.data+j,"not_supported",13)) denied=1;
            CHECK(denied); free(arg); beam_result_release(&result);
        }
        if(i==1) beam_composition_release(&composition);
        beam_vm_report final={.struct_size=sizeof(final)};
        CHECK(beam_vm_terminate_report(&vm,NULL,&final)==BEAM_OK);
        CHECK(!final.has_cleanup_error);
        CHECK(final.cause==(!strcmp(argv[3],"success") ? BEAM_TERMINAL_NONE : BEAM_TERMINAL_COMPONENT_FAILURE));
    }
    CHECK(beam_terminate(&system)==BEAM_OK);
    puts("NATIVE_LIFETIMES_OK"); return 0;
}
