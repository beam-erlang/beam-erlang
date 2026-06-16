/* SPDX-License-Identifier: Apache-2.0 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "beam_runtime_image.h"
#include <stddef.h>
#include <stdio.h>
#if defined(__linux__)
#include <features.h>
#endif

#if defined(__linux__) && !defined(__GLIBC__) && !defined(__ANDROID__)
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* musl 1.2.x keeps successful dlopen mappings for the process lifetime. Inspect
 * their dependency closure before mapping; a rejected VM must never enter it.
 * Native code is trusted. Files and loader inputs must remain stable throughout
 * the host process, just as callback code must remain resident through cleanup.
 * This is not a sandbox for constructors or concurrently replaced libraries. */
typedef struct image image;
typedef struct search_directory search_directory;
struct search_directory {
    search_directory *next;
    char *path;
    struct stat identity;
    int missing;
};
struct image {
    image *next, *parent;
    char *path, *shortname;
    struct stat identity;
    unsigned char *bytes;
    size_t size, phoff, phnum, strings, string_size, symbols, symbol_count;
    size_t dynamic, dynamic_count;
    int main_program;
    char *search;
};
typedef struct {
    image *head, *tail;
    search_directory *directories;
    size_t directory_count;
    size_t count;
    struct stat runtime;
    int shared;
    char *environment, *system, *cwd;
    char config[PATH_MAX];
    struct stat config_identity;
    int config_exists;
    char *error;
    size_t error_size;
} inspection;

static int reject(inspection *c, const char *message)
{
    if (c->error_size) snprintf(c->error, c->error_size, "%s", message);
    return 0;
}
static int same_file(const struct stat *a, const struct stat *b)
{ return a->st_dev == b->st_dev && a->st_ino == b->st_ino; }
static int unchanged(const struct stat *a, const struct stat *b)
{
    return same_file(a,b) && a->st_size == b->st_size &&
        a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
/* A pathname may have been replaced since dlopen. Compare its identity with
 * the kernel's mapping record, rather than admitting a replacement as the
 * runtime which is already executing. No pathname from /proc is interpreted. */
static int mapped_file(const void *location, const struct stat *identity)
{
    FILE *maps=fopen("/proc/self/maps","re");
    if (!maps) return 0;
    char *line=NULL;
    size_t capacity=0;
    int match=0;
    while (getline(&line,&capacity,maps)>=0) {
        unsigned long long start,end,offset,inode;
        unsigned major_id,minor_id;
        char permissions[5];
        if (sscanf(line,"%llx-%llx %4s %llx %x:%x %llu",
                   &start,&end,permissions,&offset,&major_id,&minor_id,&inode)!=7)
            break;
        uintptr_t at=(uintptr_t)location;
        if (at>=start && at<end) {
            match=inode && identity->st_ino==inode &&
                identity->st_dev==makedev(major_id,minor_id);
            break;
        }
    }
    free(line); fclose(maps);
    return match;
}
static int range(size_t at, size_t length, size_t size)
{ return at <= size && length <= size - at; }
static int read_at(image *p, size_t at, void *out, size_t length)
{
    if (!range(at,length,p->size)) return 0;
    memcpy(out,p->bytes+at,length);
    return 1;
}
static int program(image *p, size_t index, Elf64_Phdr *out)
{
    if (index >= p->phnum || index > (SIZE_MAX-p->phoff)/sizeof(*out)) return 0;
    return read_at(p,p->phoff+index*sizeof(*out),out,sizeof(*out));
}
static int address(image *p, uint64_t virtual_address, size_t length, size_t *out)
{
    int found=0;
    for (size_t i=0;i<p->phnum;++i) {
        Elf64_Phdr ph;
        if (!program(p,i,&ph)) return 0;
        if (ph.p_type != PT_LOAD || virtual_address < ph.p_vaddr) continue;
        uint64_t delta=virtual_address-ph.p_vaddr;
        if (delta > ph.p_filesz || length > ph.p_filesz-delta ||
            ph.p_offset > SIZE_MAX-delta) continue;
        size_t at=ph.p_offset+delta;
        if (!range(at,length,p->size)) return 0;
        if (found && *out!=at) return 0;
        *out=at; found=1;
    }
    return found;
}
static const char *text(image *p, uint64_t offset)
{
    if (offset >= p->string_size) return NULL;
    const char *s=(const char *)p->bytes+p->strings+offset;
    return memchr(s,0,p->string_size-offset) ? s : NULL;
}
static char *expand_origin(image *p, const char *value)
{
    const char *slash=strrchr(p->path,'/');
    size_t origin=slash && slash!=p->path ? (size_t)(slash-p->path) : 1;
    const char *directory=slash ? p->path : ".";
    size_t length=strlen(value), count=0;
    for (const char *s=value; (s=strchr(s,'$')); ++s) {
        if (strncmp(s,"$ORIGIN",7) && strncmp(s,"${ORIGIN}",9)) return NULL;
        ++count;
    }
    if (count > (SIZE_MAX-length-1)/(origin+1)) return NULL;
    char *result=malloc(length+count*(origin+1)+1), *out=result;
    if (!result) return NULL;
    for (const char *s=value; *s;) {
        if (*s=='$') {
            memcpy(out,directory,origin); out+=origin;
            s+=s[1]=='{' ? 9 : 7;
        } else *out++=*s++;
    }
    *out=0; return result;
}
static int parse(inspection *c, image *p)
{
    Elf64_Ehdr eh;
    if (!read_at(p,0,&eh,sizeof(eh)) || memcmp(eh.e_ident,ELFMAG,SELFMAG) ||
        eh.e_ident[EI_CLASS]!=ELFCLASS64 || eh.e_ident[EI_DATA]!=ELFDATA2LSB ||
        eh.e_ident[EI_VERSION]!=EV_CURRENT || eh.e_version!=EV_CURRENT ||
        (eh.e_type!=ET_DYN && eh.e_type!=ET_EXEC) || eh.e_ehsize!=sizeof(Elf64_Ehdr) ||
        eh.e_flags || eh.e_phentsize!=sizeof(Elf64_Phdr) ||
        !eh.e_phnum || eh.e_phnum==PN_XNUM ||
#if defined(__x86_64__)
        eh.e_machine!=EM_X86_64 ||
#elif defined(__aarch64__)
        eh.e_machine!=EM_AARCH64 ||
#else
#error Unsupported musl ELF architecture
#endif
        eh.e_phoff>SIZE_MAX || !range(eh.e_phoff,(size_t)eh.e_phnum*sizeof(Elf64_Phdr),p->size))
        return reject(c,"native ELF header is invalid or unsupported");
    p->phoff=eh.e_phoff; p->phnum=eh.e_phnum;
    for (size_t i=0;i<p->phnum;++i) {
        Elf64_Phdr ph;
        if (!program(p,i,&ph) || !range(ph.p_offset,ph.p_filesz,p->size))
            return reject(c,"native ELF segment is out of bounds");
        if (ph.p_type==PT_LOAD && (ph.p_filesz>ph.p_memsz ||
            ph.p_vaddr>UINT64_MAX-ph.p_memsz ||
            (ph.p_align>1 && ((ph.p_align&(ph.p_align-1)) ||
                             ph.p_vaddr%ph.p_align!=ph.p_offset%ph.p_align))))
            return reject(c,"native ELF load segment is invalid");
        if (ph.p_type==PT_DYNAMIC) {
            size_t at;
            if (p->dynamic_count || ph.p_filesz%sizeof(Elf64_Dyn))
                return reject(c,"native ELF dynamic table is invalid");
            if (!address(p,ph.p_vaddr,ph.p_filesz,&at) || at!=ph.p_offset)
                return reject(c,"native ELF dynamic table is not loadable");
            p->dynamic=ph.p_offset; p->dynamic_count=ph.p_filesz/sizeof(Elf64_Dyn);
        }
    }
    uint64_t strings=0,symbols=0,hash=0,gnu_hash=0,rpath=0,runpath=0;
    int terminated=0,has_rpath=0,has_runpath=0,syment=0;
    unsigned seen=0;
    for (size_t i=0;i<p->dynamic_count;++i) {
        Elf64_Dyn d; read_at(p,p->dynamic+i*sizeof(d),&d,sizeof(d));
        if (d.d_tag==DT_NULL) { terminated=1; break; }
        unsigned field=0;
        switch (d.d_tag) {
        case DT_STRTAB: field=1; break;
        case DT_STRSZ: field=2; break;
        case DT_SYMTAB: field=4; break;
        case DT_SYMENT: field=8; break;
        case DT_HASH: field=16; break;
        case DT_GNU_HASH: field=32; break;
        case DT_RPATH: field=64; break;
        case DT_RUNPATH: field=128; break;
        }
        if (seen&field) return reject(c,"native ELF dynamic metadata is ambiguous");
        seen|=field;
        switch (d.d_tag) {
        case DT_STRTAB: strings=d.d_un.d_ptr; break;
        case DT_STRSZ: p->string_size=d.d_un.d_val; break;
        case DT_SYMTAB: symbols=d.d_un.d_ptr; break;
        case DT_SYMENT: syment=d.d_un.d_val==sizeof(Elf64_Sym); break;
        case DT_HASH: hash=d.d_un.d_ptr; break;
        case DT_GNU_HASH: gnu_hash=d.d_un.d_ptr; break;
        case DT_RPATH: rpath=d.d_un.d_val; has_rpath=1; break;
        case DT_RUNPATH: runpath=d.d_un.d_val; has_runpath=1; break;
        }
    }
    if (!terminated || !strings || !symbols || !p->string_size || !syment ||
        !address(p,strings,p->string_size,&p->strings))
        return reject(c,"native ELF dynamic metadata is incomplete");
    if (hash) {
        size_t at; uint32_t h[2];
        if (!address(p,hash,sizeof(h),&at) || !read_at(p,at,h,sizeof(h)) || !h[0] || !h[1] ||
            (uint64_t)h[0]+h[1] > (SIZE_MAX-sizeof(h))/4 ||
            !address(p,hash,sizeof(h)+((size_t)h[0]+h[1])*4,&at))
            return reject(c,"native ELF symbol hash is invalid");
        p->symbol_count=h[1];
        for (size_t i=0;i<(size_t)h[0]+h[1];++i) {
            uint32_t index; read_at(p,at+sizeof(h)+i*4,&index,4);
            if (index>=h[1]) return reject(c,"native ELF symbol hash index is invalid");
        }
        for (size_t i=0;i<h[0];++i) {
            uint32_t index; read_at(p,at+sizeof(h)+i*4,&index,4);
            for (size_t steps=0;index;++steps) {
                if (steps>=h[1]) return reject(c,"native ELF symbol hash contains a cycle");
                read_at(p,at+sizeof(h)+((size_t)h[0]+index)*4,&index,4);
            }
        }
    }
    if (gnu_hash) {
        size_t at; uint32_t h[4];
        if (!address(p,gnu_hash,sizeof(h),&at) || !read_at(p,at,h,sizeof(h)) || !h[0] || !h[2] ||
            (h[2]&(h[2]-1)) || h[3]>=32 || h[0]>p->size/4 || h[2]>p->size/8)
            return reject(c,"native ELF GNU symbol hash is invalid");
        uint64_t buckets=gnu_hash+sizeof(h)+(uint64_t)h[2]*8;
        if (buckets<gnu_hash || !address(p,buckets,(size_t)h[0]*4,&at))
            return reject(c,"native ELF GNU hash buckets are invalid");
        size_t count=h[1];
        for (size_t i=0;i<h[0];++i) {
            uint32_t bucket; read_at(p,at+i*4,&bucket,4);
            if (!bucket) continue;
            if (bucket<h[1]) return reject(c,"native ELF GNU hash bucket is invalid");
            size_t symbol=bucket;
            for (;;) {
                uint64_t chain=buckets+(uint64_t)h[0]*4+(symbol-h[1])*4;
                size_t position; uint32_t value;
                if (symbol>=p->size/sizeof(Elf64_Sym) || chain<buckets ||
                    !address(p,chain,4,&position) || !read_at(p,position,&value,4))
                    return reject(c,"native ELF GNU hash chain is invalid");
                ++symbol;
                if (value&1) break;
            }
            if (symbol>count) count=symbol;
        }
        if (hash && count!=p->symbol_count)
            return reject(c,"native ELF symbol hashes disagree");
        p->symbol_count=count;
    } else if (!hash) return reject(c,"native ELF lacks a supported dynamic symbol hash");
    if (p->symbol_count>p->size/sizeof(Elf64_Sym) ||
        !address(p,symbols,p->symbol_count*sizeof(Elf64_Sym),&p->symbols))
        return reject(c,"native ELF symbols are out of bounds");
    for (size_t i=0;i<p->symbol_count;++i) {
        Elf64_Sym s; read_at(p,p->symbols+i*sizeof(s),&s,sizeof(s));
        const char *name=text(p,s.st_name);
        if (!name) return reject(c,"native ELF symbol name is invalid");
        unsigned binding=ELF64_ST_BIND(s.st_info), visibility=ELF64_ST_VISIBILITY(s.st_other);
        if (s.st_shndx!=SHN_UNDEF && (binding==STB_GLOBAL || binding==STB_WEAK || binding==STB_GNU_UNIQUE) &&
            (visibility==STV_DEFAULT || visibility==STV_PROTECTED) && !strcmp(name,"beam_build_id") &&
            !p->main_program && !(c->shared && same_file(&p->identity,&c->runtime)))
            return reject(c,BEAM_RUNTIME_IMAGE_ERROR);
    }
    if (has_rpath || has_runpath) {
        const char *value=text(p,has_runpath ? runpath : rpath);
        if (!value || !(p->search=expand_origin(p,value)))
            return reject(c,"native ELF search path is unsupported");
    }
    return 1;
}

static image *add_image(inspection *c, const char *path, image *parent)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if (fd<0) { reject(c,"native dependency cannot be opened"); return NULL; }
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size>SIZE_MAX) {
        close(fd); reject(c,"native dependency is not a regular ELF image"); return NULL;
    }
    for (image *p=c->head;p;p=p->next) if (same_file(&st,&p->identity)) {
        close(fd); return p;
    }
    if (++c->count>4096) { close(fd); reject(c,"native dependency closure is too large"); return NULL; }
    image *p=calloc(1,sizeof(*p));
    if (!p) { close(fd); reject(c,"native inspection allocation failed"); return NULL; }
    p->size=st.st_size; p->identity=st; p->parent=parent;
    p->main_program=!parent;
    p->path=strdup(path);
    p->bytes=p->size ? mmap(NULL,p->size,PROT_READ,MAP_PRIVATE,fd,0) : MAP_FAILED;
    close(fd);
    if (c->tail) c->tail->next=p; else c->head=p;
    c->tail=p;
    if (!p->path || p->bytes==MAP_FAILED) { reject(c,"native image cannot be inspected"); return NULL; }
    return parse(c,p) ? p : NULL;
}

/* RTLD_NOLOAD never introduces a mapping. It selects musl's real resident
 * provider rather than guessing from SONAMEs or filenames. */
static image *resident(inspection *c, const char *name, image *parent, int *found)
{
    void *handle=dlopen(name,RTLD_NOW|RTLD_LOCAL|RTLD_NOLOAD);
    *found=handle!=NULL;
    if (!handle) return NULL;
    struct link_map *map=NULL;
    int result=dlinfo(handle,RTLD_DI_LINKMAP,&map);
    image *p=NULL;
    if (result || !map || !map->l_name || !*map->l_name)
        reject(c,"resident native dependency cannot be identified");
    else {
        p=add_image(c,map->l_name,parent);
        if (p && !mapped_file(map->l_ld,&p->identity)) {
            reject(c,"resident native image identity changed"); p=NULL;
        }
    }
    dlclose(handle);
    return p;
}
static int remember_directory(inspection *c, const char *path, size_t length)
{
    for (search_directory *p=c->directories;p;p=p->next)
        if (strlen(p->path)==length && !memcmp(p->path,path,length)) return 1;
    if (++c->directory_count>4096) return 0;
    search_directory *p=calloc(1,sizeof(*p));
    if (!p) return 0;
    p->path=strndup(path,length);
    if (!p->path) { free(p); return 0; }
    if (stat(p->path,&p->identity)) {
        p->missing=errno;
        if (errno!=ENOENT && errno!=ENOTDIR && errno!=EACCES) {
            free(p->path); free(p); return 0;
        }
    }
    p->next=c->directories; c->directories=p;
    return 1;
}
static char *search_path(inspection *c, const char *name, const char *paths, int *failed)
{
    if (!paths) return NULL;
    for (const char *s=paths;*s;) {
        s+=strspn(s,":\n");
        size_t length=strcspn(s,":\n");
        if (!length) break;
        char path[PATH_MAX];
        if (length>INT_MAX || !remember_directory(c,s,length)) { *failed=1; return NULL; }
        int n=snprintf(path,sizeof(path),"%.*s/%s",(int)length,s,name);
        if (n<0 || n>=(int)sizeof(path)) { *failed=1; return NULL; }
        int fd=open(path,O_RDONLY|O_CLOEXEC);
        if (fd>=0) {
            close(fd);
            char *result=strdup(path);
            if (!result) *failed=1;
            return result;
        }
        if (errno!=ENOENT && errno!=ENOTDIR && errno!=EACCES && errno!=ENAMETOOLONG) {
            *failed=1; return NULL;
        }
        s+=length;
    }
    errno=ENOENT; return NULL;
}
static image *dependency(inspection *c, const char *name, image *parent)
{
    int found=0;
    image *p=resident(c,name,parent,&found);
    if (found) return p;
    if (!strchr(name,'/'))
        for (p=c->head;p;p=p->next)
            if (p->shortname && !strcmp(p->shortname,name)) return p;
    char *path=NULL;
    if (strchr(name,'/')) path=strdup(name);
    else {
        int failed=0;
        path=search_path(c,name,c->environment,&failed);
        for (image *ancestor=parent;!path && !failed && ancestor;ancestor=ancestor->parent)
            path=search_path(c,name,ancestor->search,&failed);
        if (!path && !failed) path=search_path(c,name,c->system,&failed);
        if (failed) { free(path); reject(c,"native dependency search failed"); return NULL; }
    }
    if (!path) { reject(c,"native dependency closure cannot be resolved"); return NULL; }
    p=add_image(c,path,parent); free(path);
    if (p && !strchr(name,'/') && !p->shortname) {
        p->shortname=strdup(name);
        if (!p->shortname) { reject(c,"native inspection allocation failed"); return NULL; }
    }
    return p;
}
static char *read_file(const char *path, size_t limit, size_t *length)
{
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if (fd<0) return NULL;
    char *buffer=malloc(limit+1);
    size_t used=0;
    if (!buffer) { close(fd); return NULL; }
    while (used<limit) {
        ssize_t n=read(fd,buffer+used,limit-used);
        if (n<0 && errno==EINTR) continue;
        if (n<0) { free(buffer); close(fd); return NULL; }
        if (!n) break;
        used+=(size_t)n;
    }
    char extra;
    if (used==limit && read(fd,&extra,1)!=0) { free(buffer); close(fd); errno=EOVERFLOW; return NULL; }
    close(fd); buffer[used]=0;
    if (length) *length=used;
    return buffer;
}
static int settings(inspection *c)
{
    if (getauxval(AT_SECURE) || !getauxval(AT_BASE))
        return reject(c,"secure or directly invoked musl loader inspection is unsupported");
    c->cwd=getcwd(NULL,0);
    size_t used=0;
    char *buffer=read_file("/proc/self/environ",1048576,&used);
    if (!c->cwd || !buffer) { free(buffer); return reject(c,"initial native loader environment is unavailable"); }
    for (size_t at=0;at<used;) {
        const char *end=memchr(buffer+at,0,used-at);
        if (!end) { free(buffer); return reject(c,"initial native loader environment is malformed"); }
        if (!strncmp(buffer+at,"LD_LIBRARY_PATH=",16)) {
            if (c->environment) { free(buffer); return reject(c,"duplicate initial loader search path"); }
            c->environment=strdup(buffer+at+16);
            if (!c->environment) { free(buffer); return reject(c,"native inspection allocation failed"); }
        }
        at=(size_t)(end-buffer)+1;
    }
    free(buffer);
#if defined(__x86_64__)
    const char *arch="x86_64";
#else
    const char *arch="aarch64";
#endif
    Dl_info loader;
    if (!dladdr((void *)&dlopen,&loader) || !loader.dli_fname || loader.dli_fname[0]!='/')
        return reject(c,"musl loader path cannot be identified");
    char prefix[PATH_MAX];
    if (snprintf(prefix,sizeof(prefix),"%s",loader.dli_fname)>=(int)sizeof(prefix)) return 0;
    char *last=strrchr(prefix,'/'); if (!last) return 0; *last=0;
    last=strrchr(prefix,'/'); if (!last) return 0; *last=0;
    if (snprintf(c->config,sizeof(c->config),"%s/etc/ld-musl-%s.path",prefix,arch)>=(int)sizeof(c->config)) return 0;
    c->config_exists=stat(c->config,&c->config_identity)==0;
    if (!c->config_exists && errno!=ENOENT) return reject(c,"musl loader configuration cannot be inspected");
    c->system=c->config_exists ? read_file(c->config,1048576,NULL) : strdup("/lib:/usr/local/lib:/usr/lib");
    if (!c->system) return reject(c,"musl loader search path cannot be inspected");
    Dl_info runtime;
    c->shared=!beam_runtime_is_static();
    if (c->shared && (!dladdr((void *)&beam_runtime_is_static,&runtime) ||
        !runtime.dli_fname || stat(runtime.dli_fname,&c->runtime) ||
        !mapped_file((void *)&beam_runtime_is_static,&c->runtime)))
        return reject(c,"host runtime image cannot be identified");
    return 1;
}

int beam_runtime_preflight(const char *path, char *error, size_t error_size)
{
    inspection c={.error=error,.error_size=error_size};
    if (error_size) error[0]=0;
    int ok=settings(&c);
    image *main=NULL;
    if (ok) {
        /* dlopen's root needed_by is the main program in musl 1.2.6. */
        char executable[PATH_MAX];
        ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
        if (n<0) ok=reject(&c,"main executable location is unavailable");
        else {
            executable[n]=0;
            main=add_image(&c,executable,NULL);
            if (!main) ok=0;
        }
    }
    image *root=ok ? dependency(&c,path,main) : NULL;
    if (!root) ok=0;
    /* Append newly discovered dependencies in breadth-first load order. Main
     * program dependencies are already resident and are selected by NOLOAD. */
    for (image *p=root;ok && p;p=p->next) {
        for (size_t i=0;i<p->dynamic_count;++i) {
            Elf64_Dyn d; read_at(p,p->dynamic+i*sizeof(d),&d,sizeof(d));
            if (d.d_tag==DT_NULL) break;
            if (d.d_tag==DT_NEEDED) {
                const char *name=text(p,d.d_un.d_val);
                if (!name || !*name || !dependency(&c,name,p)) { ok=0; break; }
            }
        }
    }
    if (ok) {
        char *cwd=getcwd(NULL,0); struct stat config;
        int exists=stat(c.config,&config)==0;
        if ((!exists && errno!=ENOENT) || !cwd || strcmp(cwd,c.cwd) || exists!=c.config_exists ||
            (exists && !unchanged(&config,&c.config_identity)))
            ok=reject(&c,"native loader inputs changed during admission");
        free(cwd);
        for (image *p=c.head;ok && p;p=p->next) {
            struct stat current;
            if (stat(p->path,&current) || !unchanged(&current,&p->identity))
                ok=reject(&c,"native image changed during admission");
        }
        for (search_directory *p=c.directories;ok && p;p=p->next) {
            struct stat current;
            int missing=stat(p->path,&current) ? errno : 0;
            if (missing!=p->missing || (!missing && !unchanged(&current,&p->identity)))
                ok=reject(&c,"native search directory changed during admission");
        }
    }
    while (c.directories) {
        search_directory *p=c.directories; c.directories=p->next;
        free(p->path); free(p);
    }
    while (c.head) {
        image *p=c.head; c.head=p->next;
        if (p->bytes && p->bytes!=MAP_FAILED) munmap(p->bytes,p->size);
        free(p->path); free(p->shortname); free(p->search); free(p);
    }
    free(c.environment); free(c.system); free(c.cwd);
    if (!ok && error_size && !error[0]) reject(&c,"native ELF inspection failed");
    return ok;
}
#else
int beam_runtime_preflight(const char *path, char *error, size_t error_size)
{ (void)path; (void)error; (void)error_size; return 1; }
#endif
