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
set(BEAM_BOOT_NATIVE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_return.c" PARENT_SCOPE)
set(BEAM_BOOT_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_cli_host.c" PARENT_SCOPE)
