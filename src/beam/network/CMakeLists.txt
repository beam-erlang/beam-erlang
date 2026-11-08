# Socket-only deployment. Legacy inet/net sources remain owned here but are not shipped.
set(BEAM_NETWORK_COMPONENT_MODULES beam_socket socket_registry socket beam_maps beam_lists beam_runtime_string_legacy prim_socket)
set(_out "${CMAKE_BINARY_DIR}/components/network/ebin")
beam_add_runtime_component(beam_socket_modules
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/beam_socket.erl"
        "${CMAKE_CURRENT_SOURCE_DIR}/socket.erl" "${CMAKE_CURRENT_SOURCE_DIR}/socket_registry.erl" "${CMAKE_CURRENT_SOURCE_DIR}/prim_socket.erl"
    INCLUDE_DIRECTORIES "${BEAM_SUPPORT_INCLUDE_DIRECTORY}"
    OUTPUT_DIRECTORY "${_out}")
set(BEAM_NETWORK_COMPONENT_BEAM_FILES
    "${_out}/beam_socket.beam" "${_out}/socket_registry.beam"
    "${_out}/socket.beam"
    "${BEAM_BASE_OUTPUT_DIRECTORY}/beam_maps.beam"
    "${BEAM_BASE_OUTPUT_DIRECTORY}/beam_lists.beam" "${BEAM_RUNTIME_SUPPORT_OUTPUT_DIRECTORY}/beam_runtime_string_legacy.beam" "${_out}/prim_socket.beam")
set_source_files_properties(prim_socket_nif.c PROPERTIES COMPILE_DEFINITIONS "STATIC_ERLANG_NIF_LIBNAME=beam_prim_socket")
if(WIN32)
    set(_socket_backend win32/win_socket_asyncio.c)
else()
    set(_socket_backend unix/unix_socket_syncio.c)
endif()
beam_add_library(beam_network_component NAME network RESOURCE_COUNTER beam_socket_count
    MODULES ${BEAM_NETWORK_COMPONENT_MODULES} BEAM_FILES ${BEAM_NETWORK_COMPONENT_BEAM_FILES}
    NIF_ENTRIES - - - - - - beam_prim_socket_nif_init
    NATIVE_SOURCES prim_socket_nif.c socket_dbg.c socket_tarray.c socket_util.c ${_socket_backend}
    DEPENDS beam_socket_modules beam_verify_base_artifacts beam_legacy_string_module)
target_include_directories(beam_network_component PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(beam_network_component PRIVATE beam_files_dyncall_headers)
if(WIN32)
    target_link_libraries(beam_network_component PRIVATE ws2_32)
    target_link_libraries(beam_network_component_static PUBLIC ws2_32)
endif()
foreach(_value IN ITEMS MODULES BEAM_FILES)
    set(BEAM_NETWORK_COMPONENT_${_value} "${BEAM_NETWORK_COMPONENT_${_value}}" PARENT_SCOPE)
endforeach()
