# Matching-build test unit. Fault controls are absent from the production unit
# and from every installed/deployed SDK profile.
set(_file "${CMAKE_SOURCE_DIR}/src/beam/file")
if(WIN32)
    set(_backend "${_file}/win32/win_prim_file.c")
else()
    set(_backend "${_file}/unix/unix_prim_file.c")
endif()
set(_fault "${CMAKE_CURRENT_BINARY_DIR}/file-faults")
file(MAKE_DIRECTORY "${_fault}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_file}/prim_file.erl")
file(READ "${_file}/prim_file.erl" _prim)
string(REPLACE "-nifs([" "-export([beam_test_io/1]).\n-nifs([beam_test_io/1," _prim "${_prim}")
string(APPEND _prim "\nbeam_test_io(_) -> erlang:nif_error(undefined).\n")
file(CONFIGURE OUTPUT "${_fault}/prim_file.erl" CONTENT "${_prim}" NEWLINE_STYLE UNIX @ONLY)
beam_add_erlang_component(beam_file_fault_modules SOURCES "${_fault}/prim_file.erl"
    INCLUDE_DIRECTORIES "${_file}" "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
    OUTPUT_DIRECTORY "${_fault}/ebin")
set_source_files_properties("${_file}/prim_buffer_nif.c" PROPERTIES COMPILE_DEFINITIONS "STATIC_ERLANG_NIF_LIBNAME=beam_prim_buffer")
set_source_files_properties("${_file}/prim_file_nif.c" PROPERTIES COMPILE_DEFINITIONS "STATIC_ERLANG_NIF_LIBNAME=beam_prim_file")
beam_add_library(beam_file_fault_component NAME file-contract-test TEST_ONLY
    MODULES beam_file prim_buffer prim_file
    BEAM_FILES "${CMAKE_BINARY_DIR}/components/file/ebin/beam_file.beam"
        "${CMAKE_BINARY_DIR}/components/file/ebin/prim_buffer.beam" "${_fault}/ebin/prim_file.beam"
    NIF_ENTRIES - beam_prim_buffer_nif_init beam_prim_file_nif_init
    NATIVE_SOURCES "${_file}/prim_buffer_nif.c" "${_file}/prim_file_nif.c" "${_backend}"
    DEPENDS beam_file_modules beam_file_fault_modules)
target_compile_definitions(beam_file_fault_component PRIVATE BEAM_FILE_CONTRACT_TEST)
target_include_directories(beam_file_fault_component PRIVATE "${_file}" "${_file}/include")
if(MSVC)
    target_compile_options(beam_file_fault_component PRIVATE /experimental:c11atomics)
endif()
