if(MINGW AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    check_c_source_compiles([=[
        #include <_mingw.h>
        #ifndef _UCRT
        #error The native Windows GCC build requires UCRT
        #endif
        int main(void) { return 0; }
    ]=] BEAM_HAVE_MINGW_UCRT)
    if(NOT BEAM_HAVE_MINGW_UCRT)
        message(FATAL_ERROR "Select a native UCRT64 GCC compiler, outside MSYS /usr")
    endif()
endif()
# Native Microsoft atomics must be verified before selecting the Win32 backend.
if(MSVC)
    check_c_source_compiles([=[
        #include <intrin.h>
        int main(void) {
            volatile long word = 0;
            volatile __int64 wide = 0;
            _InterlockedCompareExchange(&word, 1, 0);
            _InterlockedExchange(&word, 1);
            _InterlockedExchangeAdd(&word, 1);
            _InterlockedIncrement(&word);
            _InterlockedDecrement(&word);
            _InterlockedAnd(&word, 1);
            _InterlockedOr(&word, 1);
            _InterlockedCompareExchange64(&wide, 1, 0);
            _InterlockedExchange64(&wide, 1);
            _InterlockedExchangeAdd64(&wide, 1);
            _InterlockedIncrement64(&wide);
            _InterlockedDecrement64(&wide);
            _InterlockedAnd64(&wide, 1);
            _InterlockedOr64(&wide, 1);
            _ReadWriteBarrier();
            return 0;
        }
    ]=] BEAM_HAVE_MSVC_INTERLOCKED)
    if(NOT BEAM_HAVE_MSVC_INTERLOCKED)
        message(FATAL_ERROR "The selected Microsoft compiler lacks required native atomics")
    endif()
    foreach(_operation IN ITEMS AND AND64 COMPAREEXCHANGE COMPAREEXCHANGE64
            DECREMENT DECREMENT64 EXCHANGE EXCHANGE64 EXCHANGEADD EXCHANGEADD64
            INCREMENT INCREMENT64 OR OR64)
        set(ETHR_HAVE__INTERLOCKED${_operation} 1)
    endforeach()
    check_c_source_compiles([=[
        #include <intrin.h>
        int main(void) {
            __declspec(align(16)) volatile __int64 value[2] = {0, 0};
            __int64 expected[2] = {0, 0};
            return _InterlockedCompareExchange128(value, 0, 1, expected);
        }
    ]=] BEAM_HAVE_MSVC_INTERLOCKED128)
    set(ETHR_HAVE__INTERLOCKEDCOMPAREEXCHANGE128 ${BEAM_HAVE_MSVC_INTERLOCKED128})
endif()
# Winsock sockaddr structures use explicit lengths rather than BSD sa_len.
set(NO_SA_LEN 1)
include(CheckSymbolExists)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES ws2_32)
check_symbol_exists(in6addr_any "winsock2.h;ws2tcpip.h" HAVE_IN6ADDR_ANY)
check_symbol_exists(in6addr_loopback "winsock2.h;ws2tcpip.h" HAVE_IN6ADDR_LOOPBACK)
cmake_pop_check_state()
check_c_source_compiles([=[
    #include <winsock2.h>
    #include <ws2tcpip.h>
    int main(void) { struct sockaddr_in6 address = {0}; return sizeof(address.sin6_addr) != 16; }
]=] HAVE_IN6)
