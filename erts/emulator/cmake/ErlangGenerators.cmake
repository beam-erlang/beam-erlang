# Perl generators for the emulator: beam_makeops, make_tables, make_preload,
# make_alloc_types, make_version, make_driver_tab, and the JIT asm header.
# Each flavor writes under ${CMAKE_BINARY_DIR}/emulator/${FLAVOR}.

if(NOT DEFINED ERTS_DRIVER_TAB_STATIC_NIFS)
    set(ERTS_DRIVER_TAB_STATIC_NIFS "")
endif()

function(erts_add_beam_makeops FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    # genop.tab lives at lib/compiler/src/genop.tab. beam_makeops reads it
    # with the rest of the opcode tables.
    set(_tabs
        ${CMAKE_SOURCE_DIR}/lib/compiler/src/genop.tab
        ${CMAKE_SOURCE_DIR}/erts/emulator/beam/predicates.tab
        ${CMAKE_SOURCE_DIR}/erts/emulator/beam/generators.tab
    )

    if(FLAVOR STREQUAL "jit")
        list(APPEND _tabs
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/ops.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/predicates.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/generators.tab
        )
        set(_jit_arg "yes")
    elseif(FLAVOR STREQUAL "emu")
        list(APPEND _tabs
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/ops.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/predicates.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/generators.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/macros.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/arith_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/bif_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/bs_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/float_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/map_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/msg_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/select_instrs.tab
            ${CMAKE_SOURCE_DIR}/erts/emulator/beam/emu/trace_instrs.tab
        )
        set(_jit_arg "no")
    else()
        message(FATAL_ERROR "erts_add_beam_makeops: unknown FLAVOR '${FLAVOR}'")
    endif()

    set(_wordsize 64)
    set(_code_model "default")

    set(_generated_sources ${OUTDIR}/beam_opcodes.c)
    set(_generated_headers
        ${OUTDIR}/beam_opcodes.h
        ${OUTDIR}/beam_hot.h
        ${OUTDIR}/beam_warm.h
        ${OUTDIR}/beam_cold.h
    )
    if(FLAVOR STREQUAL "jit")
        list(APPEND _generated_headers
            ${OUTDIR}/beamasm_emit.h
            ${OUTDIR}/beamasm_protos.h
        )
    endif()
    set(_outputs ${_generated_sources} ${_generated_headers})

    add_custom_command(
        OUTPUT ${_outputs}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/beam_makeops
                -wordsize ${_wordsize}
                -code-model ${_code_model}
                -outdir ${OUTDIR}
                -jit ${_jit_arg}
                -DUSE_VM_PROBES=0
                -emulator ${_tabs}
        DEPENDS ${_tabs} ${CMAKE_SOURCE_DIR}/erts/emulator/utils/beam_makeops
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

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(_alloc_ostype win32)
    else()
        set(_alloc_ostype unix)
    endif()
    if(FLAVOR STREQUAL "jit")
        set(_alloc_type_vars threads nofrag beamasm ${_alloc_ostype})
    elseif(FLAVOR STREQUAL "emu")
        set(_alloc_type_vars threads nofrag ${_alloc_ostype})
    else()
        message(FATAL_ERROR "erts_add_make_alloc_types: unknown FLAVOR '${FLAVOR}'")
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _alloc_type_vars debug)
    endif()

    # Makefiles do not track changes to custom-command arguments. A content-
    # stable recipe dependency also invalidates outputs when switching types.
    set(_alloc_recipe "${OUTDIR}/allocator-recipe.txt")
    file(CONFIGURE OUTPUT "${_alloc_recipe}"
        CONTENT "build_type=${CMAKE_BUILD_TYPE}\nvariables=${_alloc_type_vars}\n"
        @ONLY)

    add_custom_command(
        OUTPUT ${_generated_headers}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_alloc_types
                -src ${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_alloc.types
                -dst ${OUTDIR}/erl_alloc_types.h
                ${_alloc_type_vars}
        DEPENDS ${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_alloc.types
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_alloc_types
                "${_alloc_recipe}"
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
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_tables
                -src ${OUTDIR}
                -include ${OUTDIR}
                -dst ${OUTDIR}
                -jit ${_jit_arg}
                ${CMAKE_SOURCE_DIR}/erts/emulator/beam/atom.names
                ${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_dirty_bif.tab
                ${CMAKE_SOURCE_DIR}/erts/emulator/beam/bif.tab
        DEPENDS ${CMAKE_SOURCE_DIR}/erts/emulator/beam/atom.names
                ${CMAKE_SOURCE_DIR}/erts/emulator/beam/bif.tab
                ${CMAKE_SOURCE_DIR}/erts/emulator/beam/erl_dirty_bif.tab
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_tables
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

    # Basenames match the NIF and driver objects linked into beam.
    # No static application NIFs.
    set(_driver_tab_nif_inputs
        prim_tty_nif.o
        erl_tracer_nif.o
        prim_buffer_nif.o
        prim_file_nif.o
        zlib_nif.o
        zstd_nif.o
        prim_socket_nif.o
        prim_net_nif.o
    )
    set(_driver_tab_driver_inputs
        inet_drv.o
        ram_file_drv.o
    )

    add_custom_command(
        OUTPUT ${_generated_sources}
        COMMAND ${PERL_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_driver_tab
                -o ${OUTDIR}/driver_tab.c
                -nifs ${_driver_tab_nif_inputs} ${ERTS_DRIVER_TAB_STATIC_NIFS}
                -drivers ${_driver_tab_driver_inputs}
        DEPENDS ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_driver_tab
        COMMENT "Generating driver table for flavor ${FLAVOR}"
        VERBATIM
    )

    add_custom_target(driver_tab_${FLAVOR} DEPENDS ${_generated_sources})
    set(ERTS_DRIVER_TAB_GENERATED_SOURCES_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
endfunction()

function(erts_add_make_preload FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    set(_preload_beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erts_code_purger.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erl_init.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/init.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_buffer.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_eval.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_inet.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_file.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/zlib.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/socket_registry.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_socket.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_net.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/prim_zip.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erl_prim_loader.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erlang.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erts_internal.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erl_tracer.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erts_literal_area_collector.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erts_trace_cleaner.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/erts_dirty_process_signal_handler.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/atomics.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/counters.beam
        ${CMAKE_SOURCE_DIR}/erts/preloaded/ebin/persistent_term.beam
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        # Win32 sys_preloaded() reads ERLANG_DICT / ERLANG_CODE resources.
        # The Unix preload.c array is not linked.
        set(_generated_sources ${OUTDIR}/beams.rc)
        set(_list_file ${OUTDIR}/preload-beams.txt)
        list(JOIN _preload_beam "\n" _preload_lines)
        file(WRITE "${_list_file}" "${_preload_lines}\n")
        add_custom_command(
            OUTPUT ${_generated_sources}
            COMMAND ${CMAKE_COMMAND}
                -DINPUT_LIST=${_list_file}
                -DOUTPUT=${_generated_sources}
                -P ${CMAKE_SOURCE_DIR}/cmake/GenerateWin32Preload.cmake
            DEPENDS ${_preload_beam} ${_list_file}
                ${CMAKE_SOURCE_DIR}/cmake/GenerateWin32Preload.cmake
            COMMENT "Generating beams.rc for flavor ${FLAVOR}"
            VERBATIM
        )
        set(ERTS_PRELOAD_RC_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
        set(ERTS_PRELOAD_GENERATED_SOURCES_${FLAVOR} "" PARENT_SCOPE)
    else()
        set(_generated_sources ${OUTDIR}/preload.c)
        add_custom_command(
            OUTPUT ${_generated_sources}
            COMMAND ${PERL_EXECUTABLE}
                    ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_preload
                    -old ${_preload_beam} > ${OUTDIR}/preload.c
            DEPENDS ${_preload_beam} ${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_preload
            COMMENT "Generating preload.c for flavor ${FLAVOR}"
            VERBATIM
        )
        set(ERTS_PRELOAD_RC_${FLAVOR} "" PARENT_SCOPE)
        set(ERTS_PRELOAD_GENERATED_SOURCES_${FLAVOR} ${_generated_sources} PARENT_SCOPE)
    endif()
    add_custom_target(preload_${FLAVOR} DEPENDS ${_generated_sources})
endfunction()

function(erts_add_make_version FLAVOR)
    set(OUTDIR "${CMAKE_BINARY_DIR}/emulator/${FLAVOR}")
    file(MAKE_DIRECTORY "${OUTDIR}")

    file(READ ${CMAKE_SOURCE_DIR}/OTP_VERSION _otp_vsn)
    string(STRIP "${_otp_vsn}" _otp_vsn)
    string(REPLACE "." "\\." _otp_pattern "${_otp_vsn}")
    file(STRINGS "${CMAKE_SOURCE_DIR}/otp_versions.table" _release_entry
        REGEX "^OTP-${_otp_pattern} : ")
    list(LENGTH _release_entry _release_count)
    if(NOT _release_count EQUAL 1)
        message(FATAL_ERROR "Expected one release-table entry for OTP ${_otp_vsn}")
    endif()
    string(REGEX MATCH "(^|[ \t])erts-([^ \t]+)" _erts_entry "${_release_entry}")
    if(NOT _erts_entry)
        message(FATAL_ERROR "Release table has no ERTS version for OTP ${_otp_vsn}")
    endif()
    set(_erts_vsn "${CMAKE_MATCH_2}")
    string(REGEX MATCH "^[0-9]+" _otp_release "${_otp_vsn}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/OTP_VERSION" "${CMAKE_SOURCE_DIR}/otp_versions.table")
    set(_generated_headers ${OUTDIR}/erl_version.h)
    add_custom_command(OUTPUT ${_generated_headers}
        COMMAND "${PERL_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_version"
            -o "${_generated_headers}" "${_otp_release}" "${_otp_vsn}"
            "${_erts_vsn}" "${ERTS_HOST_TRIPLE}"
        DEPENDS "${CMAKE_SOURCE_DIR}/erts/emulator/utils/make_version"
            "${CMAKE_SOURCE_DIR}/OTP_VERSION" "${CMAKE_SOURCE_DIR}/otp_versions.table"
        COMMENT "Generating ERTS ${_erts_vsn} / OTP ${_otp_vsn} runtime identity"
        VERBATIM)

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

    set(_pl "${CMAKE_SOURCE_DIR}/erts/emulator/beam/jit/${ERTS_TARGET_ARCH}/beam_asm_global.hpp.pl")
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
