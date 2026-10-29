set(BEAM_FILE_COMPONENT_MODULES beam_file prim_buffer prim_file)
set(BEAM_FILE_COMPONENT_BEAM_FILES)
set(_sources)
foreach(_module IN LISTS BEAM_FILE_COMPONENT_MODULES)
    list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
    list(APPEND BEAM_FILE_COMPONENT_BEAM_FILES "${CMAKE_BINARY_DIR}/components/file/ebin/${_module}.beam")
endforeach()
beam_add_erlang_component(beam_file_modules SOURCES ${_sources}
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/components/file/ebin")
set_source_files_properties(prim_buffer_nif.c PROPERTIES COMPILE_DEFINITIONS "STATIC_ERLANG_NIF_LIBNAME=beam_prim_buffer")
set_source_files_properties(prim_file_nif.c PROPERTIES COMPILE_DEFINITIONS "STATIC_ERLANG_NIF_LIBNAME=beam_prim_file")
if(WIN32)
    set(_file_backend win32/win_prim_file.c)
else()
    set(_file_backend unix/unix_prim_file.c)
endif()
beam_add_library(beam_file_component NAME file
    MODULES ${BEAM_FILE_COMPONENT_MODULES} BEAM_FILES ${BEAM_FILE_COMPONENT_BEAM_FILES}
    NIF_ENTRIES - beam_prim_buffer_nif_init beam_prim_file_nif_init
    NATIVE_SOURCES prim_buffer_nif.c prim_file_nif.c ${_file_backend}
    DEPENDS beam_file_modules)
target_include_directories(beam_file_component PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
# The socket sendfile implementation consumes this interface without linking files.
add_library(beam_files_dyncall_headers INTERFACE)
target_include_directories(beam_files_dyncall_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
foreach(_value IN ITEMS MODULES BEAM_FILES)
    set(BEAM_FILE_COMPONENT_${_value} "${BEAM_FILE_COMPONENT_${_value}}" PARENT_SCOPE)
endforeach()
