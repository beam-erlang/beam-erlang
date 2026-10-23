# Runtime mechanisms shared by every composition and every bare VM.
set(BEAM_CORE_MODULES
    erts_code_purger
    erlang
    erts_internal
    erts_literal_area_collector
    erts_trace_cleaner
    erts_dirty_process_signal_handler
    atomics
    counters
    persistent_term
)
set(BEAM_CORE_BEAM_FILES)
set(BEAM_CORE_SOURCES)
foreach(_module IN LISTS BEAM_CORE_MODULES)
    string(TOUPPER "${_module}" _module_upper)
    set(BEAM_CORE_${_module_upper}_BEAM
        "${CMAKE_SOURCE_DIR}/src/erts/preloaded/ebin/${_module}.beam")
    list(APPEND BEAM_CORE_SOURCES "${CMAKE_SOURCE_DIR}/src/erts/preloaded/src/${_module}.erl")
    list(APPEND BEAM_CORE_BEAM_FILES "${BEAM_CORE_${_module_upper}_BEAM}")
endforeach()
