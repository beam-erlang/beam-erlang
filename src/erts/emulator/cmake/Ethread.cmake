# pthread ethread static library linked into beam.

function(erts_add_ethread_library)
    if(NOT DEFINED EMULATOR_GENERATED_DIR)
        message(FATAL_ERROR "erts_add_ethread_library requires EMULATOR_GENERATED_DIR")
    endif()

    add_library(ethread OBJECT
        ../lib_src/common/ethr_aux.c
        ../lib_src/common/ethr_atomics.c
        ../lib_src/common/ethr_mutex.c
        ../lib_src/common/ethr_cbf.c
        ../lib_src/${BEAM_THREAD_DIRECTORY}/ethread.c
        ../lib_src/${BEAM_THREAD_DIRECTORY}/ethr_event.c
    )
    target_compile_definitions(ethread PRIVATE
        ${BEAM_PLATFORM_DEFINITIONS}
        HAVE_CONFIG_H ERTS_CONFIG_SKIP_ATOM_TABLE
    )
    target_link_libraries(ethread PRIVATE beam-erlang::erts_headers)
    add_dependencies(ethread tables_${ERTS_BUILD_FLAVOR})
    target_include_directories(ethread PRIVATE
        ${CMAKE_BINARY_DIR}/erts
        ${CMAKE_BINARY_DIR}/erts/include
        ${CMAKE_BINARY_DIR}/erts/include/internal
        ${EMULATOR_GENERATED_DIR}
        ../include
        ../include/internal
        beam
        sys/${BEAM_SYS_DIRECTORY}
    )
    set_target_properties(ethread PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
