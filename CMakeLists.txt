cmake_minimum_required(VERSION 3.25)

# Only the single-config Ninja generator is supported.
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR
        "Only the Ninja generator is supported. Use a preset or configure with -G Ninja.")
endif()

# Set Apple platform defaults before compiler identification and feature probes.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
    endif()
    if(NOT DEFINED CMAKE_OSX_SYSROOT)
        set(CMAKE_OSX_SYSROOT "macosx" CACHE STRING "macOS SDK")
    endif()
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/BEAM_VERSION" BEAM_VERSION LIMIT_COUNT 1)

project(
    beam-erlang
    VERSION "${BEAM_VERSION}"
    DESCRIPTION "Erlangsters BEAM/Erlang"
    HOMEPAGE_URL "https://beam.erlangsters.org"
    LANGUAGES C CXX
)

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|Windows)$")
    message(FATAL_ERROR
        "This build supports Linux, macOS, and Windows "
        "(CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME})")
endif()
if(APPLE AND (NOT CMAKE_C_COMPILER_ID STREQUAL "AppleClang"
             OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"))
    message(FATAL_ERROR "macOS builds require Apple Clang from Xcode or Command Line Tools")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT MSVC)
    message(FATAL_ERROR "Windows builds require MSVC (CMAKE_C_COMPILER_ID=${CMAKE_C_COMPILER_ID})")
endif()

# GAS for zstd's huf_decompress_amd64.S on Unix x86_64. Windows skips that
# file. The resource compiler embeds the Windows preload dictionary.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    enable_language(RC)
else()
    enable_language(ASM)
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
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|Debug)$")
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE must be Release or Debug (got '${CMAKE_BUILD_TYPE}')")
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Release Debug)

set(ERTS_BUILD_FLAVOR "jit" CACHE STRING "Emulator flavor: jit or emu")
set_property(CACHE ERTS_BUILD_FLAVOR PROPERTY STRINGS jit emu)
if(NOT ERTS_BUILD_FLAVOR MATCHES "^(jit|emu)$")
    message(FATAL_ERROR
        "ERTS_BUILD_FLAVOR must be jit or emu (got '${ERTS_BUILD_FLAVOR}')")
endif()

# In-tree zlib, ryu, and zstd. Native kernel polling is required.
set(ERTS_ZLIB_VENDORED ON)

find_package(Perl REQUIRED)
message(STATUS "Perl: ${PERL_EXECUTABLE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectPlatform.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CoreHeaders.cmake)

include(GNUInstallDirs)
include(CTest)
# Retained sources keep their upstream paths; generated headers live in erts/.
add_subdirectory(erts)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

message(STATUS "beam-erlang configured")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Flavor: ${ERTS_BUILD_FLAVOR}")
message(STATUS "  Arch: ${ERTS_TARGET_ARCH}")
message(STATUS "  Triple: ${ERTS_HOST_TRIPLE}")
if(APPLE)
    message(STATUS "  SDK: ${CMAKE_OSX_SYSROOT}")
    message(STATUS "  Deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
