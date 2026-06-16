// SPDX-License-Identifier: Apache-2.0
// Product-owned Android fixture; all VM activity stays on the Java owner thread.
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <beam/beam.h>
#include <beam/sdk/inventories.h>
#include "package_inventory.h"
#include "registration.h"
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <fenv.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace {
std::mutex admission;
std::condition_variable changed;
beam_vm *wake_vm;
bool active, teardown;
unsigned commands;
using Clock=std::chrono::steady_clock;
struct HostContext {
    static constexpr int signals[]={SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGTRAP,SIGPIPE,SIGUSR1,SIGUSR2};
    struct sigaction actions[8]{};
    sigset_t mask{};
    int rounding=0;
    bool captured=false;
    void capture() {
        for(unsigned i=0;i<8;i++)if(sigaction(signals[i],nullptr,&actions[i]))throw std::runtime_error("cannot observe Android signal handler");
        if(pthread_sigmask(SIG_SETMASK,nullptr,&mask))throw std::runtime_error("cannot observe owner signal mask");
        rounding=fegetround();captured=true;
    }
    void verify() const {
        if(!captured)return;
        for(unsigned i=0;i<8;i++) {
            struct sigaction current{};
            if(sigaction(signals[i],nullptr,&current) || current.sa_handler!=actions[i].sa_handler || current.sa_flags!=actions[i].sa_flags)
                throw std::runtime_error("BEAM changed an Android process signal handler");
            for(int signal=1;signal<NSIG;signal++)if(sigismember(&current.sa_mask,signal)!=sigismember(&actions[i].sa_mask,signal))
                throw std::runtime_error("BEAM changed an Android handler signal mask");
        }
        sigset_t current{};
        if(pthread_sigmask(SIG_SETMASK,nullptr,&current))throw std::runtime_error("cannot observe owner signal mask");
        for(int signal=1;signal<NSIG;signal++)if(sigismember(&current,signal)!=sigismember(&mask,signal))
            throw std::runtime_error("BEAM changed the Java owner thread signal mask");
        if(fegetround()!=rounding)throw std::runtime_error("BEAM changed the Java owner floating-point mode");
    }
};
struct Owner {
    JNIEnv *env;
    jobject application;
    jmethodID publish;
    AAssetManager *assets;
    pthread_t thread;
    std::string root, directory, runtime;
    std::vector<std::string> images;
    beam_system *system=nullptr;
    beam_vm *vm=nullptr;
    beam_error error{};
    HostContext host_context;
    unsigned generation=0, events=0, cycles=0, cleanups=0;
    long acknowledged=0;
    bool verified=false;
};
std::string quote(const std::string &value) {
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') {out+='\\';out+=c;}
        else if(c<32) {char code[7];snprintf(code,sizeof code,"\\u%04x",c);out+=code;}
        else out+=c;
    }
    return out+'"';
}
int entries(const char *path) {
    DIR *directory=opendir(path);if(!directory)return -1;int count=0;
    while(auto *entry=readdir(directory)) if(entry->d_name[0]!='.')count++;
    closedir(directory);return count;
}
void report(Owner &o,const char *state,const std::string &error="") {
    if(!pthread_equal(o.thread,pthread_self()))throw std::runtime_error("callback escaped owner thread");
    std::ostringstream json;
    json<<"{\"state\":"<<quote(state)<<",\"error\":"<<quote(error)
        <<",\"abi\":"<<quote(BEAM_ANDROID_ABI)<<",\"flavor\":"<<quote(BEAM_ANDROID_FLAVOR)
        <<",\"sdk_build_id\":"<<quote(beam_build_id())<<",\"runtime_image\":"<<quote(o.runtime)
        <<",\"page_size\":"<<sysconf(_SC_PAGESIZE)<<",\"generation\":"<<o.generation
        <<",\"cleanups\":"<<o.cleanups<<",\"events\":"<<o.events<<",\"cycles\":"<<o.cycles<<",\"acknowledged\":"<<o.acknowledged
        <<",\"bytecode_verified\":"<<(o.verified?"true":"false")
        <<",\"threads\":"<<entries("/proc/self/task")<<",\"descriptors\":"<<entries("/proc/self/fd")<<"}";
    jstring text=o.env->NewStringUTF(json.str().c_str());
    if(!text)throw std::runtime_error("JNI report allocation failed");
    o.env->CallVoidMethod(o.application,o.publish,text);o.env->DeleteLocalRef(text);
    if(o.env->ExceptionCheck()) {o.env->ExceptionClear();throw std::runtime_error("JNI report callback raised");}
}
void require(Owner &o,beam_status status,const char *operation) {
    if(status!=BEAM_OK)throw std::runtime_error(std::string(operation)+": "+beam_status_message(status)+" "+o.error.message);
}
std::vector<unsigned char> asset(Owner &o,const std::string &path) {
    AAsset *input=AAssetManager_open(o.assets,(o.root+path).c_str(),AASSET_MODE_STREAMING);
    if(!input)throw std::runtime_error("missing asset: "+path);
    const auto size=AAsset_getLength64(input);
    if(size<0 || size>64*1024*1024) {AAsset_close(input);throw std::runtime_error("invalid asset size");}
    std::vector<unsigned char> bytes(size);size_t offset=0;
    while(offset<bytes.size()) {
        int n=AAsset_read(input,bytes.data()+offset,bytes.size()-offset);
        if(n<=0) {AAsset_close(input);throw std::runtime_error("truncated asset: "+path);}
        offset+=n;
    }
    AAsset_close(input);return bytes;
}
void atomic_file(const std::string &path,const void *bytes,size_t size) {
    std::string temporary=path+".pending";int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
    if(fd<0)throw std::runtime_error("cannot create private file");
    size_t offset=0;while(offset<size) {ssize_t n=write(fd,(const char *)bytes+offset,size-offset);if(n<=0){close(fd);unlink(temporary.c_str());throw std::runtime_error("private file write failed");}offset+=n;}
    int synced=fsync(fd);int closed=close(fd);
    if(synced || closed || rename(temporary.c_str(),path.c_str())) {unlink(temporary.c_str());throw std::runtime_error("private file commit failed");}
    std::string parent=path.substr(0,path.rfind('/'));fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("private directory open failed");synced=fsync(fd);close(fd);if(synced)throw std::runtime_error("private directory sync failed");
}
void persist(Owner &o) {
    std::string value=std::to_string(o.acknowledged+1)+"\n";
    atomic_file(o.directory+"/acknowledged",value.data(),value.size());o.acknowledged++;
}
beam_status callback(void *context,beam_bytes request,beam_host_reply *reply) {
    auto &o=*(Owner *)context;
    const unsigned char expected[]={131,119,10,'i','d','l','e','_','e','v','e','n','t'};
    const unsigned char ok[]={131,119,2,'o','k'};
    if(!pthread_equal(o.thread,pthread_self()) || request.size!=sizeof expected || memcmp(request.data,expected,sizeof expected))return BEAM_INVALID_TERM;
    o.events++;return beam_host_reply_copy(reply,{ok,sizeof ok});
}
void prepare(Owner &o) {
    const beam_bytecode_inventory *inventories[]={beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode,beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode,&beam_package_inventory};
    size_t count=0;for(auto i:inventories)count+=i->count;
    std::vector<std::vector<unsigned char>> bytes;bytes.reserve(count);
    std::vector<std::string> origins;origins.reserve(count);
    std::vector<beam_module_spec> modules;modules.reserve(count);
    for(auto inventory:inventories) {
        size_t first=modules.size();
        for(size_t i=0;i<inventory->count;i++) {
            auto &record=inventory->records[i];bytes.push_back(asset(o,record.relative_path));origins.push_back(o.root+record.relative_path);
            modules.push_back({record.name,{bytes.back().data(),bytes.back().size()},origins.back().c_str()});
        }
        require(o,beam_bytecode_verify(inventory,modules.data()+first,inventory->count,&o.error),"verify bytecode");
    }
    o.verified=true;
    std::vector<beam_component_spec> components;
    const beam_component_kind kinds[]={BEAM_COMPONENT_FILE,BEAM_COMPONENT_NETWORK,BEAM_COMPONENT_ZLIB};
    size_t image=1;
    for(unsigned i=0;i<3;i++) if(BEAM_FIXTURE_COMPONENTS & (1<<i))
        components.push_back({sizeof(beam_component_spec),kinds[i],o.images.at(image++).c_str()});
    std::vector<beam_nif_spec> shared;
    for(size_t i=0;i<beam_package_shared_nif_count;i++)
        shared.push_back({sizeof(beam_nif_spec),beam_package_shared_nif_modules[i],o.images.at(image++).c_str()});
    std::vector<beam_linked_nif_spec> nifs(beam_package_linked_nifs(nullptr,0));beam_package_linked_nifs(nifs.data(),nifs.size());
    beam_composition_options spec{};spec.struct_size=sizeof spec;spec.modules=modules.data();spec.module_count=modules.size();spec.components=components.data();spec.component_count=components.size();spec.nifs=shared.data();spec.nif_count=shared.size();spec.linked_nifs=nifs.data();spec.linked_nif_count=nifs.size();
    beam_composition *composition=nullptr;require(o,beam_composition_prepare(&spec,&composition,&o.error),"prepare composition");
    beam_vm_options options;beam_vm_options_init(&options);options.composition=composition;options.schedulers=2;options.dirty_cpu_schedulers=1;options.dirty_io_schedulers=1;
    auto status=beam_vm_init(o.system,&options,&o.vm,&o.error);beam_composition_release(&composition);require(o,status,"initialize VM");
    require(o,beam_vm_set_host_handler(o.vm,callback,&o),"host handler");require(o,beam_vm_start(o.vm,&o.error),"start VM");require(o,beam_vm_activate(o.vm,&o.error),"activate NIFs");
    {std::lock_guard<std::mutex> lock(admission);wake_vm=o.vm;}
    o.host_context.verify();
    ++o.generation;
}
void stop(Owner &o) {
    {std::lock_guard<std::mutex> lock(admission);wake_vm=nullptr;}
    if(o.vm) {
        beam_vm_report cleanup{};cleanup.struct_size=sizeof cleanup;
        require(o,beam_vm_terminate_report(&o.vm,nullptr,&cleanup),"terminate VM");
        if(cleanup.has_cleanup_error)throw std::runtime_error("VM cleanup error");
        if(beam_file_descriptor_count() || beam_socket_count() || beam_zlib_stream_count())
            throw std::runtime_error("VM teardown retained component resources");
        ++o.cleanups;
    }
    o.host_context.verify();
}
std::vector<unsigned char> arguments(const std::string &first,const std::string &second) {
    std::vector<unsigned char> out={131,108,0,0,0,3};
    for(const auto &text:{first,second}) {uint32_t n=text.size();out.insert(out.end(),{109,(unsigned char)(n>>24),(unsigned char)(n>>16),(unsigned char)(n>>8),(unsigned char)n});out.insert(out.end(),text.begin(),text.end());}
    out.insert(out.end(),{97,BEAM_FIXTURE_COMPONENTS,106});return out;
}
void execute(Owner &o) {
    char image[4096];size_t needed=0;require(o,beam_library_path(image,sizeof image,&needed,&o.error),"runtime image");o.runtime=image;
    if(o.runtime!=o.images[0])throw std::runtime_error("runtime does not match explicit installed APK selection");
    if(strcmp(beam_build_id(),beam_package_inventory.build_id))throw std::runtime_error("runtime SDK identity mismatch");
    o.host_context.capture();
    beam_system_options options;beam_system_options_init(&options);options.owner=BEAM_SYSTEM_CALLING_THREAD;
    require(o,beam_init_with_options(&options,&o.system,&o.error),"initialize system");
    o.host_context.verify();
    FILE *saved=fopen((o.directory+"/acknowledged").c_str(),"r");
    if(saved) {long value=-1;int parsed=fscanf(saved,"%ld",&value);fclose(saved);if(parsed!=1 || value<0)throw std::runtime_error("invalid persisted state");o.acknowledged=value;}
    auto resource=asset(o,FIXTURE_RESOURCE);atomic_file(o.directory+"/payload.txt",resource.data(),resource.size());
    bool calling=false,completed=false;unsigned event_baseline=0;
    struct Result { beam_result value{}; ~Result(){beam_result_release(&value);} } owned_result;
    beam_result &result=owned_result.value;auto deadline=Clock::now();
    for(;;) {
        bool foreground,stop_requested;
        {std::lock_guard<std::mutex> lock(admission);foreground=active;stop_requested=teardown;teardown=false;}
        if(!foreground || stop_requested) {
            if(o.vm){stop(o);calling=false;completed=false;beam_result_release(&result);report(o,"inactive");}
            if(!foreground) {std::unique_lock<std::mutex> lock(admission);changed.wait_for(lock,std::chrono::milliseconds(100),[]{return active;});continue;}
        }
        if(!o.vm) {
            prepare(o);event_baseline=o.events;auto args=arguments(o.directory+"/payload.txt",o.directory+"/file-proof");
            require(o,beam_vm_call_begin(o.vm,"android_acceptance","exercise",{args.data(),args.size()}),"begin call");
            if(beam_vm_call_begin(o.vm,"android_acceptance","exercise",{args.data(),args.size()})!=BEAM_BUSY)throw std::runtime_error("busy admission accepted");
            std::fill(args.begin(),args.end(),0);calling=true;completed=false;deadline=Clock::now()+std::chrono::seconds(15);report(o,"running");
        }
        beam_pump_result pumped{};pumped.struct_size=sizeof pumped;
        require(o,beam_vm_pump(o.vm,20,4,&pumped),"pump");
        if(calling) {
            auto status=beam_vm_call_take(o.vm,&result);
            if(status==BEAM_OK) {
                if(result.kind!=BEAM_RETURN || result.value.size!=3 || result.value.data[0]!=131 || result.value.data[1]!=97 || result.value.data[2]!=99)throw std::runtime_error("unexpected Erlang outcome");
                calling=false;completed=true;
            } else if(status!=BEAM_BUSY)require(o,status,"take call");
        }
        if(completed && o.events>event_baseline) {
            persist(o);++o.cycles;completed=false;report(o,"passed");
            if(o.cycles<3) {stop(o);if(result.value.size!=3 || result.value.data[2]!=99)throw std::runtime_error("copied result did not survive teardown");beam_result_release(&result);continue;}
            beam_result_release(&result);
        }
        if((calling || completed) && Clock::now()>deadline)throw std::runtime_error("bounded call or idle callback timed out");
    }
}
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_erlangsters_acceptance_android_AcceptanceApplication_nativeImageNames(JNIEnv *env,jclass) {
    std::vector<const char *> names={"libbeam.so"};
    const char *components[]={"libbeam-file.so","libbeam-network.so","libbeam-zlib.so"};
    for(unsigned i=0;i<3;i++)if(BEAM_FIXTURE_COMPONENTS & (1<<i))names.push_back(components[i]);
    for(size_t i=0;i<beam_package_shared_nif_count;i++)names.push_back(beam_package_shared_nif_libraries[i]);
    jclass type=env->FindClass("java/lang/String");if(!type)return nullptr;
    jobjectArray out=env->NewObjectArray(names.size(),type,nullptr);env->DeleteLocalRef(type);
    if(!out)return nullptr;
    for(size_t i=0;i<names.size();i++){jstring name=env->NewStringUTF(names[i]);if(!name)return nullptr;env->SetObjectArrayElement(out,i,name);env->DeleteLocalRef(name);}
    return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_org_erlangsters_acceptance_android_AcceptanceApplication_nativeAbi(JNIEnv *env,jclass) {return env->NewStringUTF(BEAM_ANDROID_ABI);}
extern "C" JNIEXPORT void JNICALL Java_org_erlangsters_acceptance_android_AcceptanceApplication_nativeCommand(JNIEnv *,jclass,jint command) {
    std::lock_guard<std::mutex> lock(admission);if(command==0){active=false;teardown=true;}else if(command==1)active=true;else if(command==2)teardown=true;
    ++commands;if(wake_vm)beam_vm_wake(wake_vm);changed.notify_one();
}
extern "C" JNIEXPORT void JNICALL Java_org_erlangsters_acceptance_android_AcceptanceApplication_nativeRun(JNIEnv *env,jobject application,jobject assets,jstring directory,jobjectArray images) {
    Owner o{};o.env=env;o.application=application;o.assets=AAssetManager_fromJava(env,assets);o.thread=pthread_self();o.root=std::string("beam/")+BEAM_ANDROID_ABI+"/";
    jclass type=env->GetObjectClass(application);o.publish=env->GetMethodID(type,"onNativeReport","(Ljava/lang/String;)V");env->DeleteLocalRef(type);
    const char *text=env->GetStringUTFChars(directory,nullptr);if(!text)return;o.directory=text;env->ReleaseStringUTFChars(directory,text);
    o.images.resize(env->GetArrayLength(images));
    for(size_t i=0;i<o.images.size();i++){jstring item=(jstring)env->GetObjectArrayElement(images,i);text=env->GetStringUTFChars(item,nullptr);if(!text)return;o.images[i]=text;env->ReleaseStringUTFChars(item,text);env->DeleteLocalRef(item);}
    try {execute(o);}catch(const std::exception &failure){
        try {stop(o);report(o,"failed",failure.what());}catch(...){ }
        if(o.system)beam_terminate(&o.system);
    }
}
