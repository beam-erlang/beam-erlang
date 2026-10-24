cmake_minimum_required(VERSION 3.25)
project(beam_compiler_lifetime LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
add_executable(compiler_lifetime compiler_lifetime.c beam_compiler_host.c)
target_compile_features(compiler_lifetime PRIVATE c_std_99)
target_compile_options(compiler_lifetime PRIVATE -Wall -Wextra -Werror)
target_link_libraries(compiler_lifetime PRIVATE BEAM::Runtime)


include("${TEST_SUPPORT_DIR}/HostObservation.cmake")
beam_test_observe(compiler_lifetime)
