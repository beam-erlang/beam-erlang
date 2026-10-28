# Ordinary returning-entry policy compiled by the verified bootstrap.
set(BEAM_BOOT_MODULES beam_return_boot beam_boot)
set(BEAM_BOOT_SOURCES)
set(BEAM_BOOT_BEAM_FILES)
foreach(_module IN LISTS BEAM_BOOT_MODULES)
    list(APPEND BEAM_BOOT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
    list(APPEND BEAM_BOOT_BEAM_FILES "${CMAKE_BINARY_DIR}/lib/beam/boot/ebin/${_module}.beam")
endforeach()
beam_add_erlang_component(beam_boot_modules
    SOURCES ${BEAM_BOOT_SOURCES}
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/boot/ebin")
foreach(_value IN ITEMS MODULES SOURCES BEAM_FILES)
    set(BEAM_BOOT_${_value} "${BEAM_BOOT_${_value}}" PARENT_SCOPE)
endforeach()
set(BEAM_BOOT_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_cli_host.c" PARENT_SCOPE)

# Compile returning policy in an isolated public-header target. Its object is
# included for the existing SDK convenience exports, never used by VM internals.
foreach(_header IN ITEMS beam runtime boot)
    configure_file("${CMAKE_SOURCE_DIR}/include/beam/${_header}.h"
        "${CMAKE_BINARY_DIR}/public-sdk/include/beam/${_header}.h" COPYONLY)
endforeach()
add_library(beam_return_helper OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/beam_return.c")
target_include_directories(beam_return_helper PRIVATE "${CMAKE_BINARY_DIR}/public-sdk/include")
target_compile_features(beam_return_helper PRIVATE c_std_11)
set_target_properties(beam_return_helper PROPERTIES POSITION_INDEPENDENT_CODE ON)


beam_add_bytecode_inventory(beam_boot_inventory NAME boot
    MODULES ${BEAM_BOOT_MODULES} INPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/boot/ebin"
    INSTALL_ROOT lib/beam/boot DEPENDS beam_boot_modules)
