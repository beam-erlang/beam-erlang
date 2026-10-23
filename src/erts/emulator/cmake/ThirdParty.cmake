# Vendored ryu, pcre and zlib linked into the VM.

function(erts_add_emulator_third_party_libraries)
    add_library(ryu OBJECT ryu/d2s.c)
    set_target_properties(ryu PROPERTIES POSITION_INDEPENDENT_CODE ON)

    set(PCRE_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/pcre)
    file(MAKE_DIRECTORY ${PCRE_GEN_DIR})
    set(PCRE_GEN_HEADERS
        ${PCRE_GEN_DIR}/pcre2_match_loop_break_cases.gen.h
        ${PCRE_GEN_DIR}/pcre2_match_memchr_break_cases.gen.h
        ${PCRE_GEN_DIR}/pcre2_match_yield_coverage.gen.h
    )
    set(PCRE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/pcre/pcre2_match.c)
    set(PCRE_GENS_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/generate_pcre_headers.pl)

    add_custom_command(
        OUTPUT ${PCRE_GEN_HEADERS}
        COMMAND ${PERL_EXECUTABLE} ${PCRE_GENS_SCRIPT}
            ${PCRE_SRC}
            ${PCRE_GEN_DIR}/pcre2_match_loop_break_cases.gen.h
            ${PCRE_GEN_DIR}/pcre2_match_memchr_break_cases.gen.h
            ${PCRE_GEN_DIR}/pcre2_match_yield_coverage.gen.h
        DEPENDS ${PCRE_SRC} ${PCRE_GENS_SCRIPT}
        COMMENT "Generating pcre headers"
        VERBATIM
    )
    add_custom_target(pcre_gens DEPENDS ${PCRE_GEN_HEADERS})

    add_library(pcre OBJECT
        pcre/pcre2_auto_possess.c
        pcre/pcre2_chartables.c
        pcre/pcre2_chkdint.c
        pcre/pcre2_compile.c
        pcre/pcre2_compile_cgroup.c
        pcre/pcre2_compile_class.c
        pcre/pcre2_config.c
        pcre/pcre2_context.c
        pcre/pcre2_error.c
        pcre/pcre2_extuni.c
        pcre/pcre2_find_bracket.c
        pcre/pcre2_match.c
        pcre/pcre2_match_data.c
        pcre/pcre2_newline.c
        pcre/pcre2_ord2utf.c
        pcre/pcre2_pattern_info.c
        pcre/pcre2_script_run.c
        pcre/pcre2_serialize.c
        pcre/pcre2_string_utils.c
        pcre/pcre2_study.c
        pcre/pcre2_substring.c
        pcre/pcre2_tables.c
        pcre/pcre2_ucd.c
        pcre/pcre2_valid_utf.c
        pcre/pcre2_xclass.c
    )
    add_dependencies(pcre pcre_gens)
    target_include_directories(pcre PRIVATE ${PCRE_GEN_DIR})
    target_compile_definitions(pcre PRIVATE
        PCRE2_CODE_UNIT_WIDTH=8
        ERLANG_INTEGRATION
    )
    set_source_files_properties(pcre/pcre2_match.c
        PROPERTIES OBJECT_DEPENDS "${PCRE_GEN_HEADERS}")
    # Erlang's PCRE has private match state and is not the system PCRE ABI.
    # It must neither interpose on host libraries nor bind to their PCRE symbols.
    set_target_properties(pcre PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)

    add_library(zlib OBJECT
        zlib/adler32.c
        zlib/compress.c
        zlib/crc32.c
        zlib/uncompr.c
        zlib/deflate.c
        zlib/trees.c
        zlib/zutil.c
        zlib/inflate.c
        zlib/inftrees.c
        zlib/inffast.c
    )
    # Compression is private to each linked VM/component. Do not interpose on
    # an independently built host or NIF's declared system zlib dependency.
    set_target_properties(zlib PROPERTIES POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden)

    # Visibility isolates shared objects. Distinct names also isolate archives
    # from unrelated objects linked into the same executable.
    foreach(_target IN ITEMS beam_emulator pcre zlib)
        target_compile_options(${_target} PRIVATE
            "-include" "${CMAKE_CURRENT_SOURCE_DIR}/beam_private_vendor_symbols.h")
    endforeach()

endfunction()
