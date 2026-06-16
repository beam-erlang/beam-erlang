# Bionic API 26 and the selected Android Clang target.
set(ETHR_HAVE_SCHED_YIELD 1)
set(ETHR_SCHED_YIELD_RET_INT 1)
set(ETHR_HAVE_CLOCK_GETTIME_MONOTONIC 1)
set(ETHR_HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC 1)
set(ETHR_MONOTONIC_CLOCK_ID CLOCK_MONOTONIC)
set(ETHR_HAVE_GCC___ATOMIC_BUILTINS 1)
set(ETHR_HAVE___sync_synchronize ~0)
foreach(_atomic IN ITEMS __sync_add_and_fetch __sync_fetch_and_and __sync_fetch_and_or
        __sync_val_compare_and_swap __atomic_store_n __atomic_load_n __atomic_add_fetch
        __atomic_fetch_and __atomic_fetch_or __atomic_compare_exchange_n)
    set(ETHR_HAVE_${_atomic} 12)
endforeach()
if(ERTS_TARGET_ARCH STREQUAL "x86")
    set(ETHR_GCC_HAVE_DW_CMPXCHG_ASM_SUPPORT 1)
endif()
set(NO_SA_LEN 1)
set(HAVE_STRUCT_SOCKADDR_UN_SUN_PATH 1)
set(ERTS_HAVE_CORRECTED_OS_MONOTONIC_TIME 1)
if(ERTS_TARGET_ARCH STREQUAL "arm")
    check_c_source_compiles("int main(void) { __asm__ __volatile__(\"isb sy\" ::: \"memory\"); return 0; }"
        ETHR_HAVE_GCC_ASM_ARM_ISB_SY_INSTRUCTION)
    foreach(_instruction IN ITEMS "ic ivau" "dc cvau")
        string(TOUPPER "${_instruction}" _macro)
        string(REPLACE " " "_" _macro "${_macro}")
        check_c_source_compiles("int main(void) { char byte; __asm__ __volatile__(\"${_instruction}, %0\" :: \"r\"(&byte) : \"memory\"); return 0; }"
            ETHR_HAVE_GCC_ASM_ARM_${_macro}_INSTRUCTION)
    endforeach()
endif()

# Probe Bionic's public networking declarations rather than inheriting glibc facts.
foreach(_address any loopback)
    string(TOUPPER "${_address}" _name)
    check_symbol_exists(in6addr_${_address} "sys/types.h;sys/socket.h;netinet/in.h"
        HAVE_IN6ADDR_${_name})
    check_symbol_exists(IN6ADDR_${_name}_INIT "sys/types.h;sys/socket.h;netinet/in.h"
        BEAM_ANDROID_IN6ADDR_${_name}_INIT)
    if(BEAM_ANDROID_IN6ADDR_${_name}_INIT)
        set(HAVE_DECL_IN6ADDR_${_name}_INIT 1)
    endif()
endforeach()
check_symbol_exists(IPV6_V6ONLY "sys/types.h;sys/socket.h;netinet/in.h"
    BEAM_ANDROID_IPV6_V6ONLY)
if(BEAM_ANDROID_IPV6_V6ONLY)
    set(HAVE_DECL_IPV6_V6ONLY 1)
endif()
check_c_source_compiles("#include <net/if.h>
    int main(void) { struct ifreq request; request.ifr_ifindex = 0; return request.ifr_ifindex; }"
    ESOCK_USE_IFINDEX)
