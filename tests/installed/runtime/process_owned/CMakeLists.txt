cmake_minimum_required(VERSION 3.25)
project(beam_process_owned_acceptance LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
add_executable(process_owned main.c)
target_link_libraries(process_owned PRIVATE BEAM::Runtime)
target_compile_features(process_owned PRIVATE c_std_11)
set_target_properties(process_owned PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib")
add_executable(preloads preloads.c)
target_link_libraries(preloads PRIVATE BEAM::Runtime)
target_compile_features(preloads PRIVATE c_std_11)
set_target_properties(preloads PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib")
