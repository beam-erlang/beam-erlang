cmake_minimum_required(VERSION 3.25)
project(beam_interactive_acceptance LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
include("${BEAM_TEST_SUPPORT_DIR}/PrivateCLI.cmake")
add_executable(interactive_acceptance main.c)
target_link_libraries(interactive_acceptance PRIVATE private_cli_recipe m)
target_compile_features(interactive_acceptance PRIVATE c_std_11)
set_target_properties(interactive_acceptance PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/../lib")
