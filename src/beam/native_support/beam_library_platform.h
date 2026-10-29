/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BEAM_LIBRARY_PLATFORM_H
#define BEAM_LIBRARY_PLATFORM_H
#include "beam_library_config.h"
#include <erl_nif.h>
#include <beam/native.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#endif

typedef unsigned char byte;
typedef uintptr_t Uint;
typedef intptr_t Sint;
typedef uint64_t Uint64;
typedef int64_t Sint64;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint16_t Uint16;
typedef int16_t Sint16;
#define ASSERT(x) assert(x)
#define ERTS_INLINE inline
#define ERTS_CT_ASSERT(x) _Static_assert(x,#x)
#define ERTS_SINT64_MAX INT64_MAX
#define ERTS_INTERNAL_ERROR(m) beam_native_fail(BEAM_NATIVE_FAILURE_ABORT,"%s",m)
#define ERTS_DUMP_EXIT BEAM_NATIVE_FAILURE_DUMP
#define sys_memcpy memcpy
#define sys_memset memset
#define sys_memzero(p,n) memset(p,0,n)
#define MAX(A,B) ((A)>(B)?(A):(B))
#define MIN(A,B) ((A)<(B)?(A):(B))

static inline uint16_t beam_library_read_u16(const unsigned char *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}
static inline void beam_library_write_u16(unsigned value, unsigned char *p)
{
    p[0] = (unsigned char)(value >> 8); p[1] = (unsigned char)value;
}
#ifndef _WIN32
static inline int beam_library_nonblocking(int fd, int enabled)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}
#define SET_NONBLOCKING(fd) beam_library_nonblocking(fd, 1)
#define SET_BLOCKING(fd) beam_library_nonblocking(fd, 0)
#define ERRNO_BLOCK EAGAIN
#endif

/* Library-owned atomic state; sequential consistency preserves the stronger
 * acquire/release requirements of all queue and descriptor transitions. */
typedef _Atomic int32_t beam_atomic32_t;
static inline int32_t beam_atomic32_cmpxchg(beam_atomic32_t *p,int32_t value,int32_t expected)
{
    atomic_compare_exchange_strong(p,&expected,value);return expected;
}
#define beam_atomic32_cmpxchg_acqb beam_atomic32_cmpxchg
#define beam_atomic32_cmpxchg_relb beam_atomic32_cmpxchg
#define beam_atomic32_cmpxchg_nob beam_atomic32_cmpxchg
#define beam_atomic32_init_nob(p,v) atomic_init(p,v)
#define beam_atomic32_init_acqb(p,v) atomic_init(p,v)
#define beam_atomic32_read_nob(p) atomic_load(p)
#define beam_atomic32_read_acqb(p) atomic_load(p)
#define beam_atomic32_set_acqb(p,v) atomic_store(p,v)
#define beam_atomic32_set_relb(p,v) atomic_store(p,v)
#define beam_atomic32_inc_read_acqb(p) (atomic_fetch_add(p,1)+1)
#define beam_atomic32_dec_read_relb(p) (atomic_fetch_sub(p,1)-1)
#endif
