# beam-erlang — Linux build of the BEAM virtual machine.
#
# Configures and links lib/libbeam.so and bin/beam for one emulator flavor
# (jit or emu). Private hosts compile the modern components from source using
# the independently attested compiler seeds.

cmake_minimum_required(VERSION 3.25)
include(cmake/Version.cmake)

project(
    beam-erlang
    VERSION "${BEAM_PROJECT_VERSION}"
    DESCRIPTION "BEAM runtime"
    HOMEPAGE_URL "https://beam.erlangsters.org"
    LANGUAGES C CXX
)

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
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Single-config build type" FORCE)
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE must be Release (got '${CMAKE_BUILD_TYPE}')")
endif()

# Release archives keep debug information without changing optimization or
# stripping the installed payload after integrity metadata has been generated.
option(BEAM_RELEASE_DEBUG_INFO "Retain native debug information in release archives" OFF)
if(BEAM_RELEASE_DEBUG_INFO)
    string(APPEND CMAKE_C_FLAGS_RELEASE " -g")
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")
    string(APPEND CMAKE_ASM_FLAGS_RELEASE " -g")
endif()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/erts/beam_release_version.h"
    CONTENT "#define BEAM_RELEASE_VERSION \"${BEAM_RELEASE_VERSION}\"\n" @ONLY)

set(ERTS_BUILD_FLAVOR "jit" CACHE STRING "Emulator flavor: jit or emu")
set_property(CACHE ERTS_BUILD_FLAVOR PROPERTY STRINGS jit emu)
if(NOT ERTS_BUILD_FLAVOR MATCHES "^(jit|emu)$")
    message(FATAL_ERROR
        "ERTS_BUILD_FLAVOR must be jit or emu (got '${ERTS_BUILD_FLAVOR}')")
endif()

# In-tree zlib and ryu. Kernel poll (epoll) is required, not optional.
set(ERTS_ZLIB_VENDORED ON)

find_package(Perl REQUIRED)
message(STATUS "Perl: ${PERL_EXECUTABLE}")

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DetectPlatform.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCoreHeaders.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativeComponent.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativeLibraries.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InetDrivers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ErlangComponent.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BeamComponent.cmake)
add_subdirectory(src/beam)
set(BEAM_DYNAMIC_NIF_TARGETS ${BEAM_NATIVE_NIF_TARGETS})
set(BEAM_DYNAMIC_DRIVER_TARGETS ${BEAM_DRIVER_TARGETS})
add_subdirectory(src/core/shared_data)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeModules.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageTool.cmake)
# Keep generated headers and object outputs at their established build paths.
add_library(beam_erts_headers INTERFACE)
add_library(beam-erlang::erts_headers ALIAS beam_erts_headers)
target_include_directories(beam_erts_headers INTERFACE
    ${CMAKE_BINARY_DIR}/erts
    ${CMAKE_BINARY_DIR}/erts/include
    ${CMAKE_BINARY_DIR}/erts/include/internal
)
add_subdirectory(emulator erts/emulator)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Runtime.cmake)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

message(STATUS "beam-erlang configured")
message(STATUS "  Flavor: ${ERTS_BUILD_FLAVOR}")
message(STATUS "  Arch: ${ERTS_TARGET_ARCH}")
message(STATUS "  Triple: ${ERTS_HOST_TRIPLE}")
