# Standalone host-memory codec: no emulator compile or link dependencies.
set(_etf_sources
    ${CMAKE_SOURCE_DIR}/src/beam_etf.c
    ${CMAKE_SOURCE_DIR}/src/etf/cmp_nc.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_atom.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_big.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_binary.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_boolean.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_char.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_double.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_fun.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_iodata.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_list_header.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_long.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_longlong.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_pid.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_port.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_ref.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_skip.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_string.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_tuple_header.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_ulong.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_ulonglong.c
    ${CMAKE_SOURCE_DIR}/src/etf/decode_version.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_atom.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_big.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_binary.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_boolean.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_char.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_double.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_fun.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_list_header.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_long.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_longlong.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_pid.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_port.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_ref.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_string.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_tuple_header.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_ulong.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_ulonglong.c
    ${CMAKE_SOURCE_DIR}/src/etf/encode_version.c
    ${CMAKE_SOURCE_DIR}/src/etf/get_type.c
)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/etf")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/etf/config.h" CONTENT
"#define SIZEOF_VOID_P ${CMAKE_SIZEOF_VOID_P}
#define SIZEOF_LONG ${SIZEOF_LONG}
#define SIZEOF_LONG_LONG ${SIZEOF_LONG_LONG}
#define HAVE_ISFINITE 1
" @ONLY)
foreach(_target beam_etf_objects beam_etf_embed_objects)
    add_library(${_target} OBJECT ${_etf_sources})
    target_compile_features(${_target} PRIVATE c_std_11)
    # Match Erl_Interface configure.ac: long uses the wide implementation on LP64.
    if(SIZEOF_LONG EQUAL 8)
        target_compile_definitions(${_target} PRIVATE EI_64BIT)
    endif()
    set_target_properties(${_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${_target} PRIVATE "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/src/etf" "${CMAKE_BINARY_DIR}/etf")
endforeach()
target_sources(beam_etf_embed_objects PRIVATE "${CMAKE_SOURCE_DIR}/src/etf/compat.c")
target_compile_definitions(beam_etf_embed_objects PRIVATE BEAM_ETF_EMBED=1)
set_target_properties(beam_etf_embed_objects PROPERTIES EXPORT_NAME ETFObjects)
target_include_directories(beam_etf_embed_objects INTERFACE $<INSTALL_INTERFACE:include>)
if(NOT WIN32)
    target_link_libraries(beam_etf_embed_objects INTERFACE m)
endif()

# Allocation can cross the incorporation boundary (ei receive buffers and funs).
# Require the archive producer to select the same Microsoft C runtime.
if(MSVC)
    set(_beam_etf_crt "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    if(NOT _beam_etf_crt)
        set(_beam_etf_crt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    set_property(TARGET beam_etf_embed_objects PROPERTY MSVC_RUNTIME_LIBRARY "${_beam_etf_crt}")
    set(_beam_etf_crt_value "$<GENEX_EVAL:$<TARGET_PROPERTY:beam_etf_embed_objects,MSVC_RUNTIME_LIBRARY>>")
    set(_beam_etf_crt_dll "$<OR:$<STREQUAL:${_beam_etf_crt_value},MultiThreadedDLL>,$<STREQUAL:${_beam_etf_crt_value},MultiThreadedDebugDLL>>")
    set(_beam_etf_crt_debug "$<OR:$<STREQUAL:${_beam_etf_crt_value},MultiThreadedDebug>,$<STREQUAL:${_beam_etf_crt_value},MultiThreadedDebugDLL>>")
    # Resolve the producer configuration now. Comparing generator expressions
    # would incorrectly accept a Debug archive containing Release SDK objects.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/etf/BEAMETFCRT-$<CONFIG>.h" CONTENT
"/* The embedded codec and its archive must share the same allocation domain. */
#if !defined(_MSC_VER) || !defined(_MT)
#error ETF objects require the SDK Microsoft C runtime
#endif
#if defined(_DLL) != ${_beam_etf_crt_dll}
#error ETF objects and their archive select different static/dynamic C runtimes
#endif
#if defined(_DEBUG) != ${_beam_etf_crt_debug}
#error ETF objects and their archive select different debug/release C runtimes
#endif
")
endif()
