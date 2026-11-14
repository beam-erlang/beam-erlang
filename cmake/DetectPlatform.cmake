# Native architecture used by the generators and the JIT.
# ERTS_TARGET_ARCH is x86 (x86_64) or arm (aarch64).

function(erts_detect_architecture out_var)
    set(_arch "unknown")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(_arch "x86")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(_arch "arm")
    endif()
    if(_arch STREQUAL "unknown")
        message(FATAL_ERROR
            "Unsupported architecture ${CMAKE_SYSTEM_PROCESSOR} "
            "(x86_64 and aarch64 only)")
    endif()
    set(${out_var} ${_arch} PARENT_SCOPE)
    message(STATUS "Detected ERTS_TARGET_ARCH: ${_arch}")
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"
            OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$"
            OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        message(FATAL_ERROR "macOS requires a native Apple Silicon build")
    endif()
    if(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "macOS supports only a single arm64 architecture")
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
            "Minimum macOS version" FORCE)
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "15.0")
        message(FATAL_ERROR "macOS deployment target must be 15.0 or newer")
    endif()
    include(CheckCSourceCompiles)
    check_c_source_compiles("
        #if !defined(__APPLE__) || !defined(__aarch64__)
        #error Native Apple Silicon compiler required
        #endif
        int main(void) { return 0; }
    " ERTS_HAVE_NATIVE_DARWIN_ARM64)
    if(NOT ERTS_HAVE_NATIVE_DARWIN_ARM64)
        message(FATAL_ERROR "macOS requires an Apple Silicon C compiler target")
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows"
            OR NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64"
            OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        message(FATAL_ERROR "Windows requires a native x64 build")
    endif()
endif()

erts_detect_architecture(ERTS_TARGET_ARCH)
set(ERTS_TARGET_ARCH ${ERTS_TARGET_ARCH} CACHE STRING
    "High-level arch for generators and the JIT (x86 or arm)")

if(NOT DEFINED ERTS_HOST_TRIPLE OR ERTS_HOST_TRIPLE STREQUAL "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(ERTS_HOST_TRIPLE "aarch64-apple-darwin${CMAKE_SYSTEM_VERSION}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(ERTS_HOST_TRIPLE "x86_64-pc-windows-msvc")
    else()
        set(ERTS_HOST_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-pc-linux-gnu")
    endif()
endif()
set(ERTS_HOST_TRIPLE "${ERTS_HOST_TRIPLE}" CACHE STRING
    "Host triple recorded as ERLANG_ARCHITECTURE")
message(STATUS "ERTS_HOST_TRIPLE: ${ERTS_HOST_TRIPLE}")
