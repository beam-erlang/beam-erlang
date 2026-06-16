# Private command recipes are used only by command-lifecycle acceptance.
if(NOT PRIVATE_HOST_BUILD OR NOT BEAM_PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PRIVATE_HOST_BUILD is required for private CLI acceptance")
endif()
add_library(private_cli_recipe STATIC
    "${BEAM_PROJECT_SOURCE_DIR}/src/beam_cli_recipe.c")
target_include_directories(private_cli_recipe PUBLIC "${BEAM_PROJECT_SOURCE_DIR}/src")
target_include_directories(private_cli_recipe PRIVATE
    "${PRIVATE_HOST_BUILD}/pkg/include" "${PRIVATE_HOST_BUILD}/run/include" "${PRIVATE_HOST_BUILD}/cli/include"
    "${PRIVATE_HOST_BUILD}/interactive/include")
target_link_libraries(private_cli_recipe PUBLIC BEAM::Runtime)
beam_target_bytecode_verifier(private_cli_recipe INVENTORIES base boot services)
