# config.h, erl_int_sizes_config.h, and ethread_header_config.h.
#
# Unix probes, then the Linux glibc seed used by a vanilla 64-bit build.
# Kernel poll is epoll and is required. Termcap is linked when the
# toolchain provides tgetent and a usable header.

include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCSourceCompiles)

find_package(Threads REQUIRED)
if(NOT CMAKE_USE_PTHREADS_INIT)
    message(FATAL_ERROR "pthread is required")
endif()
set(ETHR_PTHREADS 1)

check_type_size("char"       SIZEOF_CHAR)
check_type_size("short"      SIZEOF_SHORT)
check_type_size("int"        SIZEOF_INT)
check_type_size("long"       SIZEOF_LONG)
check_type_size("long long"  SIZEOF_LONG_LONG)
check_type_size("void*"      SIZEOF_VOID_P)
check_type_size("off_t"      SIZEOF_OFF_T)
check_type_size("size_t"     SIZEOF_SIZE_T)
check_type_size("time_t"     SIZEOF_TIME_T)
check_type_size("__int128_t" SIZEOF___INT128_T)
check_type_size("__int64"    SIZEOF___INT64)
check_type_size("_Float16"   SIZEOF__FLOAT16)

macro(erts_sizeof_or_zero name)
    if(NOT ${name})
        set(${name} 0)
    endif()
endmacro()
erts_sizeof_or_zero(SIZEOF_CHAR)
erts_sizeof_or_zero(SIZEOF_SHORT)
erts_sizeof_or_zero(SIZEOF_INT)
erts_sizeof_or_zero(SIZEOF_LONG)
erts_sizeof_or_zero(SIZEOF_LONG_LONG)
erts_sizeof_or_zero(SIZEOF_VOID_P)
erts_sizeof_or_zero(SIZEOF_OFF_T)
erts_sizeof_or_zero(SIZEOF_SIZE_T)
erts_sizeof_or_zero(SIZEOF_TIME_T)
erts_sizeof_or_zero(SIZEOF___INT128_T)
erts_sizeof_or_zero(SIZEOF___INT64)
erts_sizeof_or_zero(SIZEOF__FLOAT16)

if(NOT SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "A 64-bit target is required (SIZEOF_VOID_P=${SIZEOF_VOID_P})")
endif()

if(CMAKE_C_BYTE_ORDER MATCHES "BIG_ENDIAN")
    set(ERTS_BIGENDIAN 1)
    set(ERTS_ENDIANNESS 1)
else()
    set(ERTS_BIGENDIAN 0)
    set(ERTS_ENDIANNESS -1)
endif()

set(HAVE_DECL_DAYLIGHT 0)
set(HAVE_DECL_IN6ADDR_ANY_INIT 0)
set(HAVE_DECL_IN6ADDR_LOOPBACK_INIT 0)
set(HAVE_DECL_IPV6_V6ONLY 0)

set(ETHR_TRUST_GCC_ATOMIC_BUILTINS_MEMORY_BARRIERS 0)
set(ERTS_STRUCTURE_ALIGNED_ALLOC 1)
set(ERTS_ENABLE_MSACC 1)
set(ERTS_RESTRICT restrict)
set(ASSUMED_CACHE_LINE_SIZE 64)
set(ETHR_HAVE_ETHREAD_DEFINES 1)
set(ETHR_SMP_REQUIRE_NATIVE_IMPLS 1)

set(ERTS_USE_BUILTIN_ZLIB 1)
set(ERTS_USE_BUILTIN_RYU 1)
set(ERTS_USE_BUILTIN_ZSTD 1)

check_include_file("pthread.h" HAVE_PTHREAD_H)
if(HAVE_PTHREAD_H)
    set(HAVE_PTHREAD_H 1)
    set(ETHR_HAVE_PTHREAD_H 1)
endif()
check_include_file("sched.h" HAVE_SCHED_H)
if(HAVE_SCHED_H)
    set(HAVE_SCHED_H 1)
    set(ETHR_HAVE_SCHED_H 1)
endif()

check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
if(HAVE_CLOCK_GETTIME)
    set(HAVE_CLOCK_GETTIME 1)
    set(OS_SYSTEM_TIME_USING_CLOCK_GETTIME 1)
    set(OS_MONOTONIC_TIME_USING_CLOCK_GETTIME 1)
endif()
check_symbol_exists(clock_getres "time.h" HAVE_CLOCK_GETRES)
if(HAVE_CLOCK_GETRES)
    set(HAVE_CLOCK_GETRES 1)
endif()

# Open-file metadata is used by both native file components, including the
# preprocessor's external documentation reader.
check_symbol_exists(fstat "sys/types.h;sys/stat.h;unistd.h" HAVE_FSTAT)

check_symbol_exists(pthread_spin_lock "pthread.h" ETHR_HAVE_PTHREAD_SPIN_LOCK)
if(ETHR_HAVE_PTHREAD_SPIN_LOCK)
    set(ETHR_HAVE_PTHREAD_SPIN_LOCK 1)
endif()

# Match the inet driver's multicast capability contract. Its inherited socket
# option handling (including SO_LINGER during TCP accept) uses this definition.
check_c_source_compiles([=[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #if !defined(IP_MULTICAST_TTL) || !defined(IP_MULTICAST_LOOP) || !defined(IP_MULTICAST_IF) || !defined(IP_ADD_MEMBERSHIP) || !defined(IP_DROP_MEMBERSHIP)
    #error Missing IPv4 multicast options
    #endif
    int main(void) { return 0; }
]=] HAVE_MULTICAST_SUPPORT)

# Linux glibc seed for a vanilla build.
set(ETHR_HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP 1)
set(ETHR_HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP 1)
set(ETHR_HAVE_PTHREAD_YIELD 1)
set(ETHR_HAVE_SCHED_YIELD 1)
set(ETHR_SCHED_YIELD_RET_INT 1)

set(ETHR_HAVE_LINUX_FUTEX 1)
set(ETHR_HAVE___sync_synchronize ~0)
set(ETHR_HAVE___sync_add_and_fetch 12)
set(ETHR_HAVE___sync_fetch_and_and 12)
set(ETHR_HAVE___sync_fetch_and_or 12)
set(ETHR_HAVE___sync_val_compare_and_swap 12)
set(ETHR_HAVE___atomic_store_n 12)
set(ETHR_HAVE___atomic_load_n 12)
set(ETHR_HAVE___atomic_add_fetch 12)
set(ETHR_HAVE___atomic_fetch_and 12)
set(ETHR_HAVE___atomic_fetch_or 12)
set(ETHR_HAVE___atomic_compare_exchange_n 12)
set(ETHR_HAVE_GCC___ATOMIC_BUILTINS 1)
if(ERTS_TARGET_ARCH STREQUAL "x86")
    set(ETHR_GCC_HAVE_DW_CMPXCHG_ASM_SUPPORT 1)
endif()

set(ETHR_HAVE_CLOCK_GETTIME_MONOTONIC 1)
set(ETHR_HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC 1)
set(ETHR_MONOTONIC_CLOCK_ID CLOCK_MONOTONIC)

set(HAVE_DECL_DAYLIGHT 1)
set(HAVE_DECL_IN6ADDR_ANY_INIT 1)
set(HAVE_DECL_IN6ADDR_LOOPBACK_INIT 1)
set(HAVE_DECL_IPV6_V6ONLY 1)
set(HAVE_PTHREAD_H 1)
set(HAVE_SYS_TIME_H 1)
set(HAVE_CLOCK_GETTIME_MONOTONIC_RAW 1)
set(HAVE_CLOCK_GETTIME_CPU_TIME 1)
set(HAVE_SYS_TYPES_H 1)
set(HAVE_SYS_SOCKET_H 1)
set(HAVE_SYS_MMAN_H 1)
set(HAVE_SYS_UIO_H 1)
set(HAVE_SYS_UN_H 1)
set(HAVE_SYS_IOCTL_H 1)
set(HAVE_FCNTL_H 1)
set(HAVE_DLFCN_H 1)
set(HAVE_DLOPEN 1)
set(HAVE_MMAP 1)
set(HAVE_MREMAP 1)
set(HAVE_POSIX_MEMALIGN 1)
set(HAVE_POLL 1)
set(HAVE_POLL_H 1)
set(HAVE_WRITEV 1)
set(HAVE_SENDFILE 1)
set(HAVE_SOCKLEN_T 1)
set(HAVE_GETADDRINFO 1)
set(HAVE_GETNAMEINFO 1)
set(HAVE_GETIFADDRS 1)
set(HAVE_IF_NAMETOINDEX 1)
set(HAVE_IFADDRS_H 1)
set(HAVE_LIBDL 1)
set(HAVE_UNISTD_H 1)
set(HAVE_STDLIB_H 1)
set(HAVE_SYSLOG_H 1)
set(HAVE_STDIO_H 1)
set(HAVE_STRING_H 1)
set(HAVE_ERRNO_H 1)
set(HAVE_STRERROR 1)
set(HAVE_GETTIMEOFDAY 1)
set(NO_SA_LEN 1)
set(HAVE_DIRENT_H 1)
set(HAVE_NET_IF_H 1)
set(HAVE_ARPA_INET_H 1)
set(HAVE_NETINET_IN_H 1)
set(HAVE_NETINET_TCP_H 1)
set(HAVE_NETDB_H 1)
set(HAVE_NETPACKET_PACKET_H 1)
set(HAVE_LINUX_FALLOC_H 1)
set(HAVE_POSIX_FALLOCATE 1)
set(HAVE_FALLOCATE 1)
set(HAVE_IN6 1)
set(HAVE_IPV6 1)
set(HAVE_IN6ADDR_ANY 1)
set(HAVE_IN6ADDR_LOOPBACK 1)
set(HAVE_INET_PTON 1)
set(HAVE_IF_NAMEINDEX 1)
set(HAVE_IF_INDEXTONAME 1)
set(HAVE_IF_FREENAMEINDEX 1)
set(HAVE_STRUCT_SOCKADDR_UN_SUN_PATH 1)
set(HAVE_RECVMMSG 1)
set(HAVE_SENDMMSG 1)
set(ESOCK_USE_HWADDR 1)
set(ESOCK_USE_IFINDEX 1)
set(ESOCK_USE_IFMAP 1)
set(MONOTONIC_CLOCK_ID CLOCK_MONOTONIC)
set(MONOTONIC_CLOCK_ID_STR CLOCK_MONOTONIC)
set(WALL_CLOCK_ID CLOCK_REALTIME)
set(WALL_CLOCK_ID_STR CLOCK_REALTIME)
set(ERTS_USE_POLL 1)
set(ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME 1)
set(ERTS_EMU_CMDLINE_FLAGS " -D_GNU_SOURCE")

# gcc may not report _Float16 without extra flags.
set(SIZEOF__FLOAT16 2)
set(FLOAT16_IS_CONVERTIBLE 1)

check_include_file("sys/epoll.h" _erts_have_sys_epoll_h)
if(NOT _erts_have_sys_epoll_h)
    message(FATAL_ERROR "sys/epoll.h is required")
endif()
set(HAVE_SYS_EPOLL_H 1)
set(ERTS_ENABLE_KERNEL_POLL 1)

foreach(_erts_termcap_lib tinfo ncurses curses termcap termlib)
    check_library_exists(${_erts_termcap_lib} tgetent ""
        ERTS_HAVE_TGETENT_${_erts_termcap_lib})
    if(ERTS_HAVE_TGETENT_${_erts_termcap_lib})
        set(ERTS_TERMCAP_LIB ${_erts_termcap_lib})
        break()
    endif()
endforeach()
if(ERTS_TERMCAP_LIB)
    check_include_file("termcap.h" HAVE_TERMCAP_H)
    check_include_file("ncurses/curses.h" HAVE_NCURSES_CURSES_H)
    check_include_file("curses.h" HAVE_CURSES_H)
    check_include_file("term.h" HAVE_TERM_H)
    if(HAVE_NCURSES_CURSES_H OR (HAVE_CURSES_H AND HAVE_TERM_H))
        set(HAVE_TERMCAP 1)
        message(STATUS "Termcap: ${ERTS_TERMCAP_LIB}")
    else()
        unset(ERTS_TERMCAP_LIB)
        message(STATUS "Termcap: library found, headers unusable")
    endif()
else()
    message(STATUS "Termcap: not found")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/erts/config.h
    @ONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/erl_int_sizes_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/erts/include/erl_int_sizes_config.h
    @ONLY
)

# erl_drv_nif.h includes "erl_int_sizes_config.h" with -I pointing at
# the erts binary directory, ahead of include/.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/erl_int_sizes_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/erts/erl_int_sizes_config.h
    @ONLY
)

set(ETHR_SIZEOF_PTR ${SIZEOF_VOID_P})
set(ETHR_SIZEOF_INT ${SIZEOF_INT})
set(ETHR_SIZEOF_LONG ${SIZEOF_LONG})
set(ETHR_SIZEOF_LONG_LONG ${SIZEOF_LONG_LONG})
set(ETHR_SIZEOF___INT128_T ${SIZEOF___INT128_T})
set(ETHR_SIZEOF___INT64 ${SIZEOF___INT64})

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/ethread_header_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/erts/include/internal/ethread_header_config.h
    @ONLY
)

message(STATUS "Core headers: ${CMAKE_CURRENT_BINARY_DIR}/erts/")
