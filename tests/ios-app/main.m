#include <beam/host.h>
// SPDX-License-Identifier: Apache-2.0
// Product-owned UIKit fixture. No playground sources or runtime-private headers.
#import <UIKit/UIKit.h>
#include <stdatomic.h>
#include <pthread.h>
#include <mach/mach.h>
#include <beam/beam.h>
#include <beam/sdk/inventories.h>
#include "package_inventory.h"
#include "registration.h"

static atomic_int active,calling,heartbeats,teardown_requested;
static pthread_mutex_t wake_lock=PTHREAD_MUTEX_INITIALIZER;
static beam_vm *wake_vm;
static void set_active(int value) {
    if(!value) atomic_store(&teardown_requested,1);
    atomic_store(&active,value);pthread_mutex_lock(&wake_lock);
    if(wake_vm) beam_vm_wake(wake_vm);
    pthread_mutex_unlock(&wake_lock);
}
static int thread_count(void) {
    thread_act_array_t threads;mach_msg_type_number_t count=0;
    if(task_threads(mach_task_self(),&threads,&count)!=KERN_SUCCESS) return -1;
    for(unsigned i=0;i<count;i++) mach_port_deallocate(mach_task_self(),threads[i]);
    vm_deallocate(mach_task_self(),(vm_address_t)threads,count*sizeof *threads);
    return (int)count;
}
static NSData *input(NSString *first,NSString *second) {
    NSMutableData *bytes=[NSMutableData dataWithBytes:(unsigned char[]){131,108,0,0,0,2} length:6];
    for(NSString *string in @[first,second]) {
        NSData *data=[string dataUsingEncoding:NSUTF8StringEncoding];uint32_t n=(uint32_t)data.length;
        unsigned char head[]={109,n>>24,n>>16,n>>8,n};
        [bytes appendBytes:head length:5];[bytes appendData:data];
    }
    [bytes appendBytes:(unsigned char[]){106} length:1];return bytes;
}

@interface AcceptanceApp : UIResponder<UIApplicationDelegate>
@property(strong,nonatomic) UIWindow *window;
@property(strong,nonatomic) UILabel *label;
@property(strong,nonatomic) NSMutableDictionary *record;
@property(strong,nonatomic) NSThread *owner;
@property(strong,nonatomic) NSTimer *timer;
- (void)report:(NSDictionary *)fields;
@end
typedef struct { __unsafe_unretained AcceptanceApp *app;unsigned events,generation;int bytecode_validated; } callback_context;
static beam_status callback(void *opaque,beam_bytes bytes,beam_host_reply *reply) {
    callback_context *context=opaque;
    static const unsigned char expected[]={131,119,10,'i','d','l','e','_','e','v','e','n','t'};
    static const unsigned char ok[]={131,119,2,'o','k'};
    if(bytes.size!=sizeof expected || memcmp(bytes.data,expected,sizeof expected)) return BEAM_INVALID_TERM;
    context->events++;
    [context->app report:@{@"events":@(context->events)}];
    return beam_host_reply_copy(reply,(beam_bytes){ok,sizeof ok});
}

static beam_status prepare(beam_system *system,callback_context *context,beam_vm **vm,beam_error *error) {
    const beam_bytecode_inventory *inventory[]={beam_sdk_inventory_get(BEAM_SDK_INVENTORY_BASE)->bytecode,beam_sdk_inventory_get(BEAM_SDK_INVENTORY_SERVICES)->bytecode,&beam_package_inventory};
    size_t total=0;for(unsigned i=0;i<3;i++) total+=inventory[i]->count;
    beam_module_spec *modules=calloc(total,sizeof *modules);
    if(!modules) return BEAM_OUT_OF_MEMORY;
    NSMutableArray * __attribute__((objc_precise_lifetime)) keep=[NSMutableArray new];size_t offset=0;beam_status status=BEAM_OK;
    for(unsigned i=0;i<3 && status==BEAM_OK;i++) {
        size_t first=offset;
        for(size_t j=0;j<inventory[i]->count;j++) {
            const beam_bytecode_record *r=&inventory[i]->records[j];
            NSString *origin=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@(r->relative_path)];
            NSData *data=[NSData dataWithContentsOfFile:origin];
            if(!data || data.length!=r->size) { status=BEAM_INVALID_TERM;break; }
            [keep addObject:origin];[keep addObject:data];
            modules[offset++]=(beam_module_spec){r->name,{data.bytes,data.length},origin.UTF8String};
        }
        if(status==BEAM_OK) status=beam_bytecode_verify(inventory[i],modules+first,inventory[i]->count,error);
    }
    context->bytecode_validated=status==BEAM_OK;
    NSString *root=NSBundle.mainBundle.privateFrameworksPath;
    NSArray * __attribute__((objc_precise_lifetime)) paths=@[[root stringByAppendingPathComponent:@"BEAMfile.framework/BEAMfile"],
        [root stringByAppendingPathComponent:@"BEAMnetwork.framework/BEAMnetwork"],
        [root stringByAppendingPathComponent:@"BEAMzlib.framework/BEAMzlib"]];
    beam_library *libraries[3]={0};
    for(size_t i=0;i<3 && status==BEAM_OK;++i)
        status=beam_library_open([paths[i] UTF8String],&libraries[i],error);
    size_t count=beam_package_linked_nifs(NULL,0);
    beam_nif_binding *nifs=calloc(count,sizeof(*nifs));
    if(count&&!nifs)status=BEAM_OUT_OF_MEMORY;
    if(status==BEAM_OK)beam_package_linked_nifs(nifs,count);
    beam_composition_options spec={.struct_size=sizeof(spec),.module_count=total,.modules=modules,
        .binding_count=count,.bindings=nifs};
    beam_startup_action actions[]={{"beam_file","start",{0}},{"beam_socket","start",{0}},{"zlib","on_load",{0}}};
    beam_host_options plan={.struct_size=sizeof(plan),.composition=&spec,.library_count=3,.libraries=libraries,
        .startup_count=3,.startup=actions};
    beam_host *host=NULL;
    if(status==BEAM_OK)status=beam_host_prepare(&plan,&host,error);
    free(modules);free(nifs);
    for(size_t i=0;i<3;++i)beam_library_release(&libraries[i]);
    if(status==BEAM_OK){
        beam_vm_options options;beam_vm_options_init(&options);options.composition=beam_host_composition(host);
        options.schedulers=2;options.dirty_cpu_schedulers=1;options.dirty_io_schedulers=1;
        status=beam_vm_init(system,&options,vm,error);
    }
    if(status==BEAM_OK)status=beam_vm_set_host_handler(*vm,callback,context);
    if(status==BEAM_OK)status=beam_vm_start(*vm,error);
    if(status==BEAM_OK)status=beam_host_start(host,*vm,error);
    beam_host_release(&host);
    return status;
}

@interface AcceptanceScene : UIResponder<UIWindowSceneDelegate>
@property(strong,nonatomic) UIWindow *window;
@end
@implementation AcceptanceScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;
    AcceptanceApp *app=(AcceptanceApp *)UIApplication.sharedApplication.delegate;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    app.window=self.window;
    UIViewController *controller=[UIViewController new];
    app.label=[[UILabel alloc] initWithFrame:self.window.bounds];app.label.numberOfLines=0;
    app.label.textAlignment=NSTextAlignmentCenter;app.label.accessibilityIdentifier=@"acceptance-result";
    controller.view.backgroundColor=UIColor.systemBackgroundColor;[controller.view addSubview:app.label];
    self.window.rootViewController=controller;[self.window makeKeyAndVisible];
}
- (void)sceneDidBecomeActive:(UIScene *)scene { (void)scene;set_active(1); }
- (void)sceneDidEnterBackground:(UIScene *)scene {
    (void)scene;AcceptanceApp *app=(AcceptanceApp *)UIApplication.sharedApplication.delegate;
    app.record[@"backgrounds"]=@([app.record[@"backgrounds"] intValue]+1);set_active(0);
}
@end

@implementation AcceptanceApp
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    self.record=[@{@"status":@"starting",@"cycles":@0,@"backgrounds":@0,@"events":@0} mutableCopy];
    [self report:@{@"state":@"initializing"}];
    self.timer=[NSTimer scheduledTimerWithTimeInterval:0.02 repeats:YES block:^(NSTimer *unused) {
        (void)unused;if(atomic_load(&calling)) atomic_fetch_add(&heartbeats,1);
    }];
    self.owner=[[NSThread alloc] initWithTarget:self selector:@selector(execute) object:nil];
    self.owner.name=@"iOS acceptance BEAM owner";[self.owner start];return YES;
}
- (void)report:(NSDictionary *)fields {
    dispatch_async(dispatch_get_main_queue(),^{
        [self.record addEntriesFromDictionary:fields];
        self.record[@"ui_heartbeats"]=@(atomic_load(&heartbeats));
        self.record[@"os"]=UIDevice.currentDevice.systemVersion;
        self.label.text=self.record.description;
        NSData *data=[NSJSONSerialization dataWithJSONObject:self.record options:NSJSONWritingPrettyPrinted error:nil];
        NSString *documents=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        [data writeToFile:[documents stringByAppendingPathComponent:@"acceptance.json"] options:NSDataWritingAtomic error:nil];
    });
}
- (void)execute {
    beam_system *system=NULL;beam_vm *vm=NULL;beam_error error={0};
    beam_system_options options;beam_system_options_init(&options);options.owner=BEAM_SYSTEM_CALLING_THREAD;
    beam_status status=beam_init_with_options(&options,&system,&error);
    callback_context context={self,0,0,0};BOOL completed=NO;beam_result saved={0};int before=thread_count();NSTimeInterval deadline=0;
    if(status!=BEAM_OK) goto failed;
    for(;;) @autoreleasepool {
        if(!atomic_load(&active) || atomic_exchange(&teardown_requested,0)) {
            if(vm) {
                pthread_mutex_lock(&wake_lock);wake_vm=NULL;pthread_mutex_unlock(&wake_lock);
                beam_vm_report cleanup={.struct_size=sizeof cleanup};
                status=beam_vm_terminate_report(&vm,NULL,&cleanup);atomic_store(&calling,0);completed=NO;
                if(status!=BEAM_OK || cleanup.has_cleanup_error) { status=BEAM_SYSTEM_ERROR;goto failed; }
                [self report:@{@"state":@"inactive",@"threads_after_stop":@(thread_count())}];
            }
            [NSThread sleepForTimeInterval:0.05];continue;
        }
        if(!vm) {
            status=prepare(system,&context,&vm,&error);if(status!=BEAM_OK) goto failed;
            pthread_mutex_lock(&wake_lock);wake_vm=vm;pthread_mutex_unlock(&wake_lock);
            ++context.generation;
            NSString *resource=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@FIXTURE_RESOURCE];
            NSString *output=[NSTemporaryDirectory() stringByAppendingPathComponent:@"beam-acceptance-file"];
            NSData *data=input(resource,output);
            status=beam_vm_call_begin(vm,"ios_acceptance","exercise",(beam_bytes){data.bytes,data.length});
            if(status!=BEAM_OK) goto failed;
            if(beam_vm_call_begin(vm,"ios_acceptance","exercise",(beam_bytes){data.bytes,data.length})!=BEAM_BUSY) {
                status=BEAM_SYSTEM_ERROR;goto failed;
            }
            deadline=NSProcessInfo.processInfo.systemUptime+10;atomic_store(&calling,1);
            [self report:@{@"state":@"calling",@"generation":@(context.generation),@"threads_running":@(thread_count()),@"threads_before":@(before)}];
        }
        if(!completed && NSProcessInfo.processInfo.systemUptime>deadline) { status=BEAM_SYSTEM_ERROR;goto failed; }
        beam_pump_result pumped={.struct_size=sizeof pumped};status=beam_vm_pump(vm,100,1,&pumped);
        if(status!=BEAM_OK) goto failed;
        if(pumped.callbacks>1 || pumped.stop_requested) { status=BEAM_SYSTEM_ERROR;goto failed; }
        if(!completed) {
            beam_result result={0};status=beam_vm_call_take(vm,&result);
            if(status==BEAM_BUSY) continue;
            if(status!=BEAM_OK || result.kind!=BEAM_RETURN || result.value.size!=3 ||
                memcmp(result.value.data,(unsigned char[]){131,97,99},3)) {
                NSString *term=result.value.size ? [[NSData dataWithBytes:result.value.data length:result.value.size] base64EncodedStringWithOptions:0] : @"";
                [self report:@{@"unexpected_result_kind":@(result.kind),@"unexpected_result_etf_base64":term,
                    @"call_status":@(beam_status_message(status))}];
                beam_result_release(&result);status=BEAM_SYSTEM_ERROR;goto failed;
            }
            beam_result_release(&saved);saved=result;completed=YES;atomic_store(&calling,0);
        }
        if(completed && context.events>=context.generation) {
            if(atomic_load(&heartbeats)<3) { status=BEAM_SYSTEM_ERROR;goto failed; }
            if(context.generation<3) {
                pthread_mutex_lock(&wake_lock);wake_vm=NULL;pthread_mutex_unlock(&wake_lock);
                beam_vm_report report={.struct_size=sizeof report};status=beam_vm_terminate_report(&vm,NULL,&report);
                if(status!=BEAM_OK || report.has_cleanup_error || saved.value.size!=3 || saved.value.data[2]!=99) { status=BEAM_SYSTEM_ERROR;goto failed; }
                [self report:@{@"threads_after_stop":@(thread_count())}];
                completed=NO;
            }
            [self report:@{@"status":@"passed",@"state":@"ready",@"cycles":@(context.generation),
                @"copied_result":@99}];
        }
    }
failed:
    pthread_mutex_lock(&wake_lock);wake_vm=NULL;pthread_mutex_unlock(&wake_lock);
    atomic_store(&calling,0);if(vm) beam_vm_terminate(&vm,NULL);
    beam_result_release(&saved);
    [self report:@{@"status":@"failed",@"bytecode_validation":context.bytecode_validated?@"passed":@"failed",@"detail":[NSString stringWithFormat:@"%s: %s",beam_status_message(status),error.message]}];
}
@end
int main(int argc,char **argv) {
    @autoreleasepool { return UIApplicationMain(argc,argv,nil,NSStringFromClass(AcceptanceApp.class)); }
}
