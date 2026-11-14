# config.h, erl_int_sizes_config.h, and ethread_header_config.h.
#
# Platform configuration for native 64-bit builds. Unix requires kernel poll;
# Windows uses native Win32 threads and WaitForMultipleObjects instead.
# Unix termcap is linked when the toolchain provides usable headers and tgetent.

include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCSourceCompiles)
include(CMakePushCheckState)

find_package(Threads REQUIRED)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(NOT CMAKE_USE_WIN32_THREADS_INIT)
        message(FATAL_ERROR "Windows builds require the Win32 thread library")
    endif()
    set(ETHR_WIN32_THREADS 1)
else()
    if(NOT CMAKE_USE_PTHREADS_INIT)
        message(FATAL_ERROR "pthread is required")
    endif()
    set(ETHR_PTHREADS 1)
endif()

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
set(WORDS_BIGENDIAN ${ERTS_BIGENDIAN})

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

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(${CMAKE_CURRENT_LIST_DIR}/ConfigureWindows.cmake)
else()
check_c_source_compiles("
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #if !defined(IP_MULTICAST_TTL) || !defined(IP_MULTICAST_LOOP) || !defined(IP_MULTICAST_IF) || !defined(IP_ADD_MEMBERSHIP) || !defined(IP_DROP_MEMBERSHIP)
    #error Multicast socket options are unavailable
    #endif
    int main(void) { return 0; }
" HAVE_MULTICAST_SUPPORT)

# ARM JIT cache maintenance requires native data/instruction cache operations
# and an instruction barrier on every scheduler, including on Linux.
if(ERTS_TARGET_ARCH STREQUAL "arm")
    foreach(_instruction IN ITEMS DMB DMB_LD DMB_ST ISB_SY DC_CVAU IC_IVAU)
        if(_instruction STREQUAL "DMB")
            set(_asm "dmb ish")
        elseif(_instruction STREQUAL "DMB_LD")
            set(_asm "dmb ishld")
        elseif(_instruction STREQUAL "DMB_ST")
            set(_asm "dmb ishst")
        elseif(_instruction STREQUAL "ISB_SY")
            set(_asm "isb sy")
        elseif(_instruction STREQUAL "DC_CVAU")
            set(_asm "dc cvau, x0")
        else()
            set(_asm "ic ivau, x0")
        endif()
        check_c_source_compiles("
            int main(void) { __asm__ __volatile__(\"${_asm}\" : : : \"memory\"); return 0; }
        " ETHR_HAVE_GCC_ASM_ARM_${_instruction}_INSTRUCTION)
    endforeach()
endif()

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

# Translate erts/configure.ac:2547-2564 at UPSTREAM_COMMIT. The runtime's
# affinity mask/update_cpu_info branch needs the complete CPU-set API, not
# merely sched.h. Like upstream AC_LINK_IFELSE, do not execute this probe.
cmake_push_check_state(RESET)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Match the emulator's feature-test definition before including sched.h.
    set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
endif()
check_c_source_compiles("
    #include <sched.h>
    #ifndef CPU_SETSIZE
    #error no CPU_SETSIZE
    #endif
    int main(void) {
        int res;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(1, &cpuset);
        res = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
        res = sched_getaffinity(0, sizeof(cpu_set_t), &cpuset);
        res = CPU_ISSET(1, &cpuset);
        CPU_CLR(1, &cpuset);
        return res;
    }
" HAVE_SCHED_xETAFFINITY)
cmake_pop_check_state()

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

# Descriptor metadata is used by ordinary file APIs and the source compiler's
# documentation reader. Probe it for every Unix target, not only Darwin.
check_symbol_exists(fstat "sys/types.h;sys/stat.h" HAVE_FSTAT)

# Match the pinned ERTS configure checks. utils.c requires both the header's
# allocator constants and mallopt to implement +MYtt/+MYtp on glibc.
check_include_file("malloc.h" HAVE_MALLOC_H)
check_function_exists(mallopt HAVE_MALLOPT)

# Upstream initializes the filename encoding from the process locale. Without
# these probes a UTF-8 Unix locale silently keeps the Latin-1 fallback.
check_include_file("langinfo.h" HAVE_LANGINFO_H)
check_function_exists(setlocale HAVE_SETLOCALE)
check_function_exists(nl_langinfo HAVE_NL_LANGINFO)

check_symbol_exists(pthread_spin_lock "pthread.h" ETHR_HAVE_PTHREAD_SPIN_LOCK)
if(ETHR_HAVE_PTHREAD_SPIN_LOCK)
    set(ETHR_HAVE_PTHREAD_SPIN_LOCK 1)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(${CMAKE_CURRENT_LIST_DIR}/ConfigureDarwin.cmake)
else()
# Linux glibc seed for a vanilla build.
set(ETHR_HAVE_PTHREAD_RWLOCKATTR_SETKIND_NP 1)
set(ETHR_HAVE_PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP 1)
check_function_exists(pthread_yield ETHR_HAVE_PTHREAD_YIELD)
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
set(ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME 1)
set(ERTS_EMU_CMDLINE_FLAGS " -D_GNU_SOURCE")

# Fill the Linux seed's consumer-facing gaps using the same native probe
# patterns as Darwin. Keep feature-test definitions consistent with the VM.
include(CheckCSourceRuns)
include(CheckStructHasMember)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS} m)
set(CMAKE_REQUIRED_FLAGS "-Werror=implicit-function-declaration")

set(HAVE_ZLIB_INFLATEGETDICTIONARY 1) # The selected bundled zlib owns this API.
foreach(_header IN ITEMS arpa/nameser.h sys/resource.h sys/stat.h
        limits.h inttypes.h elf.h sys/timerfd.h linux/types.h)
    string(TOUPPER "${_header}" _header_var)
    string(REGEX REPLACE "[/\\.]" "_" _header_var "${_header_var}")
    check_include_file("${_header}" HAVE_${_header_var})
endforeach()
# linux/errqueue.h needs the timestamp type supplied by sys/time.h.
check_c_source_compiles("
    #include <sys/time.h>
    #include <linux/types.h>
    #include <linux/errqueue.h>
    int main(void) { struct sock_extended_err error = {0}; return error.ee_errno; }
" HAVE_LINUX_ERRQUEUE_H)
foreach(_symbol_headers IN ITEMS
        "madvise|sys/mman.h|HAVE_MADVISE" "mlockall|sys/mman.h|HAVE_MLOCKALL"
        "dlvsym|dlfcn.h|HAVE_DLVSYM" "closefrom|unistd.h|HAVE_CLOSEFROM"
        "memrchr|string.h|HAVE_MEMRCHR" "strerrorname_np|string.h|HAVE_STRERRORNAME_NP"
        "posix_fadvise|fcntl.h|HAVE_POSIX_FADVISE" "ppoll|poll.h|HAVE_PPOLL"
        "setns|sched.h|HAVE_SETNS" "fdatasync|unistd.h|HAVE_FDATASYNC"
        "pread|unistd.h|HAVE_PREAD" "pwrite|unistd.h|HAVE_PWRITE"
        "getrusage|sys/resource.h|HAVE_GETRUSAGE"
        "localtime_r|time.h|HAVE_LOCALTIME_R" "gmtime_r|time.h|HAVE_GMTIME_R"
        "strftime|time.h|HAVE_STRFTIME" "log2|math.h|HAVE_LOG2"
        "finite|math.h|HAVE_FINITE"
        "strncasecmp|strings.h|HAVE_STRNCASECMP"
        "flockfile|stdio.h|HAVE_FLOCKFILE" "vsyslog|syslog.h|HAVE_VSYSLOG"
        "getprotoent|netdb.h|HAVE_GETPROTOENT" "setprotoent|netdb.h|HAVE_SETPROTOENT"
        "endprotoent|netdb.h|HAVE_ENDPROTOENT"
        "pthread_attr_setguardsize|pthread.h|ETHR_HAVE_PTHREAD_ATTR_SETGUARDSIZE")
    string(REPLACE "|" ";" _symbol_parts "${_symbol_headers}")
    list(POP_FRONT _symbol_parts _symbol)
    list(POP_BACK _symbol_parts _result)
    check_symbol_exists(${_symbol} "${_symbol_parts}" ${_result})
endforeach()
foreach(_operation IN ITEMS putc_unlocked fwrite_unlocked)
    string(TOUPPER "${_operation}" _operation_upper)
    if(_operation STREQUAL "putc_unlocked")
        set(_call "putc_unlocked('x', stdout)")
    else()
        set(_call "fwrite_unlocked(0, 1, 0, stdout)")
    endif()
    check_c_source_compiles("
        #include <stdio.h>
        int main(void) { return (int) ${_call}; }
    " HAVE_${_operation_upper})
endforeach()
check_c_source_compiles("
    #include <math.h>
    int main(void) { volatile double value = 0; return isfinite(value) ? 0 : 1; }
" HAVE_ISFINITE)
check_c_source_compiles("
    #include <stdio.h>
    int main(void) { extern int fread(); return 0; }
" ERTS_LINUX_FREAD_DECLARATION_IS_COMPATIBLE)
if(NOT ERTS_LINUX_FREAD_DECLARATION_IS_COMPATIBLE)
    set(HAVE_CONFLICTING_FREAD_DECLARATION 1)
endif()
set(CMAKE_EXTRA_INCLUDE_FILES sys/time.h)
check_type_size("suseconds_t" SIZEOF_SUSECONDS_T LANGUAGE C)
unset(CMAKE_EXTRA_INCLUDE_FILES)
check_c_source_compiles("
    #include <netinet/in.h>
    int main(void) { struct ip_mreqn m = {0};
        return m.imr_ifindex + m.imr_multiaddr.s_addr + m.imr_address.s_addr; }
" HAVE_STRUCT_IP_MREQN)
foreach(_member IN ITEMS ifr_hwaddr ifr_ifindex ifr_map)
    string(TOUPPER "${_member}" _member_upper)
    check_struct_has_member("struct ifreq" ${_member} "net/if.h"
        HAVE_STRUCT_IFREQ_${_member_upper})
endforeach()
check_symbol_exists(SO_BSDCOMPAT "sys/socket.h" HAVE_SO_BSDCOMPAT)
check_c_source_compiles("
    #include <pthread.h>
    int main(void) { char name[64];
        return pthread_getname_np(pthread_self(), name, sizeof(name)); }
" ETHR_HAVE_PTHREAD_GETNAME_NP_3)
check_c_source_compiles("
    #include <pthread.h>
    int main(void) { return pthread_setname_np(pthread_self(), \"beam\"); }
" ETHR_HAVE_PTHREAD_SETNAME_NP_2)
check_c_source_compiles("
    #include <limits.h>
    #include <pthread.h>
    int main(void) { return PTHREAD_STACK_MIN > 0 ? 0 : 1; }
" ETHR_HAVE_USABLE_PTHREAD_STACK_MIN)
set(ETHR_HAVE_SYS_TIME_H ${HAVE_SYS_TIME_H})
# Match configure.ac's Linux perf policy and x86 native-stack selection. These
# are platform policies, independent of the subsequent emu/jit build flavor.
set(HAVE_LINUX_PERF_SUPPORT 1)
if(ERTS_TARGET_ARCH STREQUAL "x86")
    set(NATIVE_ERLANG_STACK 1)
    set(ERLANG_FRAME_POINTERS 1)
    # Upstream enables its THP interface specifically for amd64 Linux.
    check_c_source_compiles("
        #include <sys/mman.h>
        int main(void) { return madvise(0, 0, MADV_HUGEPAGE); }
    " HAVE_LINUX_THP)
endif()
check_c_source_compiles("
    #include <time.h>
    int main(void) { struct timespec ts;
        return clock_gettime(CLOCK_MONOTONIC, &ts); }
" ERTS_LINUX_HAVE_HRTIME_CLOCK)
if(ERTS_LINUX_HAVE_HRTIME_CLOCK)
    set(SYS_HRTIME_USING_CLOCK_GETTIME 1)
    set(HRTIME_CLOCK_ID CLOCK_MONOTONIC)
    set(HRTIME_CLOCK_ID_STR CLOCK_MONOTONIC)
    set(HAVE_MONOTONIC_ERTS_SYS_HRTIME 1)
endif()
check_c_source_runs("
    #include <time.h>
    int main(void) { struct timespec ts; long long start, stop; int i;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) return 1;
        start = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        for (i = 0; i < 100; i++)
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) return 1;
        stop = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        return start <= 0 || stop <= start; }
" ERTS_LINUX_WORKING_THREAD_CPU_TIME)
set(HAVE_CLOCK_GETTIME_CPU_TIME ${ERTS_LINUX_WORKING_THREAD_CPU_TIME})
check_c_source_compiles("
    int main(void) { _Float16 half = 1; return (int) (float) half; }
" FLOAT16_IS_CONVERTIBLE)
cmake_push_check_state()
set(CMAKE_REQUIRED_FLAGS "-Werror")
check_c_source_compiles([=[
    int main(void) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#pragma GCC diagnostic pop
        return 0;
    }
]=] HAVE_GCC_DIAG_IGNORE_WADDRESS_OF_PACKED_MEMBER)
check_c_source_compiles([=[
    int main(void) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic pop
        return 0;
    }
]=] HAVE_GCC_DIAG_IGNORE_WFORMAT_NONLITERAL)
cmake_pop_check_state()
cmake_pop_check_state()

check_include_file("sys/epoll.h" _erts_have_sys_epoll_h)
if(NOT _erts_have_sys_epoll_h)
    message(FATAL_ERROR "sys/epoll.h is required")
endif()
set(HAVE_SYS_EPOLL_H 1)
set(ERTS_ENABLE_KERNEL_POLL 1)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/WorkingPoll.cmake")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Termcap: not used on Windows")
else()
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

# Remove the obsolete copy so the earlier erts include directory cannot shadow
# the canonical include/ header when reconfiguring an existing build tree.
file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/erts/erl_int_sizes_config.h")

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
