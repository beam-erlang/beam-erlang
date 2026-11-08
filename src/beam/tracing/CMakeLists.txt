# Ordinary public facade, private formatter and native delivery share one producer.
set(_out "${CMAKE_BINARY_DIR}/components/tracing/ebin")
beam_add_runtime_component(beam_tracing_modules
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_trace.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/beam_runtime_trace_errors.erl"
    OUTPUT_DIRECTORY "${_out}")
beam_add_library(beam_tracing_component NAME tracing
    MODULES beam_trace beam_runtime_trace_errors
    BEAM_FILES "${_out}/beam_trace.beam" "${_out}/beam_runtime_trace_errors.beam"
    NIF_ENTRIES beam_trace_nif_init -
    NATIVE_SOURCES beam_trace_nif.c DEPENDS beam_tracing_modules)
target_compile_definitions(beam_tracing_component PRIVATE STATIC_ERLANG_NIF_LIBNAME=beam_trace)
