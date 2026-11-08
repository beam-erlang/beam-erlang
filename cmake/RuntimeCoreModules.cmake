# Runtime mechanisms shared by every composition and every bare VM.
set(BEAM_CORE_MODULES
    erts_code_purger
    erlang
    erts_internal
    erts_literal_area_collector
    beam_internal
    erts_dirty_process_signal_handler
)
set(BEAM_PRIVATE_CORE_MODULES beam_internal)
set(BEAM_CORE_BEAM_FILES)
set(BEAM_CORE_SOURCES)
foreach(_module IN LISTS BEAM_CORE_MODULES)
    string(TOUPPER "${_module}" _module_upper)
    set(_core_root "${CMAKE_SOURCE_DIR}/src/erts/preloaded")
    if(_module IN_LIST BEAM_PRIVATE_CORE_MODULES)
        set(_core_root "${CMAKE_SOURCE_DIR}/src/beam/core")
    endif()
    set(BEAM_CORE_${_module_upper}_BEAM "${_core_root}/ebin/${_module}.beam")
    set(BEAM_CORE_${_module_upper}_SOURCE "${_core_root}/src/${_module}.erl")
    list(APPEND BEAM_CORE_SOURCES "${BEAM_CORE_${_module_upper}_SOURCE}")
    list(APPEND BEAM_CORE_BEAM_FILES "${BEAM_CORE_${_module_upper}_BEAM}")
endforeach()
