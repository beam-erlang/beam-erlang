# Private algorithms shared by explicitly inventoried BEAM tooling/components.
set(BEAM_SUPPORT_MODULES
    array beam_lib binary dict digraph digraph_utils erl_abstract_code
    erl_anno erl_bits erl_error erl_eval erl_internal erl_parse erl_posix_msg
    erl_pp erl_scan erl_stdlib_errors ets eval_bits filelib filename
    gb_sets gb_trees graph io_lib io_lib_format io_lib_fread io_lib_pretty lists
    maps math orddict ordsets otp_internal proplists queue records sets
    sofs string unicode unicode_util)
set(BEAM_SUPPORT_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/support/ebin")
set(BEAM_SUPPORT_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(_sources)
foreach(_module IN LISTS BEAM_SUPPORT_MODULES)
    list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/src/${_module}.erl")
endforeach()
beam_add_erlang_component(beam_support_modules
    COMPILER beam_seed_compiler SOURCES ${_sources}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/doc/src/sofs.md"
    INCLUDE_DIRECTORIES "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
    OUTPUT_DIRECTORY "${BEAM_SUPPORT_OUTPUT_DIRECTORY}")
foreach(_value IN ITEMS MODULES OUTPUT_DIRECTORY INCLUDE_DIRECTORY)
    set(BEAM_SUPPORT_${_value} "${BEAM_SUPPORT_${_value}}" PARENT_SCOPE)
endforeach()
