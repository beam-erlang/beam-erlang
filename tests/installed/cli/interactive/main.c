/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "beam_cli_recipe.h"
#include "native_counts.h"
#include <dirent.h>
#include <errno.h>
#include <fenv.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include "host_observe.h"
#endif

/* Distribution-owned observations, not additional installed SDK contracts. */
extern void beam_runtime_memory_usage(size_t *, size_t *, size_t *, size_t *);
extern void ethr_runtime_usage(size_t *, size_t *, size_t *, size_t *, size_t *, size_t *);
extern void erts_sys_resource_usage(size_t *, size_t *);

typedef struct buffer { unsigned char *data; size_t size, capacity; } buffer;
typedef struct output { buffer out, err; } output;
typedef struct resources {
    size_t allocations, bytes, mappings, mapped_bytes;
    size_t vm_keys, system_keys, vm_callbacks, system_callbacks, blocks, slots;
    size_t vm_locks, system_locks;
} resources;
typedef struct host_state {
    int fds[1024], fd_count, threads, flags[3], round, tty;
    struct stat standard[3];
    struct termios terminal;
    struct sigaction signals[NSIG];
    unsigned char signal_valid[NSIG];
    sigset_t mask;
    char cwd[4096], children[4096];
} host_state;
static const char *current_case = "initialization";

static void check(int condition, const char *message)
{
    if (!condition) { fprintf(stderr, "interactive acceptance: %s\n", message); exit(1); }
}
static void append(buffer *b, const void *bytes, size_t size)
{
    check(size <= (size_t)-1 - b->size, "buffer overflow");
    if (b->size + size > b->capacity) {
        b->capacity = b->size + size + 256;
        b->data = realloc(b->data, b->capacity);
        check(b->data != NULL, "buffer allocation");
    }
    if (size) memcpy(b->data + b->size, bytes, size);
    b->size += size;
}
static void byte(buffer *b, unsigned value) { unsigned char v = (unsigned char)value; append(b,&v,1); }
static void u32(buffer *b, size_t value)
{
    check(value <= 0xffffffffu, "ETF size");
    byte(b,(unsigned)(value>>24)); byte(b,(unsigned)(value>>16));
    byte(b,(unsigned)(value>>8)); byte(b,(unsigned)value);
}
static void atom(buffer *b, const char *name)
{ size_t n=strlen(name); check(n<256,"atom size"); byte(b,119); byte(b,(unsigned)n); append(b,name,n); }
static void binary(buffer *b, const void *bytes, size_t size)
{ byte(b,109); u32(b,size); append(b,bytes,size); }
static void text_binary(buffer *b, const char *text) { binary(b,text,strlen(text)); }
static buffer arguments(const char *root, const char *expression)
{
    buffer b={0};
    byte(&b,131); byte(&b,108); u32(&b,1); byte(&b,104); byte(&b,5);
    text_binary(&b,root); byte(&b,106);
    if (expression) { byte(&b,104); byte(&b,2); atom(&b,"eval"); text_binary(&b,expression); }
    else atom(&b,"shell");
    byte(&b,104); byte(&b,2); byte(&b,97); byte(&b,80); byte(&b,97); byte(&b,24);
    atom(&b,"false"); byte(&b,106);
    return b;
}
static buffer read_file(const char *path)
{
    FILE *file=fopen(path,"rb"); buffer b={0}; unsigned char part[4096]; size_t n;
    check(file!=NULL,path);
    while ((n=fread(part,1,sizeof part,file))) append(&b,part,n);
    check(!ferror(file),"read program"); check(fclose(file)==0,"close program"); return b;
}
static beam_output_status on_output(void *context, beam_bytes destination, beam_bytes bytes)
{
    output *o=context;
    if (destination.size==15 && !memcmp(destination.data,"standard_output",15)) append(&o->out,bytes.data,bytes.size);
    else if (destination.size==14 && !memcmp(destination.data,"standard_error",14)) append(&o->err,bytes.data,bytes.size);
    else return BEAM_OUTPUT_UNKNOWN;
    return BEAM_OUTPUT_OK;
}
typedef struct input { const char *script; size_t cursor; } input;
static beam_input_status on_input(void *context, const unsigned char **bytes, size_t *size)
{
    input *in=context; const char *start=in->script+in->cursor, *end;
    if (!*start) { *bytes=NULL; *size=0; return BEAM_INPUT_EOF; }
    end=strchr(start,'\n'); *size=end ? (size_t)(end-start)+1 : strlen(start);
    *bytes=(const unsigned char *)start; in->cursor+=*size; return BEAM_INPUT_LINE;
}
static int entries(const char *path)
{
#ifdef __APPLE__
    int count=beam_test_host_entries(path); check(count>=0,path); return count;
#else
    DIR *d=opendir(path); struct dirent *entry; int count=0;
    check(d!=NULL,path);
    while ((entry=readdir(d))) if (entry->d_name[0]!='.') ++count;
    closedir(d); return count;
#endif
}
static int compare_int(const void *a,const void *b)
{ return (*(const int*)a > *(const int*)b)-(*(const int*)a < *(const int*)b); }
static int descriptors(int *values)
{
#ifdef __APPLE__
    int count=beam_test_host_copy_fds(values,1024); check(count>=0,"descriptor census"); return count;
#else
    DIR *d=opendir("/proc/self/fd"); struct dirent *entry; int count=0;
    check(d!=NULL,"descriptor census");
    while ((entry=readdir(d))) {
        char *end; long fd=strtol(entry->d_name,&end,10);
        if (*end || fd<0 || fd==dirfd(d)) continue;
        check(count<1024,"descriptor capacity"); values[count++]=(int)fd;
    }
    closedir(d); qsort(values,(size_t)count,sizeof(int),compare_int); return count;
#endif
}
static void signal_handler(int signal) { (void)signal; }
static void children(char *out,size_t capacity)
{
#ifdef __APPLE__
    check(beam_test_host_child_text(out,capacity)==0,"child census");
#else
    char path[128]; FILE *file; size_t count;
    snprintf(path,sizeof path,"/proc/self/task/%ld/children",(long)getpid());
    file=fopen(path,"r"); check(file!=NULL,"child census");
    count=fread(out,1,capacity-1,file); check(!ferror(file) && feof(file),"child census capacity");
    out[count]='\0'; fclose(file);
#endif
}
static void snapshot(host_state *s)
{
    struct sigaction action={0}; action.sa_handler=signal_handler; sigemptyset(&action.sa_mask);
    check(sigaction(SIGUSR2,&action,NULL)==0,"install host signal handler");
    for (int signal=1;signal<NSIG;++signal)
        s->signal_valid[signal]=sigaction(signal,NULL,&s->signals[signal])==0;
    check(sigprocmask(SIG_SETMASK,NULL,&s->mask)==0,"host signal mask");
    check(fesetround(FE_DOWNWARD)==0,"host rounding mode"); s->round=fegetround();
    check(setenv("BEAM_INTERACTIVE_HOST_SENTINEL","host-owned",1)==0,"host environment");
    check(getcwd(s->cwd,sizeof s->cwd)!=NULL,"host cwd");
    children(s->children,sizeof s->children);
    s->fd_count=descriptors(s->fds); s->threads=entries("/proc/self/task");
    for (int i=0;i<3;++i) { s->flags[i]=fcntl(i,F_GETFL); check(fstat(i,&s->standard[i])==0,"standard descriptor"); }
    s->tty=tcgetattr(STDIN_FILENO,&s->terminal)==0;
}
static void observe(resources *r)
{
    beam_runtime_memory_usage(&r->allocations,&r->bytes,&r->mappings,&r->mapped_bytes);
    ethr_runtime_usage(&r->vm_keys,&r->system_keys,&r->vm_callbacks,&r->system_callbacks,&r->blocks,&r->slots);
    erts_sys_resource_usage(&r->vm_locks,&r->system_locks);
}
static void quiescent(const host_state *s, const resources *steady, int terminated)
{
    resources r={0}; int fds[1024],count; char cwd[4096],child_pids[4096];
    struct sigaction action; sigset_t mask; struct timespec delay={0,1000000};
    observe(&r);
    check(!r.allocations && !r.bytes && !r.mappings && !r.mapped_bytes &&
          !r.vm_keys && !r.vm_callbacks && !r.vm_locks,"VM allocations remain");
    check(!beam_file_descriptor_count() && !beam_socket_count() && !beam_zlib_stream_count(),"native resources remain");
    if (steady) check(r.system_keys==steady->system_keys && r.system_callbacks==steady->system_callbacks &&
                      r.system_locks==steady->system_locks,"system resource growth");
    if (terminated) check(!r.system_keys && !r.system_callbacks && !r.system_locks && !r.blocks && !r.slots,"system resources remain");
    /* Joined Linux tasks can remain visible briefly after clear_child_tid. */
    for (unsigned i=0;entries("/proc/self/task")!=s->threads && i<1000;++i) nanosleep(&delay,NULL);
    check(entries("/proc/self/task")==s->threads,"VM workers remain");
    children(child_pids,sizeof child_pids);
    if (strcmp(child_pids,s->children)) fprintf(stderr,"%s: remaining child processes: %s\n",current_case,child_pids);
    check(!strcmp(child_pids,s->children),"VM child processes remain");
    count=descriptors(fds);
    if (count!=s->fd_count || memcmp(fds,s->fds,(size_t)count*sizeof(int))) {
        fprintf(stderr,"descriptor baseline:");
        for (int i=0;i<s->fd_count;++i) fprintf(stderr," %d",s->fds[i]);
        fprintf(stderr,"\ndescriptors after:");
        for (int i=0;i<count;++i) {
            char link[64],target[512]; ssize_t length;
            snprintf(link,sizeof link,"/proc/self/fd/%d",fds[i]);
            length=readlink(link,target,sizeof target-1);
            if (length>=0) target[length]='\0'; else strcpy(target,"unknown");
            fprintf(stderr," %d:%s",fds[i],target);
        }
        fputc('\n',stderr);
    }
    check(count==s->fd_count && !memcmp(fds,s->fds,(size_t)count*sizeof(int)),"descriptor set changed");
    for (int i=0;i<3;++i) {
        struct stat current; check(fstat(i,&current)==0,"standard descriptor closed");
        check(current.st_dev==s->standard[i].st_dev && current.st_ino==s->standard[i].st_ino &&
              fcntl(i,F_GETFL)==s->flags[i],"standard descriptor changed");
    }
    check(fegetround()==s->round,"host rounding mode changed");
    check(getenv("BEAM_INTERACTIVE_HOST_SENTINEL") && !strcmp(getenv("BEAM_INTERACTIVE_HOST_SENTINEL"),"host-owned"),"host environment changed");
    check(getcwd(cwd,sizeof cwd) && !strcmp(cwd,s->cwd),"host cwd changed");
    for (int signal=1;signal<NSIG;++signal) if (s->signal_valid[signal]) {
        /* libc may add its internal SA_RESTORER flag while restoring a
         * disposition. Compare the public flags that define host semantics. */
        unsigned public_flags=SA_NOCLDSTOP|SA_NOCLDWAIT|SA_NODEFER|SA_ONSTACK|
                              SA_RESETHAND|SA_RESTART|SA_SIGINFO;
        check(sigaction(signal,NULL,&action)==0,"read host signal handler");
        if (action.sa_handler!=s->signals[signal].sa_handler ||
            ((unsigned)action.sa_flags & public_flags)!=((unsigned)s->signals[signal].sa_flags & public_flags)) {
            fprintf(stderr,"host signal %d changed after %s: flags %#x -> %#x, handler changed=%d\n",
                    signal,current_case,s->signals[signal].sa_flags,action.sa_flags,
                    action.sa_handler!=s->signals[signal].sa_handler);
            check(0,"host signal handler changed");
        }
        for (int bit=1;bit<NSIG;++bit)
            check(sigismember(&action.sa_mask,bit)==sigismember(&s->signals[signal].sa_mask,bit),"host signal action mask changed");
    }
    check(sigprocmask(SIG_SETMASK,NULL,&mask)==0,"read signal mask");
    for (int i=1;i<NSIG;++i) check(sigismember(&mask,i)==sigismember(&s->mask,i),"host signal mask changed");
    if (s->tty) {
        struct termios tty; check(tcgetattr(STDIN_FILENO,&tty)==0,"terminal unavailable");
        check(tty.c_iflag==s->terminal.c_iflag && tty.c_oflag==s->terminal.c_oflag &&
              tty.c_cflag==s->terminal.c_cflag && tty.c_lflag==s->terminal.c_lflag &&
              !memcmp(tty.c_cc,s->terminal.c_cc,NCCS),"terminal settings changed");
    }
}
static int contains(const buffer *b, const char *text)
{
    size_t size=strlen(text);
    for (size_t i=0;i+size<=b->size;++i) if (!memcmp(b->data+i,text,size)) return 1;
    return 0;
}
static beam_outcome run(beam_system *system, const char *root, const char *expression,
                        const char *script, const beam_return_module *modules,
                        size_t module_count, unsigned services, output *o, int failure)
{
    beam_cli_recipe_return_options options={0}; beam_return_boot *boot=NULL;
    beam_error error={0}; beam_status status; beam_outcome result={0};
    buffer args=arguments(root,expression); input in={script ? script : "",0};
    char rejected_path[4096]; buffer rejected={0}; beam_return_module activation[2];
    current_case=expression ? expression : script;
    options.struct_size=sizeof options; options.runtime_root=root; options.services=services;
    options.profile=BEAM_CLI_INTERACTIVE; options.entry_module="beam_interactive"; options.entry_function="main";
    options.arguments=(beam_bytes){args.data,args.size};
    options.modules=modules; options.module_count=module_count;
    options.output=on_output; options.output_context=o;
    options.input=on_input; options.input_context=&in;
    if (failure==1) {
        const char *origin=modules[0].origin,*slash=strrchr(origin,'/'); check(slash!=NULL,"fixture directory");
        check(snprintf(rejected_path,sizeof rejected_path,"%.*s/interactive_prestart_rejected.beam",(int)(slash-origin),origin)<(int)sizeof rejected_path,"activation fixture path");
        rejected=read_file(rejected_path);
        activation[0]=modules[0];
        activation[1]=(beam_return_module){"interactive_prestart_rejected",{rejected.data,rejected.size},1,rejected_path};
        options.modules=activation; options.module_count=2;
    }
    status=beam_cli_recipe_return_prepare(&options,&boot,&error); free(args.data);
    if (status!=BEAM_OK) { fprintf(stderr,"prepare: %s\n",error.message); check(0,"prepare interactive"); }
    free(rejected.data);
    result.struct_size=sizeof result; status=beam_return_run(system,&boot,&result,&error);
    if (failure==2) check(status==BEAM_UNSUPPORTED,"prestart on_load was not rejected");
    else if (status!=BEAM_OK) { fprintf(stderr,"run: %s\n",error.message); check(0,"run interactive"); }
    check(!boot,"run did not consume preparation");
    check(!result.has_cleanup_error,"interactive cleanup failed");
    return result;
}
static void copied_result(const beam_outcome *result)
{
    const char *text="copied interactive result"; size_t n=strlen(text);
    check(result->kind==BEAM_OUTCOME_RETURN && result->term.size==n+6 &&
          result->term.data[0]==131 && result->term.data[1]==109 &&
          result->term.data[2]==0 && result->term.data[3]==0 && result->term.data[4]==0 &&
          result->term.data[5]==n && !memcmp(result->term.data+6,text,n),"copied interactive result");
}
static void child_retired(const output *o)
{
    const char *marker="child "; char *text=malloc(o->out.size+1),*start,*end; long pid;
    check(text!=NULL,"child observation allocation");
    memcpy(text,o->out.data,o->out.size); text[o->out.size]='\0';
    start=strstr(text,marker); check(start!=NULL,"child process observation");
    errno=0; pid=strtol(start+strlen(marker),&end,10);
    check(!errno && pid>0 && pid<=0x7fffffff && *end=='\n',"child PID observation");
    check(contains(&o->out,"interactive resources held\n"),"resource readiness");
    errno=0; check(kill((pid_t)pid,0)==-1 && errno==ESRCH,"interactive child was not reaped");
    free(text);
}
int main(int argc,char **argv)
{
    host_state host={0}; resources steady={0}; beam_system *system=NULL; beam_error error={0};
    beam_outcome saved={0},result={0}; buffer program,rejected; output o={0};
    beam_return_module modules[2]={{0},{0}};
    char retained[8192],expression[16384],script[16384],rejected_path[4096];
    unsigned all=BEAM_CLI_OUTPUT|BEAM_CLI_CODE|BEAM_CLI_FILE|BEAM_CLI_SOCKET|BEAM_CLI_ZLIB|BEAM_CLI_PROGRAM;
    const char *endings[]={"",", erlang:halt(7)",", erlang:error(interactive_failure)",
        ", exit(group_leader(),kill), receive after 3000 -> error(owner_alive) end"};
    check(argc==4,"usage: host PREFIX FIXTURE_BEAM WORK_FILE_OR_minimal");
    {
        char loaded[4096],expected[4096],canonical[4096]; size_t required=0;
        check(snprintf(expected,sizeof expected,"%s/" BEAM_TEST_RUNTIME_RELATIVE,argv[1])<(int)sizeof expected,"runtime library path");
        check(beam_library_path(loaded,sizeof loaded,&required,&error)==BEAM_OK,error.message);
        check(realpath(expected,canonical) && !strcmp(loaded,canonical),"host uses its deployed libbeam");
    }
    program=read_file(argv[2]);
    modules[0].name="interactive_probe"; modules[0].origin=argv[2];
    modules[0].bytes=(beam_bytes){program.data,program.size};
    snapshot(&host); check(beam_init(&system,&error)==BEAM_OK,error.message);
    saved=run(system,argv[1],"interactive_probe:clean().",NULL,modules,1,BEAM_CLI_OUTPUT,&o,0);
    check(!o.out.size && !o.err.size,"eval printed its return value");
    copied_result(&saved); quiescent(&host,NULL,0); observe(&steady);
    {
        const char *scripts[]={"interactive_probe:clean().\nq().\n", "interactive_probe:clean().\n",
            "erlang:error(recoverable).\ninteractive_probe:clean().\nq().\n"};
        for (unsigned i=0;i<3;++i) {
            o.out.size=o.err.size=0;
            result=run(system,argv[1],NULL,scripts[i],modules,1,BEAM_CLI_OUTPUT,&o,0);
            if (result.kind!=BEAM_OUTCOME_FINISH || result.halt_status!=0) {
                fprintf(stderr,"shell script %u: outcome=%d status=%d service=%s\n",i,
                        result.kind,result.halt_status,result.origin[0] ? result.origin : "none");
                fwrite(o.out.data,1,o.out.size,stderr); fwrite(o.err.data,1,o.err.size,stderr);
            }
            check(result.kind==BEAM_OUTCOME_FINISH && result.halt_status==0,"shell quit/EOF status");
            check(contains(&o.out,"copied interactive result"),"shell did not evaluate input");
            if (i==2) check(contains(&o.out,"recoverable") || contains(&o.err,"recoverable"),"shell error not observed");
            beam_outcome_release(&result); quiescent(&host,&steady,0); copied_result(&saved);
        }
    }
    if (strcmp(argv[3],"minimal")) {
        const char *slash=strrchr(argv[2],'/'); check(slash!=NULL,"fixture directory");
        check(snprintf(rejected_path,sizeof rejected_path,"%.*s/interactive_prestart_rejected.beam",(int)(slash-argv[2]),argv[2])<(int)sizeof rejected_path,"prestart fixture path");
        rejected=read_file(rejected_path);
        modules[1].name="interactive_prestart_rejected"; modules[1].origin=rejected_path;
        modules[1].bytes=(beam_bytes){rejected.data,rejected.size};
        result=run(system,argv[1],"ok.",NULL,modules,2,all,&o,2);
        check(!result.term.data,"prestart failure produced a result");
        beam_outcome_release(&result); free(rejected.data); quiescent(&host,&steady,0);
        o.out.size=o.err.size=0;
        result=run(system,argv[1],"ok.",NULL,modules,1,all,&o,1);
        check(result.kind==BEAM_OUTCOME_FAILURE && !strcmp(result.origin,"interactive_prestart_rejected"),"deferred activation failure");
        check(!o.out.size && !o.err.size,"activation failure evaluated user input");
        beam_outcome_release(&result); quiescent(&host,&steady,0);
        /* Paths generated by the Python harness contain Unicode but no Erlang
         * quoting characters. They are explicit user data, never runtime lookup. */
        check(!strchr(argv[3],'"') && !strchr(argv[3],'\\'),"fixture path quoting");
        check(snprintf(retained,sizeof retained,"interactive_probe:retain(<<\"%s\"/utf8>>)",argv[3])<(int)sizeof retained,"retained expression");
        for (unsigned shell=0;shell<2;++shell) for (unsigned i=0;i<4;++i) {
            check(snprintf(expression,sizeof expression,"%s%s.",retained,endings[i])<(int)sizeof expression,"eval expression");
            check(snprintf(script,sizeof script,"%s\n%s",expression,i==0 ? "q().\n" : i==2 ? "q().\n" : "")<(int)sizeof script,"shell script");
            o.out.size=o.err.size=0;
            result=run(system,argv[1],shell ? NULL : expression,script,modules,1,all,&o,0);
            child_retired(&o);
            if (i==0 && !shell) copied_result(&result);
            else if (i==1) check(result.kind==BEAM_OUTCOME_HALT && result.halt_status==7,"interactive halt status");
            else if (i==3) check(result.kind==BEAM_OUTCOME_FAILURE && !strcmp(result.origin,"output"),"interactive owner death");
            else check(result.kind==BEAM_OUTCOME_FINISH && result.halt_status==(shell ? 0 : 1),"interactive completion status");
            beam_outcome_release(&result); quiescent(&host,&steady,0); copied_result(&saved);
            o.out.size=o.err.size=0;
            result=run(system,argv[1],"interactive_probe:clean().",NULL,modules,1,BEAM_CLI_OUTPUT,&o,0);
            copied_result(&result); check(!o.out.size && !o.err.size,"fresh evaluator state");
            beam_outcome_release(&result); quiescent(&host,&steady,0);
        }
    }
    check(beam_terminate(&system)==BEAM_OK && !system,"terminate system");
    quiescent(&host,NULL,1); copied_result(&saved);
    printf("interactive lifetimes ok: threads=%d descriptors=%d files=0 sockets=0 streams=0\n",
           entries("/proc/self/task"),host.fd_count);
    beam_outcome_release(&saved); free(program.data); free(o.out.data); free(o.err.data); return 0;
}
