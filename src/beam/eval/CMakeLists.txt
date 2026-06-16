# The committed artifact combines prim_eval.S with the Erlang stub's metadata.
# Normal builds stage these bytes; compiling prim_eval.erl alone is incorrect.
set(BEAM_EVAL_MODULES prim_eval)
set(BEAM_EVAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.erl"
    "${CMAKE_CURRENT_SOURCE_DIR}/prim_eval.S")
set(BEAM_EVAL_BEAM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ebin/prim_eval.beam")
set(BEAM_PRIM_EVAL_BEAM "${BEAM_EVAL_BEAM_FILES}" PARENT_SCOPE)
foreach(_value IN ITEMS MODULES SOURCES BEAM_FILES)
    set(BEAM_EVAL_${_value} "${BEAM_EVAL_${_value}}" PARENT_SCOPE)
endforeach()
