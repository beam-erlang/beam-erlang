# Ordinary Erlang module and NIF bridge; the codec belongs to libbeam.
set(BEAM_ZLIB_BEAM "${CMAKE_CURRENT_SOURCE_DIR}/ebin/zlib.beam" PARENT_SCOPE)

set(_bytecode "${CMAKE_BINARY_DIR}/components/zlib/ebin/zlib.beam")
beam_add_erlang_component(beam_zlib_modules SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zlib.erl"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components/zlib/ebin")
beam_add_library(beam_zlib_component NAME zlib
    MODULES zlib BEAM_FILES "${_bytecode}" NIF_ENTRIES beam_zlib_nif_init
    NATIVE_SOURCES zlib_nif.c DEPENDS beam_zlib_modules)
target_compile_definitions(beam_zlib_component PRIVATE STATIC_ERLANG_NIF_LIBNAME=beam_zlib)
