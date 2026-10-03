# Linux architecture used by the generators and the JIT.
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
            "Unsupported Linux architecture ${CMAKE_SYSTEM_PROCESSOR} "
            "(x86_64 and aarch64 only)")
    endif()
    set(${out_var} ${_arch} PARENT_SCOPE)
    message(STATUS "Detected ERTS_TARGET_ARCH: ${_arch}")
endfunction()

erts_detect_architecture(ERTS_TARGET_ARCH)
set(ERTS_TARGET_ARCH ${ERTS_TARGET_ARCH} CACHE STRING
    "High-level arch for generators and the JIT (x86 or arm)")

if(NOT DEFINED ERTS_HOST_TRIPLE OR ERTS_HOST_TRIPLE STREQUAL "")
    set(ERTS_HOST_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-pc-linux-gnu")
endif()
set(ERTS_HOST_TRIPLE "${ERTS_HOST_TRIPLE}" CACHE STRING
    "Host triple recorded as ERLANG_ARCHITECTURE")
message(STATUS "ERTS_HOST_TRIPLE: ${ERTS_HOST_TRIPLE}")
