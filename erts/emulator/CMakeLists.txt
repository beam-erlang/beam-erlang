# The BEAM virtual machine. The product target is the beam executable.

include(${CMAKE_SOURCE_DIR}/cmake/ErlangGenerators.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompileFlags.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ycf.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/EmulatorSources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/ThirdParty.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Ethread.cmake)

if(ERTS_BUILD_FLAVOR STREQUAL "jit")
    erts_add_beam_makeops(jit)
    erts_add_make_alloc_types(jit)
    erts_add_make_tables(jit)
    erts_add_make_preload(jit)
    erts_add_make_version(jit)
    erts_add_jit_asm_global_header(jit)
    erts_add_make_driver_tab(jit)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/jit")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_jit}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_jit}
        ${ERTS_PRELOAD_GENERATED_SOURCES_jit}
        ${ERTS_DRIVER_TAB_GENERATED_SOURCES_jit}
    )
    set(EMULATOR_GENERATED_HEADERS
        ${ERTS_BEAM_MAKEOPS_GENERATED_HEADERS_jit}
        ${ERTS_ALLOC_TYPES_GENERATED_HEADERS_jit}
        ${ERTS_MAKE_TABLES_GENERATED_HEADERS_jit}
        ${ERTS_VERSION_GENERATED_HEADERS_jit}
        ${ERTS_JIT_ASM_GLOBAL_GENERATED_HEADERS_jit}
    )
elseif(ERTS_BUILD_FLAVOR STREQUAL "emu")
    erts_add_beam_makeops(emu)
    erts_add_make_alloc_types(emu)
    erts_add_make_tables(emu)
    erts_add_make_preload(emu)
    erts_add_make_version(emu)
    erts_add_make_driver_tab(emu)
    set(EMULATOR_GENERATED_DIR "${CMAKE_BINARY_DIR}/emulator/emu")
    set(EMULATOR_GENERATED_SOURCES
        ${ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_emu}
        ${ERTS_MAKE_TABLES_GENERATED_SOURCES_emu}
        ${ERTS_PRELOAD_GENERATED_SOURCES_emu}
        ${ERTS_DRIVER_TAB_GENERATED_SOURCES_emu}
    )
    set(EMULATOR_GENERATED_HEADERS
        ${ERTS_BEAM_MAKEOPS_GENERATED_HEADERS_emu}
        ${ERTS_ALLOC_TYPES_GENERATED_HEADERS_emu}
        ${ERTS_MAKE_TABLES_GENERATED_HEADERS_emu}
        ${ERTS_VERSION_GENERATED_HEADERS_emu}
    )
else()
    message(FATAL_ERROR "Unsupported ERTS_BUILD_FLAVOR: ${ERTS_BUILD_FLAVOR}")
endif()

target_include_directories(beam_erts_headers INTERFACE
    ${EMULATOR_GENERATED_DIR}
)

add_library(beam_emulator OBJECT)
target_sources(beam_emulator PRIVATE ${EMULATOR_GENERATED_SOURCES})

add_custom_target(emulator_generated_headers_${ERTS_BUILD_FLAVOR}
    DEPENDS ${EMULATOR_GENERATED_HEADERS}
)
add_dependencies(beam_emulator emulator_generated_headers_${ERTS_BUILD_FLAVOR})

# config.h includes erl_atom_table.h. Listing the generated headers on the
# interface target makes every consumer wait for make_tables.
target_sources(beam_erts_headers INTERFACE ${EMULATOR_GENERATED_HEADERS})

erts_configure_compile_flags_header(beam_emulator)
erts_configure_ycf_headers(beam_emulator)
target_link_libraries(beam_emulator PRIVATE beam-erlang::erts_headers)

erts_add_emulator_sources(beam_emulator)
erts_add_emulator_third_party_libraries()
erts_add_ethread_library()

set(_erts_beam_libs beam_emulator ryu pcre zstd zlib ethread m ${CMAKE_DL_LIBS})
if(ERTS_TERMCAP_LIB)
    list(APPEND _erts_beam_libs ${ERTS_TERMCAP_LIB})
endif()

add_executable(beam)
target_link_libraries(beam PRIVATE ${_erts_beam_libs})
set_target_properties(beam PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET beam POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        $<TARGET_FILE_NAME:beam>
        ${CMAKE_BINARY_DIR}/bin/beam.smp
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        $<TARGET_FILE_NAME:beam>
        ${CMAKE_BINARY_DIR}/bin/beam.${ERTS_BUILD_FLAVOR}
    COMMENT "Publishing beam.smp and beam.${ERTS_BUILD_FLAVOR}"
)

message(STATUS "beam executable: ${CMAKE_BINARY_DIR}/bin/beam (${ERTS_BUILD_FLAVOR})")
