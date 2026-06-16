cmake_minimum_required(VERSION 3.25)
project(beam_components_acceptance LANGUAGES C)
find_package(BEAM CONFIG REQUIRED)
add_executable(components_acceptance main.c)
target_link_libraries(components_acceptance PRIVATE BEAM::Runtime)
target_compile_features(components_acceptance PRIVATE c_std_11)
set_target_properties(components_acceptance PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/../lib")
add_executable(facilities_acceptance facilities.c)
target_link_libraries(facilities_acceptance PRIVATE BEAM::Runtime)
target_compile_features(facilities_acceptance PRIVATE c_std_11)
set_target_properties(facilities_acceptance PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/../lib")
add_executable(contracts_acceptance contracts.c)
target_link_libraries(contracts_acceptance PRIVATE BEAM::Runtime)
target_compile_features(contracts_acceptance PRIVATE c_std_11)
set_target_properties(contracts_acceptance PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN/../lib")

target_include_directories(components_acceptance PRIVATE "${TEST_SUPPORT_DIR}")
beam_target_bytecode_verifier(components_acceptance INVENTORIES base services)

target_include_directories(facilities_acceptance PRIVATE "${TEST_SUPPORT_DIR}")
beam_target_bytecode_verifier(facilities_acceptance INVENTORIES base services)

target_include_directories(contracts_acceptance PRIVATE "${TEST_SUPPORT_DIR}")
beam_target_bytecode_verifier(contracts_acceptance INVENTORIES base boot services)
