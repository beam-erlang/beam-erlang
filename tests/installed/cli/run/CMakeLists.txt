cmake_minimum_required(VERSION 3.25)
project(beam_source_run_acceptance LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
include("${BEAM_TEST_SUPPORT_DIR}/PrivateCLI.cmake")
add_executable(source_run_acceptance main.c)
target_link_libraries(source_run_acceptance PRIVATE private_cli_recipe m)
target_compile_features(source_run_acceptance PRIVATE c_std_11)
set_target_properties(source_run_acceptance PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/../lib")
