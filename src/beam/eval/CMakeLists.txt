# The committed artifact combines prim_eval.S with the Erlang stub's metadata.
# Normal builds stage these bytes; compiling prim_eval.erl alone is incorrect.
set(BEAM_EVAL_MODULES prim_eval)
set(BEAM_EVAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.S")
set(BEAM_EVAL_BEAM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ebin/prim_eval.beam")
set(BEAM_PRIM_EVAL_BEAM "${BEAM_EVAL_BEAM_FILES}" PARENT_SCOPE)

set(_eval_verified "${CMAKE_BINARY_DIR}/eval-verification/verified.stamp")
add_custom_command(OUTPUT "${_eval_verified}"
    COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic --debug-info --assembly-backed
        -o "${CMAKE_BINARY_DIR}/eval-verification" --
        "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.erl"
    COMMAND ${CMAKE_COMMAND} -E compare_files
        "${CMAKE_BINARY_DIR}/eval-verification/prim_eval.beam" "${BEAM_EVAL_BEAM_FILES}"
    COMMAND ${CMAKE_COMMAND} -E touch "${_eval_verified}"
    DEPENDS beam_bootstrap ${BEAM_EVAL_SOURCES} ${BEAM_EVAL_BEAM_FILES}
    COMMENT "Verifying assembly-backed evaluator bytecode" VERBATIM)
add_custom_target(beam_verify_eval_artifact ALL DEPENDS "${_eval_verified}")
add_custom_target(beam_refresh_eval_artifact
    COMMAND $<TARGET_FILE:beam_bootstrap> --deterministic --debug-info --assembly-backed
        -o "${CMAKE_BINARY_DIR}/eval-artifacts" --
        "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.erl"
    DEPENDS beam_bootstrap ${BEAM_EVAL_SOURCES}
    COMMENT "Generating reviewable evaluator artifact candidate" VERBATIM)
foreach(_value IN ITEMS MODULES SOURCES BEAM_FILES)
    set(BEAM_EVAL_${_value} "${BEAM_EVAL_${_value}}" PARENT_SCOPE)
endforeach()
