/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2010-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

/*
 * Description: A Thread library for use in the ERTS and other OTP
 *              applications.
 * Author: Rickard Green
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#define ETHR_INLINE_FUNC_NAME_(X) X ## __
#define ETHR_AUX_IMPL__
#include "ethread.h"
#include "ethr_internal.h"
#include <string.h>
#include <limits.h>

#ifndef __WIN32__
#include <unistd.h>
#endif

#define ERTS_TS_EV_ALLOC_DEFAULT_POOL_SIZE 2048
#define ERTS_TS_EV_ALLOC_POOL_SIZE 32

erts_cpu_info_t *ethr_cpu_info__;

int ethr_not_completely_inited__ = 1;
int ethr_not_inited__ = 1;

ethr_memory_allocators ethr_mem__ = ETHR_MEM_ALLOCS_DEF_INITER__;

void *(*ethr_thr_prepare_func__)(void) = NULL;
void (*ethr_thr_parent_func__)(void *) = NULL;
void (*ethr_thr_child_func__)(void *) = NULL;

typedef struct ethr_xhndl_list_ ethr_xhndl_list;
struct ethr_xhndl_list_ {
    ethr_xhndl_list *next;
    void (*funcp)(void);
    int runtime_owned;
    int static_registration;
    int runtime_bound;
};

size_t ethr_pagesize__;
size_t ethr_min_stack_size__; /* kilo words */
size_t ethr_max_stack_size__; /* kilo words */

ethr_rwmutex xhndl_rwmtx;
ethr_xhndl_list *xhndl_list;

static ethr_tsd_key ethr_stacklimit_key__;

static int main_threads;
static int runtime_active;
static int runtime_cleaned;
static int runtime_destroyed;

static int init_ts_event_alloc(void);
static int destroy_ts_event_alloc(void);

#ifndef __WIN32__
extern pthread_key_t ethr_ts_event_key__;

typedef struct ethr_owned_key_ {
    struct ethr_owned_key_ *next;
    ethr_tsd_key key;
    ethr_tsd_key *static_address;
    int runtime_owned;
    int runtime_bound;
} ethr_owned_key;

static pthread_mutex_t owned_keys_lock = PTHREAD_MUTEX_INITIALIZER;
static ethr_owned_key *owned_keys;

int ethr_runtime_register_key__(ethr_tsd_key key)
{
    ethr_owned_key *entry = malloc(sizeof(*entry));
    if (!entry)
        return ENOMEM;
    entry->key = key;
    entry->static_address = NULL;
    entry->runtime_owned = runtime_active;
    entry->runtime_bound = runtime_active;
    pthread_mutex_lock(&owned_keys_lock);
    entry->next = owned_keys;
    owned_keys = entry;
    pthread_mutex_unlock(&owned_keys_lock);
    return 0;
}

/* Only explicit fixed VM key declarations enter this cache. Dynamic keys,
 * including scheduler preallocator keys, retain ordinary VM ownership. */
int ethr_runtime_bind_static_key__(ethr_tsd_key *keyp)
{
    ethr_owned_key *entry;
    int res;
    if (!runtime_active || runtime_cleaned || !keyp)
        return EINVAL;
    pthread_mutex_lock(&owned_keys_lock);
    for (entry = owned_keys; entry; entry = entry->next) {
        if (entry->static_address == keyp) {
            *keyp = entry->key;
            entry->runtime_bound = 1;
            pthread_mutex_unlock(&owned_keys_lock);
            return 0;
        }
    }
    entry = malloc(sizeof(*entry));
    if (!entry) {
        pthread_mutex_unlock(&owned_keys_lock);
        return ENOMEM;
    }
    res = pthread_key_create(&entry->key, NULL);
    if (res) {
        free(entry);
        pthread_mutex_unlock(&owned_keys_lock);
        return res;
    }
    entry->static_address = keyp;
    entry->runtime_owned = 0;
    entry->runtime_bound = 1;
    entry->next = owned_keys;
    owned_keys = entry;
    *keyp = entry->key;
    pthread_mutex_unlock(&owned_keys_lock);
    return 0;
}

void ethr_runtime_forget_key__(ethr_tsd_key key)
{
    ethr_owned_key **link, *entry = NULL;
    pthread_mutex_lock(&owned_keys_lock);
    for (link = &owned_keys; *link; link = &(*link)->next) {
        if ((*link)->key == key) {
            entry = *link;
            *link = entry->next;
            break;
        }
    }
    pthread_mutex_unlock(&owned_keys_lock);
    free(entry);
}

static int destroy_owned_keys(int runtime_only)
{
    ethr_owned_key **link = &owned_keys;
    while (*link) {
        ethr_owned_key *entry = *link;
        int res;
        if (runtime_only && !entry->runtime_owned) {
            link = &entry->next;
            continue;
        }
        res = pthread_setspecific(entry->key, NULL);
        if (!res)
            res = pthread_key_delete(entry->key);
        if (res)
            return res;
        *link = entry->next;
        free(entry);
    }
    return 0;
}

static int clear_runtime_keys(int unbind)
{
    ethr_owned_key *entry;
    for (entry = owned_keys; entry; entry = entry->next) {
        if (entry->runtime_bound) {
            int res = pthread_setspecific(entry->key, NULL);
            if (res)
                return res;
            if (unbind)
                entry->runtime_bound = 0;
        }
    }
    return 0;
}
#endif

int ethr_runtime_is_active(void)
{
    return runtime_active;
}

int ethr_runtime_begin(void)
{
    if (ethr_not_completely_inited__ || runtime_destroyed)
        return EINVAL;
    if (runtime_active)
        return EBUSY;
    runtime_active = 1;
    runtime_cleaned = 0;
    return 0;
}

int ethr_runtime_bind(int no_main_threads, int reader_groups)
{
    int res;
    if (!runtime_active || runtime_cleaned || no_main_threads < 0
        || no_main_threads > USHRT_MAX || reader_groups < 0
        || reader_groups > USHRT_MAX)
        return EINVAL;
    res = ethr_mutex_lib_late_init(reader_groups, no_main_threads);
    if (!res)
        main_threads = no_main_threads;
    return res;
}

int ethr_runtime_cleanup(void)
{
    ethr_xhndl_list **link;
    int res;
    if (!runtime_active)
        return EINVAL;
    if (runtime_cleaned)
        return 0;
    /* Workers have been joined. Owner-thread callbacks still need the VM's
     * TLS values, locks and allocators; remove registrations only afterward. */
    for (link = &xhndl_list; *link; ) {
        ethr_xhndl_list *entry = *link;
        if (!entry->runtime_bound) {
            link = &entry->next;
            continue;
        }
        entry->funcp();
        entry->runtime_bound = 0;
        if (entry->static_registration) {
            link = &entry->next;
        }
        else {
            *link = entry->next;
            ethr_mem__.std.free(entry);
        }
    }
#ifndef __WIN32__
    res = clear_runtime_keys(0);
    if (res)
        return res;
#endif
    runtime_cleaned = 1;
    return 0;
}

int ethr_runtime_unbind(void)
{
    ethr_ts_event *tse;
    int res;
    if (!runtime_active || !runtime_cleaned)
        return EINVAL;
    tse = ethr_get_tse__();
    if (!tse || tse->udata || (tse->iflgs & ETHR_TS_EV_BUSY))
        return EBUSY;
    res = ethr_set_main_thr_status(0, 0);
    if (!res)
        res = ethr_rwmutex_set_reader_group(0);
    if (!res)
        res = ethr_mutex_lib_late_init(0, 0);
    if (res)
        return res;
#ifndef __WIN32__
    /* Semantic finalizers may have consulted or repopulated a TLS slot after
     * owner callbacks ran. No VM value may survive in a retained static key. */
    res = clear_runtime_keys(1);
    if (!res)
        res = destroy_owned_keys(1);
    if (res)
        return res;
#endif
    tse->uflgs = 0;
    ethr_event_reset(&tse->event);
    main_threads = 0;
    runtime_active = 0;
    return 0;
}

int ethr_runtime_system_cleanup(void)
{
    ethr_xhndl_list *entry;
    if (runtime_active || runtime_destroyed || ethr_not_completely_inited__)
        return EBUSY;
    if (ethr_no_used_tse() != 1)
        return EBUSY;
    while ((entry = xhndl_list)) {
        if (!entry->static_registration || entry->runtime_bound)
            entry->funcp();
        xhndl_list = entry->next;
        ethr_mem__.std.free(entry);
    }
    return 0;
}

int ethr_runtime_destroy(void)
{
    ethr_ts_event *tse;
    int res;
    if (runtime_active || runtime_destroyed || ethr_not_completely_inited__)
        return EBUSY;
    if (ethr_no_used_tse() != 1)
        return EBUSY;
    res = ethr_runtime_system_cleanup();
    if (res)
        return res;
    tse = ethr_get_tse__();
    if (!tse || tse->udata || (tse->iflgs & ETHR_TS_EV_BUSY))
        return EBUSY;
    res = ethr_rwmutex_destroy(&xhndl_rwmtx);
    if (res)
        return res;
    ethr_ts_event_destructor__(tse);
    res = destroy_ts_event_alloc();
    if (res)
        return res;
#ifndef __WIN32__
    res = destroy_owned_keys(0);
    if (!res)
        res = pthread_key_delete(ethr_ts_event_key__);
    if (res)
        return res;
    pthread_mutex_destroy(&owned_keys_lock);
#endif
    erts_cpu_info_destroy(ethr_cpu_info__);
    ethr_cpu_info__ = NULL;
    res = ethr_mutex_lib_destroy();
    runtime_destroyed = 1;
    ethr_not_completely_inited__ = 1;
    return res;
}

ethr_runtime_t ethr_runtime__
#ifdef __GNUC__
__attribute__ ((aligned (ETHR_CACHE_LINE_SIZE)))
#endif
    ;

#if defined(ETHR_X86_RUNTIME_CONF__)

/*
 * x86/x86_64 specifics shared between windows and
 * pthread implementations.
 */

#define ETHR_IS_X86_VENDOR(V, B, C, D) \
   (sizeof(V) == 13 && is_x86_vendor((V), (B), (C), (D)))

static ETHR_INLINE int
is_x86_vendor(char *str, int ebx, int ecx, int edx)
{
    return (*((int *) &str[0]) == ebx
	    && *((int *) &str[sizeof(int)]) == edx
	    && *((int *) &str[sizeof(int)*2]) == ecx);
}

static void
x86_init(void)
{
    int eax, ebx, ecx, edx;

    eax = ebx = ecx = edx = 0;

    ethr_x86_cpuid__(&eax, &ebx, &ecx, &edx);

    ethr_runtime__.conf.have_cpuid = (eax > 0);

    if (eax > 0
	&& (ETHR_IS_X86_VENDOR("GenuineIntel", ebx, ecx, edx)
	    || ETHR_IS_X86_VENDOR("AuthenticAMD", ebx, ecx, edx)
	    || ETHR_IS_X86_VENDOR("HygonGenuine", ebx, ecx, edx))) {
	eax = 1;
	ethr_x86_cpuid__(&eax, &ebx, &ecx, &edx);
    }
    else {
	/*
	 * The meaning of the feature flags for this
	 * vendor have not been verified.
	 */
	eax = ebx = ecx = edx = 0;
    }

    /*
     * The feature flags tested below have only been verified
     * for vendors checked above. Also note that only these
     * feature flags have been verified to have these specific
     * meanings. If another feature flag test is introduced,
     * it has to be verified to have the same meaning for all
     * vendors above.
     */

#if ETHR_SIZEOF_PTR == 8
    /* bit 13 of ecx is set if we have cmpxchg16b */
    ethr_runtime__.conf.have_dw_cmpxchg = (ecx & (1 << 13));
#elif ETHR_SIZEOF_PTR == 4
    /* bit 8 of edx is set if we have cmpxchg8b */
    ethr_runtime__.conf.have_dw_cmpxchg = (edx & (1 << 8));
#else
#  error "Not supported"
#endif
    /* bit 26 of edx is set if we have sse2 */
    ethr_runtime__.conf.have_sse2 = (edx & (1 << 26));

    /* check if we have extended feature set */
    eax = 0x80000000;
    ethr_x86_cpuid__(&eax, &ebx, &ecx, &edx);

    if (eax < 0x80000001)
        return;

    if (eax >= 0x80000007) {
        /* Advanced Power Management Information */
        eax = 0x80000007;
	ethr_x86_cpuid__(&eax, &ebx, &ecx, &edx);

        /* I got the values below from:
           http://lxr.free-electrons.com/source/arch/x86/include/asm/cpufeature.h
           They can be gotten from the intel/amd manual as well.
        */

        ethr_runtime__.conf.have_constant_tsc  = (edx & (1 <<  8));
        ethr_runtime__.conf.have_tsc_reliable   = (edx & (1 << 23));
        ethr_runtime__.conf.have_nonstop_tsc    = (edx & (1 << 24));
        ethr_runtime__.conf.have_nonstop_tsc_s3 = (edx & (1 << 30));

    }

    /* Extended Processor Info and Feature Bits */
    eax = 0x80000001;
    ethr_x86_cpuid__(&eax, &ebx, &ecx, &edx);

    /* bit 27 of edx is set if we have rdtscp */
    ethr_runtime__.conf.have_rdtscp = (edx & (1 << 27));

}

#endif /* ETHR_X86_RUNTIME_CONF__ */


int
ethr_init_common__(ethr_init_data *id)
{
    int res;

    ethr_init_event__();

#if defined(ETHR_X86_RUNTIME_CONF__)
    x86_init();
#endif

    if (id) {
	ethr_thr_prepare_func__	= id->thread_create_prepare_func;
	ethr_thr_parent_func__	= id->thread_create_parent_func;
	ethr_thr_child_func__	= id->thread_create_child_func;
    }

    ethr_cpu_info__ = erts_cpu_info_create();
    if (!ethr_cpu_info__)
	return ENOMEM;

#ifdef _SC_PAGESIZE
    ethr_pagesize__ = (size_t) sysconf(_SC_PAGESIZE);
#elif defined(HAVE_GETPAGESIZE)
    ethr_pagesize__ = (size_t) getpagesize();
#else
    ethr_pagesize__ = (size_t) 4*1024; /* Guess 4 KB */
#endif

    /* User needs at least 4 KB */
    ethr_min_stack_size__ = 4*1024;
#if SIZEOF_VOID_P == 8
    /* Double that on 64-bit archs */
    ethr_min_stack_size__ *= 2;
#endif
    /* On some systems as much as about 4 KB is used by the system */
    ethr_min_stack_size__ += 4*1024;
    /* There should be room for signal handlers */
#ifdef SIGSTKSZ
    ethr_min_stack_size__ += SIGSTKSZ;
#else
    ethr_min_stack_size__ += ethr_pagesize__;
#endif
    /* The system may think that we need more stack */
#if defined(ETHR_HAVE_USABLE_PTHREAD_STACK_MIN)
    if (ethr_min_stack_size__ < PTHREAD_STACK_MIN)
	ethr_min_stack_size__ = PTHREAD_STACK_MIN;
#elif defined(_SC_THREAD_STACK_MIN)
    {
	size_t thr_min_stk_sz = (size_t) sysconf(_SC_THREAD_STACK_MIN);
	if (ethr_min_stack_size__ < thr_min_stk_sz)
	    ethr_min_stack_size__ = thr_min_stk_sz;
    }
#endif
    /* The guard is at least on some platforms included in the stack size
       passed when creating threads */
#ifdef ETHR_STACK_GUARD_SIZE
    ethr_min_stack_size__ += ETHR_STACK_GUARD_SIZE;
#endif
    ethr_min_stack_size__ = ETHR_PAGE_ALIGN(ethr_min_stack_size__);

    ethr_max_stack_size__ = 32*1024*1024;
#if SIZEOF_VOID_P == 8
    ethr_max_stack_size__ *= 2;
#endif
    ethr_max_stack_size__ = ETHR_PAGE_ALIGN(ethr_max_stack_size__);

    res = ethr_init_atomics();
    if (res != 0)
	return res;

    res = ethr_mutex_lib_init(erts_get_cpu_configured(ethr_cpu_info__));
    if (res != 0)
	return res;

    res = ethr_tsd_key_create(&ethr_stacklimit_key__, "stacklimit");
    if (res != 0)
        return res;

    xhndl_list = NULL;

    return 0;
}

int
ethr_late_init_common__(ethr_late_init_data *lid)
{
    ethr_ts_event *tsep = NULL;
    int reader_groups;
    int res;
    int i;
    ethr_memory_allocator *m[] = {&ethr_mem__.std,
				  &ethr_mem__.sl,
				  &ethr_mem__.ll};
    if (lid)
	ethr_mem__ = lid->mem;
    if (!ethr_mem__.std.alloc
	|| !ethr_mem__.std.realloc
	|| !ethr_mem__.std.free) {
	ethr_mem__.std.alloc = malloc;
	ethr_mem__.std.realloc = realloc;
	ethr_mem__.std.free = free;
    }
    for (i = 0; i < sizeof(m)/sizeof(m[0]); i++) {
	if (!m[i]->alloc || !m[i]->realloc || !m[i]->free) {
	    m[i]->alloc = ethr_mem__.std.alloc;
	    m[i]->realloc = ethr_mem__.std.realloc;
	    m[i]->free = ethr_mem__.std.free;
	}

    }
    res = init_ts_event_alloc();
    if (res != 0)
	return res;
    
    res = ethr_make_ts_event__(&tsep, 0);
    if (res == 0)
	tsep->iflgs |= ETHR_TS_EV_ETHREAD;
    if (!lid) {
	main_threads = 0;
	reader_groups = 0;
    }
    else {
	if (lid->main_threads < 0 || USHRT_MAX < lid->main_threads)
	    return res;
	main_threads = lid->main_threads;
	reader_groups = lid->reader_groups;
    }
    res = ethr_mutex_lib_late_init(reader_groups, main_threads);
    if (res != 0)
	return res;
    ethr_not_completely_inited__ = 0; /* Need it for
					 rwmutex_init */
    res = ethr_rwmutex_init(&xhndl_rwmtx);
    ethr_not_completely_inited__ = 1;
    if (res != 0)
	return res;
    return 0;
}

/*
 * Stack limit
 */

void *ethr_get_stacklimit(void)
{
    return ethr_tsd_get(ethr_stacklimit_key__);
}

int ethr_set_stacklimit(void *limit)
{
    void *prev = ethr_tsd_get(ethr_stacklimit_key__);
    if (prev)
        return EACCES;
    if (!limit)
        return EINVAL;
    return ethr_tsd_set(ethr_stacklimit_key__, limit);
}

/* internal stacklimit (thread creation) */

void
ethr_set_stacklimit__(char *prev_c, size_t stacksize)
{
    /*
     * We *don't* want this function inlined, i.e., it is
     * risky to call this function from another function
     * in ethr_aux.c
     */
    void *limit = NULL;
    char c;
    int res;

    if (stacksize) {
        char *start;
        if (&c > prev_c) {
            start = (char *) ((((ethr_uint_t) prev_c)
                               / ethr_pagesize__)
                              * ethr_pagesize__);
            limit = start + stacksize;
        }
        else {
            start = (char *) (((((ethr_uint_t) prev_c) - 1)
                               / ethr_pagesize__ + 1)
                              * ethr_pagesize__);
            limit = start - stacksize;
        }
    }

    res = ethr_tsd_set(ethr_stacklimit_key__, limit);
    if (res != 0)
        ethr_abort__();
}

int
ethr_install_exit_handler(void (*funcp)(void))
{
    ethr_xhndl_list *xhp;

#if ETHR_XCHK
    if (ethr_not_completely_inited__) {
	ETHR_ASSERT(0);
	return EACCES;
    }
#endif

    if (!funcp)
	return EINVAL;

    xhp = (ethr_xhndl_list *) ethr_mem__.std.alloc(sizeof(ethr_xhndl_list));
    if (!xhp)
	return ENOMEM;

    ethr_rwmutex_rwlock(&xhndl_rwmtx);

    xhp->funcp = funcp;
    xhp->runtime_owned = runtime_active;
    xhp->runtime_bound = runtime_active;
    xhp->static_registration = 0;
    xhp->next = xhndl_list;
    xhndl_list = xhp;

    ethr_rwmutex_rwunlock(&xhndl_rwmtx);

    return 0;
}

int
ethr_install_static_exit_handler(void (*funcp)(void))
{
    ethr_xhndl_list *handler;
    if (!runtime_active || runtime_cleaned || !funcp)
        return EINVAL;
    ethr_rwmutex_rwlock(&xhndl_rwmtx);
    for (handler = xhndl_list; handler; handler = handler->next) {
        if (handler->static_registration && handler->funcp == funcp) {
            handler->runtime_bound = 1;
            ethr_rwmutex_rwunlock(&xhndl_rwmtx);
            return 0;
        }
    }
    handler = ethr_mem__.std.alloc(sizeof(*handler));
    if (!handler) {
        ethr_rwmutex_rwunlock(&xhndl_rwmtx);
        return ENOMEM;
    }
    handler->funcp = funcp;
    handler->runtime_owned = 0;
    handler->runtime_bound = 1;
    handler->static_registration = 1;
    handler->next = xhndl_list;
    xhndl_list = handler;
    ethr_rwmutex_rwunlock(&xhndl_rwmtx);
    return 0;
}

void
ethr_run_exit_handlers__(void)
{
    ethr_xhndl_list *xhp;

    ethr_rwmutex_rlock(&xhndl_rwmtx);

    xhp = xhndl_list;

    ethr_rwmutex_runlock(&xhndl_rwmtx);

    for (; xhp; xhp = xhp->next)
	if (!xhp->static_registration || xhp->runtime_bound)
            (*xhp->funcp)();
}

/*
 * Thread specific event alloc, etc.
 *
 * Note that we don't know when it is safe to destroy an event, but
 * we know when it is safe to reuse it. ts_event_free() therefore
 * never destroys an event (but makes freed events available for
 * reuse).
 *
 * We could easily keep track of the usage of events, and by this
 * make it possible to destroy events. We would however suffer a
 * performance penalty for this and save very little memory.
 */

typedef union {
    ethr_ts_event ts_ev;
    char align[ETHR_CACHE_LINE_ALIGN_SIZE(sizeof(ethr_ts_event))];
} ethr_aligned_ts_event;

typedef struct ethr_ts_event_pool_ {
    struct ethr_ts_event_pool_ *next;
    void *allocation;
    ethr_aligned_ts_event *events;
    int count;
} ethr_ts_event_pool;

static ethr_ts_event_pool *ts_event_pools;

void ethr_runtime_usage(size_t *vm_keys, size_t *system_keys,
                        size_t *vm_callbacks, size_t *system_callbacks,
                        size_t *event_blocks, size_t *event_slots)
{
    ethr_xhndl_list *handler;
    ethr_ts_event_pool *pool;
    *vm_keys = *system_keys = *vm_callbacks = *system_callbacks = 0;
    *event_blocks = *event_slots = 0;
#ifndef __WIN32__
    {
        ethr_owned_key *key;
        for (key = owned_keys; key; key = key->next) {
            if (key->runtime_owned || key->runtime_bound)
                ++*vm_keys;
            if (!key->runtime_owned)
                ++*system_keys;
        }
        /* The thread-event key is created directly by the pthread backend. */
        if (!ethr_not_completely_inited__ && !runtime_destroyed)
            ++*system_keys;
    }
#endif
    for (handler = xhndl_list; handler; handler = handler->next) {
        if (handler->runtime_owned || handler->runtime_bound)
            ++*vm_callbacks;
        if (!handler->runtime_owned)
            ++*system_callbacks;
    }
    for (pool = ts_event_pools; pool; pool = pool->next) {
        ++*event_blocks;
        *event_slots += pool->count;
    }
}

static ethr_mutex ts_ev_alloc_lock;
static ethr_ts_event *free_ts_ev;
static ethr_sint64_t used_events;

static ethr_ts_event *ts_event_pool(int size, ethr_ts_event **endpp)
{
    int i;
    ethr_ts_event_pool *pool;
    ethr_aligned_ts_event *atsev;
    pool = malloc(sizeof(*pool));
    if (!pool)
        return NULL;
    atsev = ethr_mem__.std.alloc(sizeof(ethr_aligned_ts_event) * size
					 + ETHR_CACHE_LINE_SIZE - 1);
    if (!atsev) {
        free(pool);
	return NULL;
    }
    pool->allocation = atsev;
    if ((((ethr_uint_t) atsev) & ETHR_CACHE_LINE_MASK) != 0)
	atsev = ((ethr_aligned_ts_event *)
		 ((((ethr_uint_t) atsev) & ~ETHR_CACHE_LINE_MASK)
			  + ETHR_CACHE_LINE_SIZE));
    pool->events = atsev;
    pool->count = size;
    /* Pool growth occurs outside ts_ev_alloc_lock. Use the independent key
     * registry lock so event allocation cannot recursively acquire itself. */
#ifndef __WIN32__
    pthread_mutex_lock(&owned_keys_lock);
#endif
    pool->next = ts_event_pools;
    ts_event_pools = pool;
#ifndef __WIN32__
    pthread_mutex_unlock(&owned_keys_lock);
#endif
    for (i = 1; i < size; i++) {
	atsev[i-1].ts_ev.next = &atsev[i].ts_ev;
	ethr_atomic32_init(&atsev[i-1].ts_ev.uaflgs, 0);
	atsev[i-1].ts_ev.iflgs = 0;
    }
    ethr_atomic32_init(&atsev[size-1].ts_ev.uaflgs, 0);
    atsev[size-1].ts_ev.iflgs = 0;
    atsev[size-1].ts_ev.next = NULL;
    if (endpp)
	*endpp = &atsev[size-1].ts_ev;
    return &atsev[0].ts_ev;
}

static int init_ts_event_alloc(void)
{
    used_events = 0;
    free_ts_ev = ts_event_pool(ERTS_TS_EV_ALLOC_DEFAULT_POOL_SIZE,
			       NULL);
    if (!free_ts_ev)
	return ENOMEM;
    return ethr_mutex_init(&ts_ev_alloc_lock);
}

static int destroy_ts_event_alloc(void)
{
    ethr_ts_event_pool *pool;
    int res, i;
    if (used_events)
        return EBUSY;
    res = ethr_mutex_destroy(&ts_ev_alloc_lock);
    if (res)
        return res;
    while ((pool = ts_event_pools)) {
        for (i = 0; i < pool->count; i++) {
            ethr_ts_event *event = &pool->events[i].ts_ev;
            if (event->iflgs & ETHR_TS_EV_INITED) {
                res = ethr_event_destroy(&event->event);
                if (res)
                    return res;
            }
        }
        ts_event_pools = pool->next;
        ethr_mem__.std.free(pool->allocation);
        free(pool);
    }
    free_ts_ev = NULL;
    return 0;
}

static ethr_ts_event *ts_event_alloc(void)
{
    ethr_ts_event *ts_ev;
    ethr_mutex_lock(&ts_ev_alloc_lock);
    ETHR_ASSERT(used_events >= 0);
    if (free_ts_ev) {
	ts_ev = free_ts_ev;
	free_ts_ev = ts_ev->next;
        used_events++;
	ethr_mutex_unlock(&ts_ev_alloc_lock);
    }
    else {
	ethr_ts_event *ts_ev_pool_end;
	ethr_mutex_unlock(&ts_ev_alloc_lock);

	ts_ev = ts_event_pool(ERTS_TS_EV_ALLOC_POOL_SIZE, &ts_ev_pool_end);
	if (!ts_ev)
	    return NULL;

	ethr_mutex_lock(&ts_ev_alloc_lock);
	ts_ev_pool_end->next = free_ts_ev;
	free_ts_ev = ts_ev->next;
        used_events++;
	ethr_mutex_unlock(&ts_ev_alloc_lock);
    }
    return ts_ev;
}

static void ts_event_free(ethr_ts_event *ts_ev)
{
    ETHR_ASSERT(!ts_ev->udata);
    ethr_mutex_lock(&ts_ev_alloc_lock);
    ETHR_ASSERT(used_events > 0);
    used_events--;
    ts_ev->next = free_ts_ev;
    free_ts_ev = ts_ev;
    ethr_mutex_unlock(&ts_ev_alloc_lock);
}

static void
init_ts_event_flags(ethr_ts_event **tsepp)
{
    ethr_ts_event *tsep = *tsepp;
    tsep->iflgs = ETHR_TS_EV_INITED;
    tsep->udata = NULL;
    tsep->rgix = 0;
    tsep->mtix = 0;
}

ethr_sint64_t
ethr_no_used_tse(void)
{
    ethr_sint64_t res;
    ethr_mutex_lock(&ts_ev_alloc_lock);
    ETHR_ASSERT(used_events >= 0);
    res = used_events;
    ethr_mutex_unlock(&ts_ev_alloc_lock);
    return res;
}


int ethr_make_ts_event__(ethr_ts_event **tsepp, int tmp)
{
    int res;
    ethr_ts_event *tsep;

    tsep = ts_event_alloc();
    if (!tsep)
        return ENOMEM;

    if (*tsepp) {
        *tsep = **tsepp;
	ETHR_ASSERT(tsep->iflgs & ETHR_TS_EV_BUSY);
        tsep->iflgs &= ~ETHR_TS_EV_BUSY;
        tsep->iflgs |= ETHR_TS_EV_TMP;
    }
    else {
        init_ts_event_flags(&tsep);

        if (tmp) {
            tsep->iflgs |= ETHR_TS_EV_TMP;
        }
        else {
            res = ethr_set_tse__(tsep);
            if (res != 0 && tsepp && *tsepp) {
                ts_event_free(tsep);
                return res;
            }
        }
    }
    
    res = ethr_event_init(&tsep->event);
    if (res != 0) {
        ts_event_free(tsep);
        return res;
    }

    if (tsepp)
	*tsepp = tsep;

    return 0;
}

int ethr_free_ts_event__(ethr_ts_event *tsep)
{
    int res = ethr_event_destroy(&tsep->event);
    if (res == 0)
        ts_event_free(tsep);
    return res;
}

void ethr_ts_event_destructor__(void *vtsep)
{
    if (vtsep) {
	ethr_ts_event *tsep = (ethr_ts_event *) vtsep;
	ts_event_free(tsep);
	ethr_set_tse__(NULL);
    }
}

int ethr_set_main_thr_status(int on, int no)
{
    ethr_ts_event *tsep = ethr_get_tse__();
    if (!tsep)
	return EINVAL;
    if (on) {
	if (no < 1 || main_threads < no)
	    return EINVAL;
	tsep->mtix = (unsigned short) no;
	tsep->iflgs |= ETHR_TS_EV_MAIN_THR;
    }
    else {
	tsep->iflgs &= ~ETHR_TS_EV_MAIN_THR;
	tsep->mtix = (unsigned short) 0;
    }
    return 0;
}

int ethr_get_main_thr_status(int *on)
{
    ethr_ts_event *tsep = ethr_get_tse__();
    if (!tsep)
	*on = 0;
    else {
	if (tsep->iflgs & ETHR_TS_EV_MAIN_THR)
	    *on = 1;
	else
	    *on = 0;
    }
    return 0;
}

/* Spinlocks and rwspinlocks */

int
ethr_spinlock_init(ethr_spinlock_t *lock)
{
#if ETHR_XCHK 
    if (!lock) {
	ETHR_ASSERT(0);
	return EINVAL;
    }
#endif
    return ethr_spinlock_init__(lock);
}

int
ethr_spinlock_destroy(ethr_spinlock_t *lock)
{
#if ETHR_XCHK 
    if (ethr_not_inited__) {
	ETHR_ASSERT(0);
	return EACCES;
    }
    if (!lock) {
	ETHR_ASSERT(0);
	return EINVAL;
    }
#endif
    return ethr_spinlock_destroy__(lock);
}

void
ethr_spin_unlock(ethr_spinlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_spin_unlock__(lock);
}

void
ethr_spin_lock(ethr_spinlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_spin_lock__(lock);
}

int
ethr_rwlock_init(ethr_rwlock_t *lock)
{
#if ETHR_XCHK 
    if (!lock) {
	ETHR_ASSERT(0);
	return EINVAL;
    }
#endif
    return ethr_rwlock_init__(lock);
}

int
ethr_rwlock_destroy(ethr_rwlock_t *lock)
{
#if ETHR_XCHK 
    if (ethr_not_inited__) {
	ETHR_ASSERT(0);
	return EACCES;
    }
    if (!lock) {
	ETHR_ASSERT(0);
	return EINVAL;
    }
#endif
    return ethr_rwlock_destroy__(lock);
}

void
ethr_read_unlock(ethr_rwlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_read_unlock__(lock);
}

void
ethr_read_lock(ethr_rwlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_read_lock__(lock);
}

void
ethr_write_unlock(ethr_rwlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_write_unlock__(lock);
}

void
ethr_write_lock(ethr_rwlock_t *lock)
{
    ETHR_ASSERT(!ethr_not_inited__);
    ETHR_ASSERT(lock);
    ethr_write_lock__(lock);
}

ETHR_IMPL_NORETURN__ ethr_fatal_error__(const char *file,
					int line,
					const char *func,
					int err)
{
    const char *errstr;
    if (err == ENOTSUP)
	errstr = "Operation not supported";
    else {
	errstr = strerror(err);
	if (!errstr)
	    errstr = "Unknown error";
    }
    fprintf(stderr, "%s:%d: Fatal error in %s(): %s (%d)\n",
	    file, line, func, errstr, err);
    ethr_abort__();
}

ETHR_IMPL_NORETURN__
ethr_assert_failed(const char *file, int line, const char *func, const char *a)
{
    fprintf(stderr, "%s:%d: %s(): Assertion failed: %s\n", file, line, func, a);
    ethr_abort__();
}
