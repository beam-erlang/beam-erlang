# Private command recipes are used only by command-lifecycle acceptance.
if(NOT PRIVATE_HOST_BUILD OR NOT BEAM_PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PRIVATE_HOST_BUILD is required for private CLI acceptance")
endif()
add_library(private_cli_recipe STATIC
    "${BEAM_PROJECT_SOURCE_DIR}/src/beam_cli_recipe.c")
target_include_directories(private_cli_recipe PUBLIC "${BEAM_PROJECT_SOURCE_DIR}/src")
target_include_directories(private_cli_recipe PRIVATE
    "${PRIVATE_HOST_BUILD}/run/include" "${PRIVATE_HOST_BUILD}/cli/include"
    "${PRIVATE_HOST_BUILD}/interactive/include")
target_link_libraries(private_cli_recipe PUBLIC BEAM::Runtime)
set(_component_directory lib)
if(WIN32)
    set(_component_directory bin)
endif()
target_compile_definitions(private_cli_recipe PRIVATE
    BEAM_CLI_COMPONENT_DIRECTORY="${_component_directory}"
    BEAM_CLI_LIBRARY_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}"
    BEAM_CLI_LIBRARY_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
beam_target_bytecode_verifier(private_cli_recipe INVENTORIES base boot services)
