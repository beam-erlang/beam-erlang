# The Erlang zlib interface owns a NIF component. The compression backend is
# also used by the VM and remains a separate shared dependency of libbeam.
set(BEAM_ZLIB_BEAM "${CMAKE_CURRENT_SOURCE_DIR}/ebin/zlib.beam" PARENT_SCOPE)

set(_bytecode "${CMAKE_BINARY_DIR}/components/zlib/ebin/zlib.beam")
beam_add_erlang_component(beam_zlib_modules SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zlib.erl"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components/zlib/ebin")
beam_add_component(beam_zlib_component NAME zlib KIND BEAM_COMPONENT_ZLIB
    MODULES zlib BEAM_FILES "${_bytecode}" NIF_ENTRIES beam_zlib_nif_init
    NATIVE_SOURCES zlib_nif.c DEPENDS beam_zlib_modules)
target_compile_definitions(beam_zlib_component PRIVATE STATIC_ERLANG_NIF_LIBNAME=beam_zlib)
target_link_libraries(beam_zlib_component PRIVATE zlib)
if(MSVC)
    target_compile_options(beam_zlib_component PRIVATE
        "/FI${PROJECT_SOURCE_DIR}/src/erts/emulator/beam_private_vendor_symbols.h")
else()
    target_compile_options(beam_zlib_component PRIVATE
        "-include" "${PROJECT_SOURCE_DIR}/src/erts/emulator/beam_private_vendor_symbols.h")
endif()
target_include_directories(beam_zlib_component PRIVATE
    "${PROJECT_SOURCE_DIR}/src/erts/emulator/zlib")
