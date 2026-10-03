# Vendored ryu, pcre, zlib, and zstd. Linked into beam.

function(erts_add_emulator_third_party_libraries)
    add_library(ryu STATIC ryu/d2s.c)
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

    add_library(pcre STATIC
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
    set_target_properties(pcre PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(zlib STATIC
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
    set_target_properties(zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_library(zstd STATIC
        zstd/common/debug.c
        zstd/common/entropy_common.c
        zstd/common/error_private.c
        zstd/common/fse_decompress.c
        zstd/common/pool.c
        zstd/common/threading.c
        zstd/common/xxhash.c
        zstd/common/zstd_common.c
        zstd/compress/fse_compress.c
        zstd/compress/hist.c
        zstd/compress/huf_compress.c
        zstd/compress/zstd_compress.c
        zstd/compress/zstd_compress_literals.c
        zstd/compress/zstd_compress_sequences.c
        zstd/compress/zstd_compress_superblock.c
        zstd/compress/zstd_double_fast.c
        zstd/compress/zstd_fast.c
        zstd/compress/zstd_lazy.c
        zstd/compress/zstd_ldm.c
        zstd/compress/zstdmt_compress.c
        zstd/compress/zstd_opt.c
        zstd/compress/zstd_preSplit.c
        zstd/decompress/huf_decompress.c
        zstd/decompress/zstd_ddict.c
        zstd/decompress/zstd_decompress_block.c
        zstd/decompress/zstd_decompress.c
    )
    if(ERTS_TARGET_ARCH STREQUAL "x86")
        target_sources(zstd PRIVATE zstd/decompress/huf_decompress_amd64.S)
    endif()
    set_target_properties(zstd PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()
