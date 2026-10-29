cmake_minimum_required(VERSION 3.25)
project(beam_returning_acceptance LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
include("${TEST_SUPPORT_DIR}/HostObservation.cmake")
set(_beam_test_rpath "$ORIGIN/../lib")
if(APPLE)
    set(_beam_test_rpath "@executable_path/../lib")
endif()
set(_sources main.c support.c etf.c base.c lifecycle.c code_output.c services.c)
add_executable(returning_acceptance ${_sources})
target_link_libraries(returning_acceptance PRIVATE BEAM::Host BEAM::Runtime)
target_compile_features(returning_acceptance PRIVATE c_std_11)
add_executable(returning_minimal ${_sources})
target_link_libraries(returning_minimal PRIVATE BEAM::Host BEAM::Runtime)
target_compile_features(returning_minimal PRIVATE c_std_11)
set_target_properties(returning_minimal PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "${_beam_test_rpath}")

# This fixture intentionally violates the matching-build descriptor contract.
# The fixture uses only public SDK declarations.
add_library(mismatched_component MODULE mismatched_component.c)
target_link_libraries(mismatched_component PRIVATE BEAM::NIF)
set_target_properties(mismatched_component PROPERTIES PREFIX "" OUTPUT_NAME "mismatched-component")

target_include_directories(returning_acceptance PRIVATE "${TEST_SUPPORT_DIR}")
beam_target_bytecode_verifier(returning_acceptance INVENTORIES base boot services)

target_include_directories(returning_minimal PRIVATE "${TEST_SUPPORT_DIR}")
beam_target_bytecode_verifier(returning_minimal INVENTORIES base boot services)

beam_test_observe(returning_acceptance)
beam_test_observe(returning_minimal)
