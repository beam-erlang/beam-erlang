# BEAM/Erlang: one target SDK and an explicit native bootstrap.
#
# Configures and links lib/libbeam.so and bin/beam for one emulator flavor
# (jit or emu). Private hosts compile the modern components from source using
# the independently attested compiler seeds.

cmake_minimum_required(VERSION 3.25)
cmake_policy(SET CMP0141 NEW)
# Embedded debug records avoid a build-time PDB server and work for both the
# shared SDK and its independently linked static consumers.
if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()
include(cmake/Version.cmake)

project(
    beam-erlang
    VERSION "${BEAM_PROJECT_VERSION}"
    DESCRIPTION "BEAM runtime"
    HOMEPAGE_URL "https://beam.erlangsters.org"
    LANGUAGES C CXX
)

include(cmake/ProjectOptions.cmake)
include(cmake/TargetPlatform.cmake)
include(cmake/PlatformTargets.cmake)
include(cmake/HostTools.cmake)

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
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeModules.cmake)

# Keep generated headers and object outputs at their established build paths.
add_library(beam_erts_headers INTERFACE)
add_library(beam-erlang::erts_headers ALIAS beam_erts_headers)
target_include_directories(beam_erts_headers INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/erts
    ${CMAKE_BINARY_DIR}/erts/include
    ${CMAKE_BINARY_DIR}/erts/include/internal
)
add_subdirectory(src/erts/emulator erts/emulator)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageTool.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Runtime.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallSDK.cmake)
beam_register_host_tools_manifest()

include(cmake/Testing.cmake)

message(STATUS "beam-erlang configured")
message(STATUS "  Flavor: ${ERTS_BUILD_FLAVOR}")
message(STATUS "  Arch: ${ERTS_TARGET_ARCH}")
message(STATUS "  Triple: ${ERTS_HOST_TRIPLE}")
