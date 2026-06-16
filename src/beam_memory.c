/* SPDX-License-Identifier: Apache-2.0 */
#include "beam_memory.h"

#include <assert.h>
#include "beam_platform.h"
#include <stdint.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif

/* This ledger owns backing allocations, including carriers that migrate into
 * shared pools. It never walks allocator lists or sends deferred work during
 * reclamation. Semantic finalizers and native lock destruction run first. */
typedef struct allocation {
    struct allocation *next;
    void *base;
    size_t size;
} allocation;

#define ALLOCATION_BUCKETS 4096
static allocation *allocations[ALLOCATION_BUCKETS];
static allocation *mappings;
static beam_mutex_t memory_mutex = BEAM_MUTEX_INITIALIZER;
static int active;
static int system_destroyed;
static size_t allocation_count, allocated_size, mapping_count, mapped_size;

static size_t bucket(void *ptr)
{
    return ((uintptr_t) ptr >> 4) % ALLOCATION_BUCKETS;
}

static allocation *record(void *ptr, size_t size)
{
    allocation *entry = malloc(sizeof(*entry));
    /* Native allocator initialization already treats metadata OOM as fatal.
     * Losing the ownership record would silently make teardown incomplete. */
    if (!entry)
        abort();
    entry->base = ptr;
    entry->size = size;
    entry->next = NULL;
    return entry;
}

static void track_allocation(void *ptr, size_t size)
{
    if (active && ptr) {
        allocation *entry = record(ptr, size);
        size_t index = bucket(ptr);
        entry->next = allocations[index];
        allocations[index] = entry;
        allocation_count++;
        allocated_size += size;
    }
}

static void forget_allocation(void *ptr)
{
    allocation **link = &allocations[bucket(ptr)];
    while (*link) {
        allocation *entry = *link;
        if (entry->base == ptr) {
            *link = entry->next;
            allocation_count--;
            allocated_size -= entry->size;
            free(entry);
            return;
        }
        link = &entry->next;
    }
}

void beam_runtime_memory_begin(void)
{
    assert(!active && !allocation_count && !mapping_count);
    active = 1;
}

void beam_runtime_track_allocation(void *ptr, size_t size)
{
    beam_mutex_lock(&memory_mutex);
    track_allocation(ptr, size);
    beam_mutex_unlock(&memory_mutex);
}

void beam_runtime_forget_allocation(void *ptr)
{
    beam_mutex_lock(&memory_mutex);
    forget_allocation(ptr);
    beam_mutex_unlock(&memory_mutex);
}

void *beam_runtime_malloc(size_t size)
{
    void *ptr = malloc(size);
    beam_runtime_track_allocation(ptr, size);
    return ptr;
}

void *beam_runtime_calloc(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    beam_runtime_track_allocation(ptr, count * size);
    return ptr;
}

void *beam_runtime_realloc(void *ptr, size_t size)
{
    void *result;
    allocation **link;
    beam_mutex_lock(&memory_mutex);
    link = &allocations[bucket(ptr)];
    while (*link && (*link)->base != ptr)
        link = &(*link)->next;
    result = realloc(ptr, size);
    if (result || !size) {
        if (*link) {
            allocation *entry = *link;
            *link = entry->next;
            allocation_count--;
            allocated_size -= entry->size;
            free(entry);
        }
        track_allocation(result, size);
    }
    beam_mutex_unlock(&memory_mutex);
    return result;
}

void beam_runtime_free(void *ptr)
{
    beam_runtime_forget_allocation(ptr);
    free(ptr);
}

void beam_runtime_track_mapping(void *ptr, size_t size)
{
    beam_mutex_lock(&memory_mutex);
    if (active && ptr && size) {
        allocation *entry = record(ptr, size);
        entry->next = mappings;
        mappings = entry;
        mapping_count++;
        mapped_size += size;
    }
    beam_mutex_unlock(&memory_mutex);
}

void beam_runtime_forget_mapping(void *ptr, size_t size)
{
    uintptr_t begin = (uintptr_t) ptr, end = begin + size;
    allocation **link;
    beam_mutex_lock(&memory_mutex);
    link = &mappings;
    while (*link) {
        allocation *entry = *link;
        uintptr_t first = (uintptr_t) entry->base;
        uintptr_t last = first + entry->size;
        uintptr_t from = first > begin ? first : begin;
        uintptr_t to = last < end ? last : end;
        if (from < to) {
            mapped_size -= to - from;
            if (from == first && to == last) {
                *link = entry->next;
                mapping_count--;
                free(entry);
                continue;
            } else if (from == first) {
                entry->base = (void *) to;
                entry->size = last - to;
            } else {
                entry->size = from - first;
                if (to != last) {
                    allocation *tail = record((void *) to, last - to);
                    tail->next = entry->next;
                    entry->next = tail;
                    mapping_count++;
                }
            }
        }
        link = &entry->next;
    }
    beam_mutex_unlock(&memory_mutex);
}

void beam_runtime_memory_usage(size_t *count, size_t *bytes,
                               size_t *maps, size_t *map_bytes)
{
    if (system_destroyed) {
        *count = *bytes = *maps = *map_bytes = 0;
        return;
    }
    beam_mutex_lock(&memory_mutex);
    *count = allocation_count;
    *bytes = allocated_size;
    *maps = mapping_count;
    *map_bytes = mapped_size;
    beam_mutex_unlock(&memory_mutex);
}

void beam_runtime_memory_destroy(void)
{
    size_t index;
    assert(active);
    /* The caller has joined workers, destroyed term roots, executable code,
     * native objects, TLS values and locks. No allocator operation follows. */
    for (index = 0; index < ALLOCATION_BUCKETS; index++) {
        allocation *entry = allocations[index];
        while (entry) {
            allocation *next = entry->next;
            free(entry->base);
            free(entry);
            entry = next;
        }
        allocations[index] = NULL;
    }
    while (mappings) {
        allocation *entry = mappings;
        mappings = entry->next;
#ifdef _WIN32
        if (!VirtualFree(entry->base, 0, MEM_RELEASE))
#else
        if (munmap(entry->base, entry->size) != 0)
#endif
            abort();
        free(entry);
    }
    allocation_count = allocated_size = mapping_count = mapped_size = 0;
    active = 0;
}

void beam_runtime_memory_system_destroy(void)
{
    assert(!active && !allocation_count && !mapping_count);
    if (beam_mutex_destroy(&memory_mutex) != 0)
        abort();
    system_destroyed = 1;
}
