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

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateCoreHeaders.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativeComponent.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ErlangComponent.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BeamComponent.cmake)
include(cmake/RuntimeCoreModules.cmake)
set(BEAM_CORE_MODULE_INITIALIZERS "")
foreach(_module IN LISTS BEAM_CORE_MODULES)
    string(APPEND BEAM_CORE_MODULE_INITIALIZERS "    \"${_module}\",\n")
endforeach()
configure_file("${CMAKE_SOURCE_DIR}/cmake/beam_core_modules.h.in"
    "${CMAKE_BINARY_DIR}/erts/beam_core_modules.h" @ONLY)
include(cmake/ComponentBuildId.cmake)
set(BEAM_PUBLIC_HEADERS beam.h runtime.h boot.h native.h zlib.h native_image.h library.h host.h sdk/bytecode.h sdk/inventories.h)

# Keep generated headers and object outputs at their established build paths.
add_library(beam_erts_headers INTERFACE)
add_library(beam-erlang::erts_headers ALIAS beam_erts_headers)
target_include_directories(beam_erts_headers INTERFACE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/erts
    ${CMAKE_BINARY_DIR}/erts/include
    ${CMAKE_BINARY_DIR}/erts/include/internal
)
add_subdirectory(src/beam)
add_subdirectory(src/erts/emulator erts/emulator)
add_dependencies(beam_run beam_verify_core_artifacts)
add_custom_target(beam_refresh_boot_artifacts)
add_dependencies(beam_refresh_boot_artifacts
    beam_refresh_native_artifacts beam_refresh_core_artifacts beam_refresh_base_artifacts)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageTool.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallSDK.cmake)
beam_register_host_tools_manifest()

include(CTest)
if(BUILD_TESTING AND NOT BEAM_HOST_TOOLS_ONLY)
    add_subdirectory(tests)
endif()
beam_apply_directory_build_options("${CMAKE_SOURCE_DIR}")

message(STATUS "beam-erlang configured")
message(STATUS "  Flavor: ${ERTS_BUILD_FLAVOR}")
message(STATUS "  Arch: ${ERTS_TARGET_ARCH}")
message(STATUS "  Triple: ${ERTS_HOST_TRIPLE}")
