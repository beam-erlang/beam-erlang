# Owned shell/language policy and explicit matching-build evaluator support.
set(_interactive_root "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/interactive")
set(_interactive_out "${_interactive_root}/ebin")
set(_interactive_include "${CMAKE_BINARY_DIR}/interactive/include")
file(MAKE_DIRECTORY "${_interactive_include}")
set(_interactive_owned beam_interactive shell shell_default erl_eval erl_parse
    erl_pp erl_features erl_lint erl_expand_records)
set(_interactive_retained dict digraph digraph_utils erl_anno erl_bits erl_error
    erl_internal erl_posix_msg erl_stdlib_errors ets eval_bits gb_sets gb_trees
    math orddict ordsets otp_internal proplists queue sets sofs)
set(BEAM_INTERACTIVE_MODULES ${_interactive_owned} ${_interactive_retained} prim_eval)
set(BEAM_INTERACTIVE_BEAM_FILES)
add_custom_target(beam_interactive_owned)
foreach(_module IN LISTS _interactive_owned)
    set(_target "beam_interactive_module_${_module}")
    beam_add_erlang_component(${_target}
        SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl"
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_DIRECTORY "${_interactive_out}")
    add_dependencies(beam_interactive_owned ${_target})
    list(APPEND BEAM_INTERACTIVE_BEAM_FILES "${_interactive_out}/${_module}.beam")
endforeach()
set(_retained_outputs)
foreach(_module IN LISTS _interactive_retained)
    set(_input "${BEAM_SUPPORT_OUTPUT_DIRECTORY}/${_module}.beam")
    set(_output "${_interactive_out}/${_module}.beam")
    add_custom_command(OUTPUT "${_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_interactive_out}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_input}" "${_output}"
        DEPENDS "${_input}" beam_support_modules VERBATIM)
    list(APPEND _retained_outputs "${_output}")
    list(APPEND BEAM_INTERACTIVE_BEAM_FILES "${_output}")
endforeach()
# Selective receive must retain the canonical assembly-backed implementation.
set(_prim_eval "${CMAKE_CURRENT_SOURCE_DIR}/../eval/ebin/prim_eval.beam")
set(_prim_eval_output "${_interactive_out}/prim_eval.beam")
add_custom_command(OUTPUT "${_prim_eval_output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_interactive_out}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_prim_eval}" "${_prim_eval_output}"
    DEPENDS "${_prim_eval}" VERBATIM)
list(APPEND _retained_outputs "${_prim_eval_output}")
list(APPEND BEAM_INTERACTIVE_BEAM_FILES "${_prim_eval_output}")
add_custom_target(beam_interactive_retained DEPENDS ${_retained_outputs})
add_custom_target(beam_interactive_support)
add_dependencies(beam_interactive_support beam_interactive_owned beam_interactive_retained)

set(_inventory "/* Private matching-build interactive module inventory. */\n#ifndef BEAM_INTERACTIVE_INVENTORY_H\n#define BEAM_INTERACTIVE_INVENTORY_H\nstatic const char *const beam_interactive_modules[] = {\n")
foreach(_module IN LISTS BEAM_INTERACTIVE_MODULES)
    string(APPEND _inventory "    \"${_module}\",\n")
endforeach()
list(LENGTH BEAM_INTERACTIVE_MODULES _module_count)
string(APPEND _inventory "};\n#define BEAM_INTERACTIVE_MODULE_COUNT ${_module_count}\n#endif\n")
file(CONFIGURE OUTPUT "${_interactive_include}/beam_interactive_inventory.h" CONTENT "${_inventory}" @ONLY)
string(JOIN "\n" _manifest ${BEAM_INTERACTIVE_MODULES})
file(CONFIGURE OUTPUT "${_interactive_root}/modules.txt" CONTENT "${_manifest}\n" @ONLY)
install(FILES ${BEAM_INTERACTIVE_BEAM_FILES} DESTINATION lib/beam/runtime/beam/interactive/ebin)
install(FILES "${_interactive_root}/modules.txt" DESTINATION lib/beam/runtime/beam/interactive)
set(BEAM_INTERACTIVE_INCLUDE_DIRECTORY "${_interactive_include}" PARENT_SCOPE)
foreach(_value IN ITEMS MODULES BEAM_FILES)
    set(BEAM_INTERACTIVE_${_value} "${BEAM_INTERACTIVE_${_value}}" PARENT_SCOPE)
endforeach()
