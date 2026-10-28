# Private CLI I/O and formatting support shared by source execution and interaction.
set(_cli_root "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/cli")
set(_cli_out "${_cli_root}/ebin")
set(_cli_include "${CMAKE_BINARY_DIR}/cli/include")
file(MAKE_DIRECTORY "${_cli_include}")
set(_cli_owned beam_run_io error_handler io io_lib io_lib_pretty erl_scan)
set(_cli_support binary io_lib_format io_lib_fread records string unicode unicode_util)
# NETWORK already owns these exact artifacts. The distribution recipe acquires
# the private copies only when no network component is selected.
set(BEAM_CLI_FALLBACK_MODULES lists maps)
set(BEAM_CLI_MODULES ${_cli_owned} ${_cli_support})
set(BEAM_CLI_BEAM_FILES)
set(BEAM_CLI_FALLBACK_BEAM_FILES)
add_custom_target(beam_cli_owned)
foreach(_module IN LISTS _cli_owned)
    set(_target "beam_cli_module_${_module}")
    beam_add_erlang_component(${_target}
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl"
        OUTPUT_DIRECTORY "${_cli_out}")
    add_dependencies(beam_cli_owned ${_target})
    list(APPEND BEAM_CLI_BEAM_FILES "${_cli_out}/${_module}.beam")
endforeach()
set(_support_outputs)
foreach(_module IN LISTS _cli_support BEAM_CLI_FALLBACK_MODULES)
    set(_input "${BEAM_SUPPORT_OUTPUT_DIRECTORY}/${_module}.beam")
    set(_output "${_cli_out}/${_module}.beam")
    add_custom_command(OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_cli_out}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_input}" "${_output}"
        DEPENDS "${_input}" beam_support_modules VERBATIM)
    list(APPEND _support_outputs "${_output}")
    if(_module IN_LIST BEAM_CLI_FALLBACK_MODULES)
        list(APPEND BEAM_CLI_FALLBACK_BEAM_FILES "${_output}")
    else()
        list(APPEND BEAM_CLI_BEAM_FILES "${_output}")
    endif()
endforeach()
add_custom_target(beam_cli_retained DEPENDS ${_support_outputs})
add_custom_target(beam_cli_support)
add_dependencies(beam_cli_support beam_cli_owned beam_cli_retained)

set(_inventory "/* Private matching-build CLI module inventory. */\n#ifndef BEAM_CLI_INVENTORY_H\n#define BEAM_CLI_INVENTORY_H\nstatic const char *const beam_cli_modules[] = {\n")
foreach(_module IN LISTS BEAM_CLI_MODULES)
    string(APPEND _inventory "    \"${_module}\",\n")
endforeach()
list(LENGTH BEAM_CLI_MODULES _module_count)
string(APPEND _inventory "};\n#define BEAM_CLI_MODULE_COUNT ${_module_count}\nstatic const char *const beam_cli_fallback_modules[] = {\n")
foreach(_module IN LISTS BEAM_CLI_FALLBACK_MODULES)
    string(APPEND _inventory "    \"${_module}\",\n")
endforeach()
list(LENGTH BEAM_CLI_FALLBACK_MODULES _fallback_count)
string(APPEND _inventory "};\n#define BEAM_CLI_FALLBACK_MODULE_COUNT ${_fallback_count}\n#endif\n")
file(CONFIGURE OUTPUT "${_cli_include}/beam_cli_inventory.h" CONTENT "${_inventory}" NEWLINE_STYLE UNIX @ONLY)
string(JOIN "\n" _manifest ${BEAM_CLI_MODULES} ${BEAM_CLI_FALLBACK_MODULES})
file(CONFIGURE OUTPUT "${_cli_root}/modules.txt" CONTENT "${_manifest}\n" NEWLINE_STYLE UNIX @ONLY)
install(FILES ${BEAM_CLI_BEAM_FILES} ${BEAM_CLI_FALLBACK_BEAM_FILES}
    DESTINATION lib/beam/runtime/beam/cli/ebin)
install(FILES "${_cli_root}/modules.txt" DESTINATION lib/beam/runtime/beam/cli)
set(BEAM_CLI_INCLUDE_DIRECTORY "${_cli_include}" PARENT_SCOPE)
foreach(_value IN ITEMS MODULES BEAM_FILES FALLBACK_MODULES FALLBACK_BEAM_FILES)
    set(BEAM_CLI_${_value} "${BEAM_CLI_${_value}}" PARENT_SCOPE)
endforeach()

set(_reserved "static const char *const beam_cli_core_modules[] = {\n")
# Installed host modules are supplied from the actual acquired composition.
foreach(_module IN LISTS BEAM_CORE_MODULES)
    string(APPEND _reserved "    \"${_module}\",\n")
endforeach()
string(APPEND _reserved "};\n")
file(CONFIGURE OUTPUT "${_cli_include}/beam_cli_core_inventory.h" CONTENT "${_reserved}" NEWLINE_STYLE UNIX @ONLY)

beam_add_erlang_component(beam_application_support
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_application_run.erl"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/application/ebin")
add_dependencies(beam_cli_support beam_application_support)
install(FILES "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/application/ebin/beam_application_run.beam"
    DESTINATION lib/beam/runtime/beam/application/ebin)
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/application/modules.txt"
    CONTENT "beam_application_run\n" NEWLINE_STYLE UNIX @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/application/modules.txt"
    DESTINATION lib/beam/runtime/beam/application)
