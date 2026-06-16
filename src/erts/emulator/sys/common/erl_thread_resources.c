/* SPDX-License-Identifier: Apache-2.0 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "global.h"
#include "erl_threads.h"
#include "beam_platform.h"

/* The registry uses host allocations and a native lock: registering an ERTS
 * lock must never allocate through, or acquire, another registered lock. */
typedef struct ErtsThreadResource_ {
    struct ErtsThreadResource_ *next;
    void *resource;
    ErtsThrResourceType type;
    int vm_owned;
    unsigned long serial;
} ErtsThreadResource;

#define THREAD_RESOURCE_BUCKETS 1024
static beam_mutex_t thread_resource_lock = BEAM_MUTEX_INITIALIZER;
static ErtsThreadResource *thread_resources[THREAD_RESOURCE_BUCKETS];
static unsigned long thread_resource_serial;

static unsigned int thread_resource_bucket(void *resource)
{
    return ((UWord) resource >> 4) % THREAD_RESOURCE_BUCKETS;
}

void erts_thr_resource_register(void *resource, ErtsThrResourceType type)
{
    unsigned int bucket = thread_resource_bucket(resource);
    ErtsThreadResource *entry = malloc(sizeof(*entry));
    if (!entry)
        erts_thr_fatal_error(ENOMEM, "record native lock ownership");
    entry->resource = resource;
    entry->type = type;
    entry->vm_owned = ethr_runtime_is_active();
    beam_mutex_lock(&thread_resource_lock);
    entry->serial = ++thread_resource_serial;
    entry->next = thread_resources[bucket];
    thread_resources[bucket] = entry;
    beam_mutex_unlock(&thread_resource_lock);
}

void erts_thr_resource_unregister(void *resource)
{
    unsigned int bucket = thread_resource_bucket(resource);
    ErtsThreadResource **link, *entry = NULL;
    beam_mutex_lock(&thread_resource_lock);
    for (link = &thread_resources[bucket]; *link; link = &(*link)->next) {
        if ((*link)->resource == resource) {
            entry = *link;
            *link = entry->next;
            break;
        }
    }
    beam_mutex_unlock(&thread_resource_lock);
    free(entry);
}

static int thread_resource_reverse_order(const void *left, const void *right)
{
    const ErtsThreadResource *a = *(ErtsThreadResource *const *) left;
    const ErtsThreadResource *b = *(ErtsThreadResource *const *) right;
    return a->serial < b->serial ? 1 : a->serial > b->serial ? -1 : 0;
}

static void destroy_thread_resources(int vm_owned)
{
    unsigned int bucket;
    size_t count = 0, index = 0;
    ErtsThreadResource **resources;
    for (bucket = 0; bucket < THREAD_RESOURCE_BUCKETS; bucket++) {
        ErtsThreadResource *entry;
        for (entry = thread_resources[bucket]; entry; entry = entry->next)
            if (entry->vm_owned == vm_owned)
                count++;
    }
    if (!count)
        return;
    resources = malloc(count * sizeof(*resources));
    if (!resources)
        erts_thr_fatal_error(ENOMEM, "release native lock ownership");
    for (bucket = 0; bucket < THREAD_RESOURCE_BUCKETS; bucket++) {
        ErtsThreadResource **link = &thread_resources[bucket];
        while (*link) {
            ErtsThreadResource *entry = *link;
            if (entry->vm_owned != vm_owned) {
                link = &entry->next;
                continue;
            }
            *link = entry->next;
            resources[index++] = entry;
        }
    }
    /* Allocator and diagnostic locks precede subsystem locks at creation and
     * remain usable until the locks depending on them have been released. */
    qsort(resources, count, sizeof(*resources), thread_resource_reverse_order);
    for (index = 0; index < count; index++) {
        ErtsThreadResource *entry = resources[index];
        switch (entry->type) {
        case ERTS_THR_RESOURCE_MUTEX: {
            erts_mtx_t *mtx = entry->resource;
#ifdef DEBUG
            mtx->flags &= ~ERTS_LOCK_FLAGS_PROPERTY_STATIC;
#endif
            erts_mtx_destroy(mtx);
            break;
        }
        case ERTS_THR_RESOURCE_COND:
            erts_cnd_destroy(entry->resource);
            break;
        case ERTS_THR_RESOURCE_RWMUTEX: {
            erts_rwmtx_t *rwmtx = entry->resource;
#ifdef DEBUG
            rwmtx->flags &= ~ERTS_LOCK_FLAGS_PROPERTY_STATIC;
#endif
            erts_rwmtx_destroy(rwmtx);
            break;
        }
        case ERTS_THR_RESOURCE_SPINLOCK: {
            erts_spinlock_t *lock = entry->resource;
#ifdef DEBUG
            lock->flags &= ~ERTS_LOCK_FLAGS_PROPERTY_STATIC;
#endif
            erts_spinlock_destroy(lock);
            break;
        }
        case ERTS_THR_RESOURCE_RWLOCK: {
            erts_rwlock_t *lock = entry->resource;
#ifdef DEBUG
            lock->flags &= ~ERTS_LOCK_FLAGS_PROPERTY_STATIC;
#endif
            erts_rwlock_destroy(lock);
            break;
        }
        }
        free(entry);
    }
    free(resources);
}

void erts_sys_resource_usage(size_t *vm_locks, size_t *system_locks)
{
    size_t bucket;
    ErtsThreadResource *entry;
    /* Called by the owner only while no VM worker can mutate the registry. */
    *vm_locks = *system_locks = 0;
    for (bucket = 0; bucket < THREAD_RESOURCE_BUCKETS; bucket++) {
        for (entry = thread_resources[bucket]; entry; entry = entry->next) {
            if (entry->vm_owned)
                ++*vm_locks;
            else
                ++*system_locks;
        }
    }
}

void erts_sys_vm_cleanup_locks(void)
{
    destroy_thread_resources(1);
}

int erts_sys_system_cleanup(void)
{
    int res = ethr_runtime_system_cleanup();
    if (res)
        return res;
    destroy_thread_resources(0);
    res = ethr_runtime_destroy();
    if (!res)
        res = beam_mutex_destroy(&thread_resource_lock);
    return res;
}
