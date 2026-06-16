# Perl generators for the emulator: beam_makeops, make_tables, make_preload,
# make_alloc_types, make_version, make_driver_tab, and the JIT asm header.
# Each flavor writes under ${CMAKE_BINARY_DIR}/emulator/${FLAVOR}.

if(NOT DEFINED ERTS_DRIVER_TAB_STATIC_NIFS)
    set(ERTS_DRIVER_TAB_STATIC_NIFS "")
endif()

function(erts_add_beam_makeops FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    # genop.tab retains its upstream-relative path under src/. beam_makeops reads it
    # with the rest of the opcode tables.
    set(_tabs
        ${CMAKE_SOURCE_DIR}/src/lib/compiler/src/genop.tab
        ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/predicates.tab
        ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/generators.tab
    )

    if(FLAVOR STREQUAL "jit")
        list(APPEND _tabs
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/ops.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/predicates.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/generators.tab
        )
        set(_jit_arg "yes")
    elseif(FLAVOR STREQUAL "emu")
        list(APPEND _tabs
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/ops.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/predicates.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/generators.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/macros.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/arith_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/bif_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/bs_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/float_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/map_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/msg_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/select_instrs.tab
            ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/emu/trace_instrs.tab
        )
        set(_jit_arg "no")
    else()
        message(FATAL_ERROR "erts_add_beam_makeops: unknown FLAVOR '${FLAVOR}'")
    endif()

    set(_wordsize 64)
    set(_code_model "default")

    set(_generated_sources ${OUTDIR}/beam_opcodes.c)
    set(_generated_headers ${OUTDIR}/beam_opcodes.h)
    if(FLAVOR STREQUAL "jit")
        list(APPEND _generated_headers
            ${OUTDIR}/beamasm_emit.h
            ${OUTDIR}/beamasm_protos.h
        )
    else()
        list(APPEND _generated_headers
            ${OUTDIR}/beam_hot.h
            ${OUTDIR}/beam_warm.h
            ${OUTDIR}/beam_cold.h
        )
    endif()
    set(_outputs ${_generated_sources} ${_generated_headers})

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/beam_makeops
                -wordsize ${_wordsize}
                -code-model ${_code_model}
                -outdir ${OUTDIR}
                -jit ${_jit_arg}
                -DUSE_VM_PROBES=0
                -emulator ${_tabs}
        DEPENDS ${_tabs} ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/beam_makeops
        COMMENT "Generating BEAM opcodes for flavor ${FLAVOR}"
        VERBATIM
    )

    add_custom_target(beam_opcodes_${FLAVOR} DEPENDS ${_outputs})
    set(ERTS_BEAM_MAKEOPS_GENERATED_SOURCES_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
    set(ERTS_BEAM_MAKEOPS_GENERATED_HEADERS_${FLAVOR} ${_generated_headers} PARENT_SCOPE)
    set(ERTS_BEAM_MAKEOPS_OUTPUTS_${FLAVOR} ${_outputs} PARENT_SCOPE)
endfunction()

function(erts_add_make_alloc_types FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    set(_generated_headers ${OUTDIR}/erl_alloc_types.h)

    if(FLAVOR STREQUAL "jit")
        set(_alloc_type_vars threads nofrag beamasm)
    elseif(FLAVOR STREQUAL "emu")
        set(_alloc_type_vars threads nofrag)
    else()
        message(FATAL_ERROR "erts_add_make_alloc_types: unknown FLAVOR '${FLAVOR}'")
    endif()

    if(WIN32)
        list(APPEND _alloc_type_vars win32)
    else()
        list(APPEND _alloc_type_vars unix)
    endif()

    add_custom_command(
        OUTPUT ${_generated_headers}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_alloc_types
                -src ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_alloc.types
                -dst ${OUTDIR}/erl_alloc_types.h
                ${_alloc_type_vars}
        DEPENDS ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_alloc.types
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_alloc_types
        COMMENT "Generating allocator types for flavor ${FLAVOR}"
        VERBATIM
    )

    add_custom_target(alloc_types_${FLAVOR} DEPENDS ${_generated_headers})
    set(ERTS_ALLOC_TYPES_GENERATED_HEADERS_${FLAVOR} ${_generated_headers} PARENT_SCOPE)
endfunction()

function(erts_add_make_tables FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    set(_jit_arg "no")
    if(FLAVOR STREQUAL "jit")
        set(_jit_arg "yes")
    endif()

    set(_generated_sources
        ${OUTDIR}/erl_atom_table.c
        ${OUTDIR}/erl_bif_table.c
        ${OUTDIR}/erl_guard_bifs.c
        ${OUTDIR}/erl_dirty_bif_wrap.c
    )
    set(_generated_headers
        ${OUTDIR}/erl_atom_table.h
        ${OUTDIR}/erl_bif_table.h
        ${OUTDIR}/erl_bif_list.h
    )
    set(_outputs ${_generated_sources} ${_generated_headers})

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_tables
                -src ${OUTDIR}
                -include ${OUTDIR}
                -dst ${OUTDIR}
                -jit ${_jit_arg}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/atom.names
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_dirty_bif.tab
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/bif.tab
        DEPENDS ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/atom.names
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/bif.tab
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/erl_dirty_bif.tab
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_tables
        COMMENT "Generating atom and BIF tables for flavor ${FLAVOR}"
        VERBATIM
    )

    add_custom_target(tables_${FLAVOR} DEPENDS ${_outputs})
    set(ERTS_MAKE_TABLES_GENERATED_SOURCES_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
    set(ERTS_MAKE_TABLES_GENERATED_HEADERS_${FLAVOR} ${_generated_headers} PARENT_SCOPE)
endfunction()

function(erts_add_make_driver_tab FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    set(_generated_sources ${OUTDIR}/driver_tab.c)

    # Only explicitly configured generic static extensions enter the VM.
    set(_driver_tab_nif_inputs)
    set(_driver_tab_driver_inputs)

    add_custom_command(
        OUTPUT ${_generated_sources}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_driver_tab
                -o ${OUTDIR}/driver_tab.c
                -nifs ${_driver_tab_nif_inputs} ${ERTS_DRIVER_TAB_STATIC_NIFS}
                -drivers ${_driver_tab_driver_inputs}
        DEPENDS ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_driver_tab
        COMMENT "Generating driver table for flavor ${FLAVOR}"
        VERBATIM
    )

    add_custom_target(driver_tab_${FLAVOR} DEPENDS ${_generated_sources})
    set(ERTS_DRIVER_TAB_GENERATED_SOURCES_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
endfunction()

# Core bytes and the only preload classification table are shared by all VMs.
function(erts_add_core_preload_target TARGET OUTDIR)
    file(MAKE_DIRECTORY "${OUTDIR}")
    set(_source "${OUTDIR}/core_preload.c")
    add_custom_command(
        OUTPUT "${_source}"
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_preload
                -prefix core_ -table core_pre_loaded ${BEAM_CORE_BEAM_FILES} > "${_source}"
        DEPENDS ${BEAM_CORE_BEAM_FILES} ${CMAKE_SOURCE_DIR}/src/erts/emulator/utils/make_preload
        COMMENT "Generating shared core preload data"
        VERBATIM
    )
    add_library(${TARGET} OBJECT "${_source}")
endfunction()

function(erts_add_make_version FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    set(_generated_headers ${OUTDIR}/erl_version.h)
    # ERTS identity follows the integrated base in BEAM_VERSION. Keep the shared
    # VM's current OTP compatibility reporting until its owning contract changes.
    file(WRITE ${_generated_headers}
"/* Generated by the beam-erlang build. */\n"
"#ifndef ERL_VERSION_H__\n"
"#define ERL_VERSION_H__\n"
"#define ERLANG_OTP_VERSION \"29.0\"\n"
"#define ERLANG_OTP_VERSION_STR \"29.0\"\n"
"#define ERLANG_VERSION \"${BEAM_ERTS_VERSION}\"\n"
"#define ERLANG_OTP_RELEASE \"29\"\n"
"#endif\n"
)

    add_custom_target(version_${FLAVOR} DEPENDS ${_generated_headers})
    set(ERTS_VERSION_GENERATED_HEADERS_${FLAVOR} ${_generated_headers} PARENT_SCOPE)
endfunction()

function(erts_add_jit_asm_global_header FLAVOR)
    if(NOT FLAVOR STREQUAL "jit")
        return()
    endif()
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    if(NOT DEFINED ERTS_TARGET_ARCH OR ERTS_TARGET_ARCH STREQUAL "")
        message(FATAL_ERROR "erts_add_jit_asm_global_header: ERTS_TARGET_ARCH not set")
    endif()

    set(_pl "${CMAKE_SOURCE_DIR}/src/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/beam_asm_global.hpp.pl")
    if(NOT EXISTS "${_pl}")
        message(FATAL_ERROR "JIT arch header script not found: ${_pl}")
    endif()

    set(_arch_outdir "${OUTDIR}/beam/jit/${ERTS_TARGET_ARCH}")
    file(MAKE_DIRECTORY "${_arch_outdir}")
    set(_out "${_arch_outdir}/beam_asm_global.hpp")

    add_custom_command(
        OUTPUT ${_out}
        COMMAND ${CMAKE_COMMAND} -E env LANG=C ${PERL_EXECUTABLE} ${_pl} > ${_out}
        DEPENDS ${_pl}
        COMMENT "Generating beam_asm_global.hpp (JIT ${ERTS_TARGET_ARCH})"
        VERBATIM
    )

    add_custom_target(jit_asm_global_header_${FLAVOR} DEPENDS ${_out})
    set(ERTS_JIT_ASM_GLOBAL_GENERATED_HEADERS_${FLAVOR} ${_out} PARENT_SCOPE)
endfunction()
