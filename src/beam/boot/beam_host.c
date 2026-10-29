/* SPDX-License-Identifier: Apache-2.0 */
#include <beam/host.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct beam_host {
    beam_composition *composition;
    beam_buffer startup;
    size_t binding_count;
    char **bindings;
};
static beam_status fail(beam_error *e, beam_status s, const char *m)
{
    if (e) snprintf(e->message, sizeof(e->message), "%s", m);
    return s;
}

void beam_host_release(beam_host **out)
{
    if (!out || !*out) return;
    beam_host *h = *out;
    *out = NULL;
    beam_composition_release(&h->composition);
    for (size_t i=0; i<h->binding_count; ++i) free(h->bindings[i]);
    free(h->bindings);
    free(h->startup.data);
    free(h);
}
const beam_composition *beam_host_composition(const beam_host *h)
{
    return h ? h->composition : NULL;
}

typedef struct buffer { beam_buffer bytes; size_t capacity; } buffer;
static int append(buffer *b, const void *data, size_t size)
{
    if (size > SIZE_MAX - b->bytes.size) return 0;
    size_t needed = b->bytes.size + size;
    if (needed > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 128;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        void *grown = realloc(b->bytes.data, capacity);
        if (!grown) return 0;
        b->bytes.data = grown; b->capacity = capacity;
    }
    if (size) memcpy(b->bytes.data + b->bytes.size, data, size);
    b->bytes.size = needed;
    return 1;
}
static int byte(buffer *b, unsigned v) { unsigned char c=v; return append(b,&c,1); }
static int length(buffer *b, size_t v)
{
    if (v>UINT32_MAX) return 0;
    unsigned char c[]={v>>24,v>>16,v>>8,v}; return append(b,c,4);
}
static int atom(buffer *b, const char *s)
{
    if (!s || !*s) return 0;
    size_t n=strlen(s);
    return n<=65535 && byte(b,118) && byte(b,n>>8) && byte(b,n) && append(b,s,n);
}
static int binary(buffer *b, const void *p, size_t n) { return byte(b,109)&&length(b,n)&&append(b,p,n); }
static int list(buffer *b, size_t n) { return byte(b,108)&&length(b,n); }
static int tuple(buffer *b, unsigned n) { return byte(b,104)&&byte(b,n); }
static int encode_startup(buffer *b, const beam_host_options *o,
                          const beam_module_spec *modules, size_t count)
{
    if (!byte(b,131)||!list(b,3)||!list(b,o->startup_count)) return 0;
    for (size_t i=0;i<o->startup_count;++i) {
        const beam_startup_action *a=&o->startup[i];
        if (!tuple(b,3)||!atom(b,a->module)||!atom(b,a->function)) return 0;
        if (a->arguments.size) {
            if (!a->arguments.data || a->arguments.data[0]!=131 ||
                !append(b,a->arguments.data+1,a->arguments.size-1)) return 0;
        } else if (!byte(b,106)) return 0;
    }
    if (!byte(b,106)||!list(b,o->deferred_count)) return 0;
    for (size_t i=0;i<o->deferred_count;++i) {
        const beam_module_spec *m=&o->deferred[i];
        const char *origin=m->origin?m->origin:"memory";
        if (!tuple(b,3)||!atom(b,m->name)||!binary(b,m->bytes.data,m->bytes.size)||
            !binary(b,origin,strlen(origin))) return 0;
    }
    if (!byte(b,106)||!list(b,count+o->deferred_count)) return 0;
    for (size_t i=0;i<count+o->deferred_count;++i) {
        const beam_module_spec *m=i<count?&modules[i]:&o->deferred[i-count];
        const char *origin=m->origin?m->origin:"memory";
        if (!tuple(b,2)||!atom(b,m->name)||!binary(b,origin,strlen(origin))) return 0;
    }
    return byte(b,106)&&byte(b,106);
}

beam_status beam_host_prepare(const beam_host_options *o, beam_host **out, beam_error *e)
{
    beam_status s=BEAM_OK;
    beam_composition_options c={.struct_size=sizeof(c)};
    beam_library_inputs *inputs=NULL;
    beam_module_spec *modules=NULL;
    beam_builtin_nif *nifs=NULL;
    beam_native_owner *owners=NULL;
    beam_composition *validation=NULL;
    buffer b={0};
    if (e) memset(e,0,sizeof(*e));
    if (!out || *out || !o || o->struct_size!=sizeof(*o) ||
        (o->composition && o->composition->struct_size!=sizeof(c)) ||
        o->library_count>1024 || (o->library_count&&!o->libraries) ||
        o->startup_count>4096 || (o->startup_count&&!o->startup) ||
        o->deferred_count>4096 || (o->deferred_count&&!o->deferred))
        return fail(e,BEAM_INVALID_ARGUMENT,"invalid host preparation");
    if (o->composition) c=*o->composition;
    /* Validate generic pointer/count pairs before merging any inputs. */
    if ((c.module_count&&!c.modules)||(c.builtin_nif_count&&!c.builtin_nifs)||
        (c.native_owner_count&&!c.native_owners)||c.module_count>4096||
        c.builtin_nif_count>1024||c.native_owner_count>4096||
        c.binding_count>1024||(c.binding_count&&!c.bindings))
        return fail(e,BEAM_INVALID_ARGUMENT,"invalid generic host inputs");
    beam_host *h=calloc(1,sizeof(*h));
    if (!h) return BEAM_OUT_OF_MEMORY;
    size_t mc=c.module_count,nc=c.builtin_nif_count,oc=c.native_owner_count;
    inputs=calloc(o->library_count?o->library_count:1,sizeof(*inputs));
    if (!inputs) { s=BEAM_OUT_OF_MEMORY; goto done; }
    for(size_t i=0;i<o->library_count;++i) {
        inputs[i].struct_size=sizeof(inputs[i]);
        s=beam_library_get_inputs(o->libraries[i],&inputs[i]);
        if(s!=BEAM_OK) goto done;
        mc+=inputs[i].module_count;nc+=inputs[i].nif_count;++oc;
        if(mc>4096||nc>1024||oc>4096) {s=BEAM_INVALID_ARGUMENT;goto done;}
    }
    modules=calloc(mc+o->deferred_count+1,sizeof(*modules));
    nifs=calloc(nc+1,sizeof(*nifs));owners=calloc(oc+1,sizeof(*owners));
    if(!modules||!nifs||!owners){s=BEAM_OUT_OF_MEMORY;goto done;}
    if(c.module_count)memcpy(modules,c.modules,c.module_count*sizeof(*modules));
    if(c.builtin_nif_count)memcpy(nifs,c.builtin_nifs,c.builtin_nif_count*sizeof(*nifs));
    if(c.native_owner_count)memcpy(owners,c.native_owners,c.native_owner_count*sizeof(*owners));
    mc=c.module_count;nc=c.builtin_nif_count;oc=c.native_owner_count;
    for(size_t i=0;i<o->library_count;++i){
        if(inputs[i].module_count)memcpy(modules+mc,inputs[i].modules,inputs[i].module_count*sizeof(*modules));
        if(inputs[i].nif_count)memcpy(nifs+nc,inputs[i].nifs,inputs[i].nif_count*sizeof(*nifs));
        mc+=inputs[i].module_count;nc+=inputs[i].nif_count;owners[oc++]=inputs[i].owner;
    }
    c.modules=modules;c.module_count=mc;c.builtin_nifs=nifs;c.builtin_nif_count=nc;
    c.native_owners=owners;c.native_owner_count=oc;
    if(o->deferred_count){
        memcpy(modules+mc,o->deferred,o->deferred_count*sizeof(*modules));
        c.module_count=mc+o->deferred_count;
        s=beam_composition_prepare(&c,&validation,e);
        if(s!=BEAM_OK)goto done;
        beam_composition_release(&validation);
        c.module_count=mc;
    }
    s=beam_composition_prepare(&c,&h->composition,e);
    if(s!=BEAM_OK)goto done;
    h->bindings=calloc(c.binding_count?c.binding_count:1,sizeof(*h->bindings));
    if(!h->bindings){s=BEAM_OUT_OF_MEMORY;goto done;}
    for(size_t i=0;i<c.binding_count;++i){
        h->bindings[h->binding_count++]=strdup(c.bindings[i].module);
        if(!h->bindings[i]){s=BEAM_OUT_OF_MEMORY;goto done;}
    }
    if(o->startup_count||o->deferred_count){
        const beam_module_spec *copied;size_t count;
        beam_composition_get_modules(h->composition,&copied,&count);
        int found=0;
        for(size_t i=0;i<count;++i)if(!strcmp(copied[i].name,"beam_startup"))found=1;
        if(!found){s=fail(e,BEAM_INVALID_ARGUMENT,"explicit startup requires beam_startup bytecode");goto done;}
        if(!encode_startup(&b,o,copied,count)){s=BEAM_INVALID_ARGUMENT;goto done;}
        h->startup=b.bytes;b.bytes=(beam_buffer){0};
    }
    *out=h;h=NULL;
done:
    free(b.bytes.data);free(inputs);free(modules);free(nifs);free(owners);
    beam_composition_release(&validation);beam_host_release(&h);
    if(s!=BEAM_OK&&e&&!e->message[0])fail(e,s,beam_status_message(s));
    return s;
}

beam_status beam_host_start(beam_host *h, beam_vm *vm, beam_error *e)
{
    if(e)memset(e,0,sizeof(*e));
    if(!h||!vm)return BEAM_INVALID_ARGUMENT;
    beam_vm_state_info state;
    beam_status s=beam_vm_get_state(vm,&state);
    if(s!=BEAM_OK)return s;
    if(state.state!=BEAM_VM_RUNNING)return BEAM_INVALID_STATE;
    if(h->startup.size){
        beam_result result={0};
        s=beam_vm_call(vm,"beam_startup","run",(beam_bytes){h->startup.data,h->startup.size},&result);
        if(s==BEAM_OK&&result.kind!=BEAM_RETURN)s=BEAM_SYSTEM_ERROR;
        beam_result_release(&result);
    }
    for(size_t i=0;s==BEAM_OK&&i<h->binding_count;++i)s=beam_vm_bind_nif(vm,h->bindings[i],e);
    if(s!=BEAM_OK){
        beam_vm_report report={.struct_size=sizeof(report)};
        if(beam_vm_get_report(vm,&report)==BEAM_OK&&report.cause==BEAM_TERMINAL_FAILURE&&e)*e=report.failure;
        beam_vm_stop(vm,NULL);
        if(e&&!e->message[0])fail(e,s,"explicit host startup failed");
    }
    return s;
}

beam_status beam_host_open_nif(const char *path,const char *module,beam_native_image **out,
                               beam_nif_binding *binding,beam_native_owner *owner,beam_error *e)
{
    if(e)memset(e,0,sizeof(*e));
    if(!module||!*module||!out||*out||!binding||binding->struct_size!=sizeof(*binding)||
       !owner||owner->struct_size!=sizeof(*owner))return BEAM_INVALID_ARGUMENT;
    beam_native_image *image=NULL;void *stamp=NULL,*init=NULL;
    beam_status s=beam_native_image_open(path,&image,e);
    if(s!=BEAM_OK)return s;
    s=beam_native_image_symbol(image,"beam_nif_sdk_v1",&stamp);
    if(s==BEAM_OK)s=beam_native_image_symbol(image,"nif_init",&init);
    if(s!=BEAM_OK||!stamp||strcmp(stamp,beam_build_id())||!init){
        beam_native_image_release(&image);return fail(e,BEAM_INVALID_ARGUMENT,"NIF SDK identity or initializer mismatch");
    }
    beam_nif_binding b={.struct_size=sizeof(b),.module=module,.build_id=beam_build_id()};
    /* The image API uses the native platform entry ABI, including Windows callbacks. */
    _Static_assert(sizeof(b.native_initialize)==sizeof(init),"native function pointer representation");
    memcpy(&b.native_initialize,&init,sizeof(init));
    beam_native_owner o={.struct_size=sizeof(o)};
    s=beam_native_image_owner(image,&o);
    if(s!=BEAM_OK){beam_native_image_release(&image);return s;}
    *binding=b;*owner=o;*out=image;return BEAM_OK;
}
