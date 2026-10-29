# config.h, erl_int_sizes_config.h, and ethread_header_config.h.
#
# Target compile/link probes. The native Darwin poll behavior check runs only
# when not cross-compiling; cross builds retain its conservative select fallback.

file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/erts/beam_release_version.h"
    CONTENT "#define BEAM_RELEASE_VERSION \"${BEAM_RELEASE_VERSION}\"\n" NEWLINE_STYLE UNIX @ONLY)

include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCSourceCompiles)

find_package(Threads REQUIRED)
if(WIN32)
    set(ETHR_WIN32_THREADS 1)
else()
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
    check_symbol_exists(CLOCK_REALTIME "time.h" BEAM_HAVE_CLOCK_REALTIME)
    check_symbol_exists(CLOCK_MONOTONIC "time.h" BEAM_HAVE_CLOCK_MONOTONIC)
    if(BEAM_HAVE_CLOCK_REALTIME)
        set(OS_SYSTEM_TIME_USING_CLOCK_GETTIME 1)
        set(WALL_CLOCK_ID CLOCK_REALTIME)
        set(WALL_CLOCK_ID_STR CLOCK_REALTIME)
    endif()
    if(BEAM_HAVE_CLOCK_MONOTONIC)
        set(OS_MONOTONIC_TIME_USING_CLOCK_GETTIME 1)
        set(MONOTONIC_CLOCK_ID CLOCK_MONOTONIC)
        set(MONOTONIC_CLOCK_ID_STR CLOCK_MONOTONIC)
        set(ETHR_HAVE_CLOCK_GETTIME_MONOTONIC 1)
        set(ETHR_MONOTONIC_CLOCK_ID CLOCK_MONOTONIC)
    endif()
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

# Platform-specific configuration precedes the shared target probes.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include("${CMAKE_CURRENT_LIST_DIR}/platform/LinuxFeatures.cmake")
elseif(ANDROID)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform/AndroidFeatures.cmake")
elseif(APPLE)
    include("${CMAKE_CURRENT_LIST_DIR}/platform/DarwinFeatures.cmake")
elseif(WIN32)
    include("${CMAKE_CURRENT_LIST_DIR}/platform/WindowsFeatures.cmake")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/platform/CommonFeatures.cmake")

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
