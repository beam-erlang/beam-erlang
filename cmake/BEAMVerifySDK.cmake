# SPDX-License-Identifier: Apache-2.0
# Read-only installed SDK verification, usable without executing target binaries.
cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/BEAMRuntimeArtifacts.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/BEAMTarget.cmake")
beam_read_target_metadata(_facts)
if(BEAM_VERIFY_COMPILER)
    beam_get_runtime_artifact(_cli NAME command.beam KIND executable-helper)
endif()

if(_facts_system STREQUAL "iOS" OR _facts_system STREQUAL "Android")
    beam_get_runtime_artifact(_toolchain NAME sdk.toolchain KIND consumer-toolchain)
endif()

# Validate every declared native/build input before target assembly or aggregation.
# Final application signatures intentionally change Mach-O bytes after this point.
foreach(_artifact IN LISTS BEAM_RUNTIME_ARTIFACTS)
    beam_get_runtime_artifact(_verified NAME "${_artifact}")
endforeach()
