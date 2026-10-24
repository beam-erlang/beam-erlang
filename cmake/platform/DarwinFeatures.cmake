# Native atomics must link without a fallback library for every advertised width.
foreach(_operation store_n load_n add_fetch fetch_and fetch_or compare_exchange_n)
    set(ETHR_HAVE___atomic_${_operation} 0)
    foreach(_width 4 8 16)
        if(_width EQUAL 4)
            set(_type int32_t)
        elseif(_width EQUAL 8)
            set(_type int64_t)
        else()
            set(_type __int128_t)
        endif()
        if(_operation STREQUAL store_n)
            set(_expression "__atomic_store_n(&value, 2, __ATOMIC_RELAXED); return value != 2;")
        elseif(_operation STREQUAL load_n)
            set(_expression "return __atomic_load_n(&value, __ATOMIC_RELAXED) != 1;")
        elseif(_operation STREQUAL compare_exchange_n)
            set(_expression "${_type} expected = 1; return !__atomic_compare_exchange_n(&value, &expected, 2, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);")
        else()
            set(_expression "return __atomic_${_operation}(&value, 2, __ATOMIC_RELAXED) == 0;")
        endif()
        check_c_source_compiles("#include <stdint.h>
            _Static_assert(__atomic_always_lock_free(${_width}, 0), \"native atomics required\");
            static volatile ${_type} value = 1;
            int main(void) { ${_expression} }" BEAM_DARWIN_ATOMIC_${_operation}_${_width})
        if(BEAM_DARWIN_ATOMIC_${_operation}_${_width})
            math(EXPR ETHR_HAVE___atomic_${_operation}
                "${ETHR_HAVE___atomic_${_operation}} | ${_width}")
        endif()
    endforeach()
endforeach()
math(EXPR _native_widths "${ETHR_HAVE___atomic_compare_exchange_n} & 12")
if(NOT _native_widths EQUAL 12)
    message(FATAL_ERROR "Darwin requires native 32-bit and 64-bit atomic operations")
endif()
set(ETHR_HAVE_GCC___ATOMIC_BUILTINS 1)
check_c_source_compiles("int main(void) { __sync_synchronize(); return 0; }"
    BEAM_DARWIN_SYNC_BARRIER)
if(BEAM_DARWIN_SYNC_BARRIER)
    set(ETHR_HAVE___sync_synchronize ~0)
endif()

# AF_LINK and IPv6 declarations are supplied by Darwin's native socket headers.
check_include_file("net/if_dl.h" HAVE_NET_IF_DL_H)
foreach(_address any loopback)
    string(TOUPPER "${_address}" _name)
    check_symbol_exists(in6addr_${_address} "sys/types.h;sys/socket.h;netinet/in.h"
        HAVE_IN6ADDR_${_name})
    check_symbol_exists(IN6ADDR_${_name}_INIT "sys/types.h;sys/socket.h;netinet/in.h"
        BEAM_DARWIN_IN6ADDR_${_name}_INIT)
    if(BEAM_DARWIN_IN6ADDR_${_name}_INIT)
        set(HAVE_DECL_IN6ADDR_${_name}_INIT 1)
    endif()
endforeach()
check_symbol_exists(IPV6_V6ONLY "sys/types.h;sys/socket.h;netinet/in.h"
    BEAM_DARWIN_IPV6_V6ONLY)
if(BEAM_DARWIN_IPV6_V6ONLY)
    set(HAVE_DECL_IPV6_V6ONLY 1)
endif()
check_c_source_compiles("#include <sys/un.h>
    int main(void) { struct sockaddr_un address; return sizeof address.sun_path == 0; }"
    HAVE_STRUCT_SOCKADDR_UN_SUN_PATH)

# Darwin's poll can reject /dev/null although select accepts it. Kernel kqueue
# still handles ordinary descriptors; its non-kqueue fallback must honor this
# behavior, as the pinned upstream configure probe does.
if(CMAKE_CROSSCOMPILING)
    set(ERTS_USE_POLL 0)
else()
    include(CheckCSourceRuns)
    check_c_source_runs([=[#include <fcntl.h>
        #include <poll.h>
        #include <unistd.h>
        int main(void) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0) return 1;
            struct pollfd input = {fd, POLLIN, 0};
            int result = poll(&input, 1, 0);
            close(fd);
            return result < 0 || (input.revents & POLLNVAL) != 0;
        }]=] BEAM_DARWIN_WORKING_POLL)
    set(ERTS_USE_POLL "${BEAM_DARWIN_WORKING_POLL}")
endif()

# Darwin names the current pthread with one argument, and inspects a selected
# pthread with three. Do not misclassify absent configure definitions as an OS
# capability exclusion.
check_c_source_compiles([=[#include <pthread.h>
    int main(void) { return pthread_setname_np("beam-probe"); }
]=] ETHR_HAVE_PTHREAD_SETNAME_NP_1)
check_c_source_compiles([=[#include <pthread.h>
    int main(void) {
        char name[64];
        return pthread_getname_np(pthread_self(), name, sizeof name);
    }
]=] ETHR_HAVE_PTHREAD_GETNAME_NP_3)
