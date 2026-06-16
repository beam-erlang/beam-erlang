set(BEAM_ERL_TRACER_BEAM
    "${CMAKE_CURRENT_SOURCE_DIR}/ebin/erl_tracer.beam" PARENT_SCOPE)

add_library(beam_erl_tracer_nif MODULE erl_tracer_nif.c)
beam_configure_dynamic_nif(beam_erl_tracer_nif beam erl_tracer)
