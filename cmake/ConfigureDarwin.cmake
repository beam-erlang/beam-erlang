# Native macOS capabilities. Keep Linux-specific defaults out of this path.
# Compare with the OTP-29.0 macos-arm64 capture at upstream 550d7b7898706c78.

include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckStructHasMember)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
set(CMAKE_REQUIRED_DEFINITIONS -D_DARWIN_C_SOURCE)
set(CMAKE_REQUIRED_FLAGS "-Werror=implicit-function-declaration")

# Upstream reserves structure-only allocator alignment for x86/amd64.
set(ERTS_STRUCTURE_ALIGNED_ALLOC 0)
# The selected builtin zlib provides this API without a system-library probe.
set(HAVE_ZLIB_INFLATEGETDICTIONARY 1)
set(HAVE_DECL_TIME2POSIX 0)
set(HAVE_DECL_POSIX2TIME 0)

foreach(_header IN ITEMS
        sys/time.h sys/types.h sys/socket.h sys/mman.h sys/uio.h sys/un.h
        sys/ioctl.h sys/event.h sys/resource.h sys/sysctl.h sys/sockio.h
        sys/stat.h fcntl.h dlfcn.h unistd.h stdlib.h syslog.h stdio.h
        string.h errno.h dirent.h net/if.h net/if_dl.h arpa/inet.h arpa/nameser.h
        netinet/in.h netinet/tcp.h netdb.h ifaddrs.h limits.h inttypes.h
        stdint.h langinfo.h)
    string(TOUPPER "${_header}" _header_var)
    string(REGEX REPLACE "[/\\.]" "_" _header_var "${_header_var}")
    check_include_file("${_header}" HAVE_${_header_var})
endforeach()

check_c_source_compiles("
    #include <sys/types.h>
    #include <sys/event.h>
    #include <unistd.h>
    int main(void) {
        struct kevent event;
        EV_SET(&event, 0, EVFILT_READ, EV_ADD, 0, 0, 0);
        int fd = kqueue();
        int result = kevent(fd, &event, 1, 0, 0, 0);
        close(fd);
        return result;
    }
" ERTS_HAVE_KQUEUE)
if(NOT ERTS_HAVE_KQUEUE)
    message(FATAL_ERROR "macOS requires kqueue and kevent")
endif()
set(ERTS_ENABLE_KERNEL_POLL 1)
# Preserve upstream's ancillary SSL-consumer output for the Darwin poll policy.
# The VM's primary backend remains kqueue, independently of this definition.
set(USE_SELECT 1)

check_c_source_compiles("
    #include <time.h>
    int main(void) {
        struct timespec ts;
        return clock_gettime(CLOCK_UPTIME_RAW, &ts)
             + clock_getres(CLOCK_UPTIME_RAW, &ts)
             + clock_gettime(CLOCK_REALTIME, &ts);
    }
" ERTS_HAVE_DARWIN_CLOCKS)
if(NOT ERTS_HAVE_DARWIN_CLOCKS)
    message(FATAL_ERROR "macOS requires CLOCK_UPTIME_RAW and CLOCK_REALTIME")
endif()
set(ETHR_HAVE_CLOCK_GETTIME_MONOTONIC 1)
set(ETHR_MONOTONIC_CLOCK_ID CLOCK_UPTIME_RAW)
set(MONOTONIC_CLOCK_ID CLOCK_UPTIME_RAW)
set(MONOTONIC_CLOCK_ID_STR CLOCK_UPTIME_RAW)
set(WALL_CLOCK_ID CLOCK_REALTIME)
set(WALL_CLOCK_ID_STR CLOCK_REALTIME)
set(SYS_HRTIME_USING_CLOCK_GETTIME 1)
set(HRTIME_CLOCK_ID CLOCK_UPTIME_RAW)
set(HRTIME_CLOCK_ID_STR CLOCK_UPTIME_RAW)
set(HAVE_MONOTONIC_ERTS_SYS_HRTIME 1)
# The raw uptime clock is not adjusted by the OS for clock drift.
set(ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME 0)
set(ERTS_EMU_CMDLINE_FLAGS " -D_DARWIN_C_SOURCE=")

check_symbol_exists(sched_yield "sched.h" ETHR_HAVE_SCHED_YIELD)
set(ETHR_SCHED_YIELD_RET_INT 0)
if(ETHR_HAVE_SCHED_YIELD)
    check_c_source_compiles("
        #include <sched.h>
        extern int sched_yield(void);
        int main(void) { return sched_yield(); }
    " ERTS_DARWIN_SCHED_YIELD_RETURNS_INT)
    set(ETHR_SCHED_YIELD_RET_INT "${ERTS_DARWIN_SCHED_YIELD_RETURNS_INT}")
endif()
check_symbol_exists(pthread_attr_setguardsize "pthread.h"
    ETHR_HAVE_PTHREAD_ATTR_SETGUARDSIZE)
check_c_source_compiles("
    #include <pthread.h>
    int main(void) { return pthread_setname_np(\"beam\"); }
" ETHR_HAVE_PTHREAD_SETNAME_NP_1)
check_c_source_compiles("
    #include <pthread.h>
    int main(void) {
        char name[64];
        return pthread_getname_np(pthread_self(), name, sizeof(name));
    }
" ETHR_HAVE_PTHREAD_GETNAME_NP_3)
check_c_source_compiles("
    #include <limits.h>
    #include <pthread.h>
    int main(void) { return PTHREAD_STACK_MIN > 0 ? 0 : 1; }
" ETHR_HAVE_USABLE_PTHREAD_STACK_MIN)
set(ETHR_HAVE_SYS_TIME_H ${HAVE_SYS_TIME_H})
# macOS has no pthread_condattr_setclock. The existing Darwin ethread
# implementation uses its condition/pipe/select path for timed events.

# The masks use the byte width as a bit (4 | 8 | 16), as ethread expects.
# Check all operations and require lock-free native 32/64-bit atomics.
set(_erts_atomic_widths 0)
foreach(_width IN ITEMS 4 8 16)
    if(_width EQUAL 4)
        set(_type "unsigned int")
    elseif(_width EQUAL 8)
        set(_type "unsigned long long")
    else()
        set(_type "unsigned __int128")
    endif()
    check_c_source_compiles("
        typedef ${_type} value_t;
        _Static_assert(sizeof(value_t) == ${_width}, \"atomic width\");
        _Static_assert(__atomic_always_lock_free(${_width}, 0), \"lock free\");
        int main(void) {
            value_t value = 0, expected = 0;
            __atomic_store_n(&value, 1, __ATOMIC_SEQ_CST);
            value_t result = __atomic_load_n(&value, __ATOMIC_SEQ_CST);
            result += __atomic_add_fetch(&value, 1, __ATOMIC_SEQ_CST);
            result += __atomic_fetch_and(&value, 1, __ATOMIC_SEQ_CST);
            result += __atomic_fetch_or(&value, 1, __ATOMIC_SEQ_CST);
            result += __atomic_compare_exchange_n(&value, &expected, 1, 0,
                                                  __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
            result += __sync_add_and_fetch(&value, 1);
            result += __sync_fetch_and_and(&value, 1);
            result += __sync_fetch_and_or(&value, 1);
            result += __sync_val_compare_and_swap(&value, 0, 1);
            __sync_synchronize();
            return (int) result;
        }
    " ERTS_HAVE_DARWIN_ATOMICS_${_width})
    if(ERTS_HAVE_DARWIN_ATOMICS_${_width})
        math(EXPR _erts_atomic_widths "${_erts_atomic_widths} | ${_width}")
    endif()
endforeach()
math(EXPR _erts_required_atomics "${_erts_atomic_widths} & 12")
if(NOT _erts_required_atomics EQUAL 12)
    message(FATAL_ERROR "macOS requires native 32-bit and 64-bit atomics")
endif()
set(ETHR_HAVE_GCC___ATOMIC_BUILTINS 1)
set(ETHR_HAVE___sync_synchronize ~0)
foreach(_operation IN ITEMS
        __sync_add_and_fetch __sync_fetch_and_and __sync_fetch_and_or
        __sync_val_compare_and_swap __atomic_store_n __atomic_load_n
        __atomic_add_fetch __atomic_fetch_and __atomic_fetch_or
        __atomic_compare_exchange_n)
    set(ETHR_HAVE_${_operation} ${_erts_atomic_widths})
endforeach()
foreach(_symbol_headers IN ITEMS
        "dlopen|dlfcn.h|HAVE_DLOPEN" "mmap|sys/mman.h|HAVE_MMAP"
        "madvise|sys/mman.h|HAVE_MADVISE"
        "mlockall|sys/mman.h|HAVE_MLOCKALL"
        "log2|math.h|HAVE_LOG2"
        "posix_memalign|stdlib.h|HAVE_POSIX_MEMALIGN"
        "poll|poll.h|HAVE_POLL" "writev|sys/uio.h|HAVE_WRITEV"
        "sendfile|sys/types.h;sys/socket.h;sys/uio.h|HAVE_SENDFILE"
        "getaddrinfo|sys/types.h;sys/socket.h;netdb.h|HAVE_GETADDRINFO"
        "getnameinfo|sys/types.h;sys/socket.h;netdb.h|HAVE_GETNAMEINFO"
        "getprotoent|netdb.h|HAVE_GETPROTOENT"
        "setprotoent|netdb.h|HAVE_SETPROTOENT"
        "endprotoent|netdb.h|HAVE_ENDPROTOENT"
        "getifaddrs|ifaddrs.h|HAVE_GETIFADDRS"
        "if_nametoindex|net/if.h|HAVE_IF_NAMETOINDEX"
        "if_nameindex|net/if.h|HAVE_IF_NAMEINDEX"
        "if_indextoname|net/if.h|HAVE_IF_INDEXTONAME"
        "if_freenameindex|net/if.h|HAVE_IF_FREENAMEINDEX"
        "inet_pton|arpa/inet.h|HAVE_INET_PTON"
        "in6addr_any|netinet/in.h|HAVE_IN6ADDR_ANY"
        "in6addr_loopback|netinet/in.h|HAVE_IN6ADDR_LOOPBACK"
        "vsyslog|syslog.h|HAVE_VSYSLOG" "strerror|string.h|HAVE_STRERROR"
        "strncasecmp|strings.h|HAVE_STRNCASECMP"
        "flockfile|stdio.h|HAVE_FLOCKFILE"
        "gettimeofday|sys/time.h|HAVE_GETTIMEOFDAY"
        "fstat|sys/stat.h|HAVE_FSTAT"
        "pread|unistd.h|HAVE_PREAD" "pwrite|unistd.h|HAVE_PWRITE"
        "getrusage|sys/resource.h|HAVE_GETRUSAGE"
        "localtime_r|time.h|HAVE_LOCALTIME_R" "gmtime_r|time.h|HAVE_GMTIME_R"
        "setlocale|locale.h|HAVE_SETLOCALE"
        "nl_langinfo|langinfo.h|HAVE_NL_LANGINFO"
        "strftime|time.h|HAVE_STRFTIME"
        "time2posix|time.h|HAVE_DECL_TIME2POSIX"
        "posix2time|time.h|HAVE_DECL_POSIX2TIME"
        "daylight|time.h|HAVE_DECL_DAYLIGHT"
        "IN6ADDR_ANY_INIT|netinet/in.h|HAVE_DECL_IN6ADDR_ANY_INIT"
        "IN6ADDR_LOOPBACK_INIT|netinet/in.h|HAVE_DECL_IN6ADDR_LOOPBACK_INIT"
        "IPV6_V6ONLY|netinet/in.h|HAVE_DECL_IPV6_V6ONLY")
    string(REPLACE "|" ";" _symbol_parts "${_symbol_headers}")
    list(POP_FRONT _symbol_parts _symbol)
    list(POP_BACK _symbol_parts _result)
    # Declaration outputs use numeric defaults and separate cached probe names.
    if(_result MATCHES "^HAVE_DECL_")
        check_symbol_exists(${_symbol} "${_symbol_parts}" _erts_${_result})
        if(_erts_${_result})
            set(${_result} 1)
        endif()
    else()
        check_symbol_exists(${_symbol} "${_symbol_parts}" ${_result})
    endif()
endforeach()
check_function_exists(fdatasync ERTS_DARWIN_LINKABLE_FDATASYNC)
set(HAVE_FDATASYNC "${ERTS_DARWIN_LINKABLE_FDATASYNC}")
check_function_exists(time2posix HAVE_TIME2POSIX)
check_function_exists(posix2time HAVE_POSIX2TIME)
check_c_source_compiles("
    #include <stdio.h>
    int main(void) {
        int result = putc_unlocked('x', stdout);
        return result == EOF;
    }
" HAVE_PUTC_UNLOCKED)
check_library_exists(dl dlopen "" HAVE_LIBDL)
check_include_file("poll.h" HAVE_POLL_H)
set(CMAKE_EXTRA_INCLUDE_FILES sys/time.h)
check_type_size("suseconds_t" SIZEOF_SUSECONDS_T LANGUAGE C)
if(NOT SIZEOF_SUSECONDS_T)
    set(SIZEOF_SUSECONDS_T 0)
endif()
set(CMAKE_EXTRA_INCLUDE_FILES sys/socket.h)
check_type_size("socklen_t" _erts_darwin_socklen_t LANGUAGE C)
unset(CMAKE_EXTRA_INCLUDE_FILES)
set(HAVE_SOCKLEN_T ${_erts_darwin_socklen_t})
check_struct_has_member("struct sockaddr_un" sun_path "sys/un.h"
    HAVE_STRUCT_SOCKADDR_UN_SUN_PATH)
check_struct_has_member("struct sockaddr_dl" sdl_len "sys/types.h;net/if_dl.h"
    ESOCK_SDL_LEN)
check_struct_has_member("struct sockaddr_in6" sin6_addr "sys/types.h;netinet/in.h"
    HAVE_IN6)
check_c_source_compiles("
    #include <netinet/in.h>
    int main(void) {
        struct ip_mreqn membership = {0};
        membership.imr_ifindex = 0;
        membership.imr_multiaddr.s_addr = 0;
        membership.imr_address.s_addr = 0;
        return membership.imr_ifindex
             + membership.imr_multiaddr.s_addr
             + membership.imr_address.s_addr;
    }
" HAVE_STRUCT_IP_MREQN)
set(HAVE_IPV6 ${HAVE_IN6})
check_c_source_compiles("
    #include <time.h>
    int main(void) {
        struct timespec ts;
        return clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    }
" HAVE_CLOCK_GETTIME_MONOTONIC_RAW)

# The pinned upstream CPU-clock run test unnecessarily links librt on Darwin.
# Native arm64 is required, so test the advancing clock directly without librt.
# Keep a new cache key: the old output cached only compilation/linkage.
check_c_source_runs("
    #include <time.h>
    int main(void) {
        struct timespec ts;
        long long start, stop;
        int i;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
            return 1;
        start = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        for (i = 0; i < 100; i++) {
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
                return 1;
        }
        stop = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
        return start <= 0 || stop <= start;
    }
" ERTS_DARWIN_WORKING_THREAD_CPU_TIME)
set(HAVE_CLOCK_GETTIME_CPU_TIME "${ERTS_DARWIN_WORKING_THREAD_CPU_TIME}")

check_c_source_compiles("
    #include <math.h>
    int main(void) {
        volatile double value = 0.0;
        return isfinite(value) ? 0 : 1;
    }
" HAVE_ISFINITE)
check_c_source_compiles("
    #include <stdio.h>
    int main(void) {
        extern int fread();
        return 0;
    }
" ERTS_DARWIN_FREAD_DECLARATION_IS_COMPATIBLE)
if(ERTS_DARWIN_FREAD_DECLARATION_IS_COMPATIBLE)
    set(HAVE_CONFLICTING_FREAD_DECLARATION 0)
else()
    set(HAVE_CONFLICTING_FREAD_DECLARATION 1)
endif()

# Match upstream's narrow diagnostic-pragmas tests with warnings as errors.
cmake_push_check_state()
set(CMAKE_REQUIRED_FLAGS "-Werror -Werror=implicit-function-declaration")
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

check_c_source_compiles("
    int main(void) { _Float16 half = 1; return (int) (float) half; }
" FLOAT16_IS_CONVERTIBLE)

cmake_pop_check_state()
message(STATUS "macOS kernel poll: kqueue; monotonic clock: CLOCK_UPTIME_RAW")
