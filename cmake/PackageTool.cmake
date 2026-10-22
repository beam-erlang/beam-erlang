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
set(_pkg_root "${CMAKE_BINARY_DIR}/lib/beam/tools/pkg")
set(BEAM_PKG_INCLUDE_DIRECTORY "${CMAKE_BINARY_DIR}/pkg/include")
beam_package_build(beam_pkg_support
    OUTPUT_DIRECTORY "${_pkg_root}"
    COMPILER_COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic
    COMPILER_DEPENDS beam_bootstrap)
get_target_property(_modules beam_pkg_support BEAM_PACKAGE_MODULES)
get_target_property(_files beam_pkg_support BEAM_PACKAGE_FILES)
get_target_property(_manifest beam_pkg_support BEAM_PACKAGE_INVENTORY)
if(NOT _modules OR NOT _files OR NOT _manifest)
    message(FATAL_ERROR "Incomplete package-tool build inventory from BEAM_PACKAGE_SOURCE_DIR")
endif()
set(_header "/* Private matching-build package tool inventory. */\nstatic const char *const beam_pkg_modules[] = {\n")
foreach(_module IN LISTS _modules)
    if(NOT _module MATCHES "^[a-z][a-zA-Z0-9_]*$")
        message(FATAL_ERROR "Invalid package-tool module name: ${_module}")
    endif()
    string(APPEND _header "    \"${_module}\",\n")
endforeach()
list(LENGTH _modules _count)
string(APPEND _header "};\n#define BEAM_PKG_MODULE_COUNT ${_count}\n")
file(CONFIGURE OUTPUT "${BEAM_PKG_INCLUDE_DIRECTORY}/beam_pkg_inventory.h"
    CONTENT "${_header}" @ONLY)
install(FILES ${_files} DESTINATION lib/beam/tools/pkg/ebin)
install(FILES "${_manifest}" DESTINATION lib/beam/tools/pkg)
