# beam-erlang — Linux build of the BEAM virtual machine.
#
# Configures and links bin/beam for one emulator flavor (jit or emu).
# OTP applications, the erl launcher, and other host tools are not built.

cmake_minimum_required(VERSION 3.25)

project(
    beam-erlang
    VERSION 29.0
    DESCRIPTION "BEAM runtime"
    HOMEPAGE_URL "https://beam.erlangsters.org"
    LANGUAGES C CXX
)

# GAS for zstd's huf_decompress_amd64.S on x86_64. JIT on aarch64 is C++.
enable_language(ASM)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "This build supports Linux only (CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})")
endif()

# gnu11: beam_emu dispatch uses computed gotos. C++17 is required by the JIT.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Single-config build type" FORCE)
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE must be Release (got '${CMAKE_BUILD_TYPE}')")
endif()

set(ERTS_BUILD_FLAVOR "jit" CACHE STRING "Emulator flavor: jit or emu")
set_property(CACHE ERTS_BUILD_FLAVOR PROPERTY STRINGS jit emu)
if(NOT ERTS_BUILD_FLAVOR MATCHES "^(jit|emu)$")
    message(FATAL_ERROR
        "ERTS_BUILD_FLAVOR must be jit or emu (got '${ERTS_BUILD_FLAVOR}')")
endif()

# In-tree zlib, ryu, and zstd. Kernel poll (epoll) is required, not optional.
set(ERTS_ZLIB_VENDORED ON)

find_package(Perl REQUIRED)
message(STATUS "Perl: ${PERL_EXECUTABLE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectPlatform.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCoreHeaders.cmake)

add_subdirectory(erts)

message(STATUS "beam-erlang configured")
message(STATUS "  Flavor: ${ERTS_BUILD_FLAVOR}")
message(STATUS "  Arch: ${ERTS_TARGET_ARCH}")
message(STATUS "  Triple: ${ERTS_HOST_TRIPLE}")
