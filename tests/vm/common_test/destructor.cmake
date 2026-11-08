cmake_minimum_required(VERSION 3.25)
project(VMDestructorNIF LANGUAGES C)
find_package(BEAM CONFIG REQUIRED COMPONENTS NIF)
add_library(erts_test_destructor MODULE "${DESTRUCTOR_SOURCE}")
target_link_libraries(erts_test_destructor PRIVATE BEAM::NIF)
beam_target_nif(erts_test_destructor)
set_target_properties(erts_test_destructor PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${DESTRUCTOR_OUTPUT}")
