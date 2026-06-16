# Ordinary activation support, separate from selecting native capabilities.
set(BEAM_SERVICES_MODULES beam_component_services beam_host beam_output beam_code)
set(BEAM_SERVICES_SOURCES)
set(BEAM_SERVICES_BEAM_FILES)
foreach(_module IN LISTS BEAM_SERVICES_MODULES)
    list(APPEND BEAM_SERVICES_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${_module}.erl")
    list(APPEND BEAM_SERVICES_BEAM_FILES "${CMAKE_BINARY_DIR}/lib/beam/services/ebin/${_module}.beam")
endforeach()
beam_add_erlang_component(beam_services_modules SOURCES ${BEAM_SERVICES_SOURCES}
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/services/ebin")
foreach(_value IN ITEMS MODULES SOURCES BEAM_FILES)
    set(BEAM_SERVICES_${_value} "${BEAM_SERVICES_${_value}}" PARENT_SCOPE)
endforeach()
set(BEAM_HOST_BEAM "${CMAKE_BINARY_DIR}/lib/beam/services/ebin/beam_host.beam" PARENT_SCOPE)
