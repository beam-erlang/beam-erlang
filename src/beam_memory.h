/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_MEMORY_H
#define BEAM_MEMORY_H

#include <stddef.h>

/* Private runtime implementation interface; not installed.
 * Backing storage ownership, independent of the ERTS allocator that uses it.
 * Begin/end are owner-thread operations at quiescent lifecycle boundaries. */
void beam_runtime_memory_begin(void);
void beam_runtime_memory_destroy(void);
void beam_runtime_memory_system_destroy(void);
void *beam_runtime_malloc(size_t size);
void *beam_runtime_calloc(size_t count, size_t size);
void *beam_runtime_realloc(void *ptr, size_t size);
void beam_runtime_free(void *ptr);
void beam_runtime_track_allocation(void *ptr, size_t size);
void beam_runtime_forget_allocation(void *ptr);
void beam_runtime_track_mapping(void *ptr, size_t size);
void beam_runtime_forget_mapping(void *ptr, size_t size);
void beam_runtime_memory_usage(size_t *allocations, size_t *bytes,
                               size_t *mappings, size_t *mapped_bytes);

#endif
