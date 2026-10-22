set(_bundle_default OFF)
if(BEAM_BUILD_CLI AND CMAKE_SYSTEM_NAME MATCHES "^(Linux|Windows|Darwin)$")
    set(_bundle_default ON)
endif()
option(BEAM_BUNDLE_PACKAGE_TOOL "Bundle the independently maintained package application" ${_bundle_default})
if(NOT BEAM_BUNDLE_PACKAGE_TOOL)
    return()
endif()
# Build the selected package-tool sources using the distribution bootstrap.
set(BEAM_PACKAGE_SOURCE_DIR "" CACHE PATH
    "Local beam-erlang-package source checkout (required; never downloaded)")
get_filename_component(_package_source "${BEAM_PACKAGE_SOURCE_DIR}" ABSOLUTE
    BASE_DIR "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${_package_source}/cmake/PackageTool.cmake" OR
   NOT EXISTS "${_package_source}/src/beam_pkg.erl")
    message(FATAL_ERROR
        "BEAM_PACKAGE_SOURCE_DIR must select a complete local beam-erlang-package checkout: ${_package_source}. Provide -DBEAM_PACKAGE_SOURCE_DIR=/path/to/beam-erlang-package; sources are not downloaded.")
endif()
include("${_package_source}/cmake/PackageTool.cmake")
if(NOT COMMAND beam_package_build)
    message(FATAL_ERROR "BEAM_PACKAGE_SOURCE_DIR does not provide beam_package_build")
endif()
set(_pkg_root "${CMAKE_BINARY_DIR}/${BEAM_PACKAGE_INSTALL_DIR}")
beam_package_build(beam_pkg_support
    OUTPUT_DIRECTORY "${_pkg_root}"
    COMPILER_COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic
    COMPILER_DEPENDS beam_bootstrap
    DESCRIPTOR_TOOL "${CMAKE_SOURCE_DIR}/tools/beam-application.py"
    RUNTIME_BUILD_ID "${BEAM_COMPONENT_BUILD_ID}")
get_target_property(_modules beam_pkg_support BEAM_PACKAGE_MODULES)
get_target_property(_files beam_pkg_support BEAM_PACKAGE_FILES)
get_target_property(_manifest beam_pkg_support BEAM_PACKAGE_INVENTORY)
if(NOT _modules OR NOT _files OR NOT _manifest)
    message(FATAL_ERROR "Incomplete package-tool build inventory from BEAM_PACKAGE_SOURCE_DIR")
endif()
beam_package_install(beam_pkg_support "$<TARGET_FILE:beam_launch>" relative "beam${CMAKE_EXECUTABLE_SUFFIX}")
