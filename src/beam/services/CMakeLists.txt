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

# The modern compiler and components build from attested BEAM compiler seeds.
set(_beam_host_beam "${CMAKE_BINARY_DIR}/lib/beam/ebin/beam_host.beam")

# One compiler output also supplies the explicitly loadable host bridge asset.
add_custom_command(OUTPUT "${_beam_host_beam}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/lib/beam/services/ebin/beam_host.beam" "${_beam_host_beam}"
    DEPENDS "${CMAKE_BINARY_DIR}/lib/beam/services/ebin/beam_host.beam" beam_services_modules
    COMMENT "Staging the optional host bridge" VERBATIM)
add_custom_target(beam_host_artifact DEPENDS "${_beam_host_beam}")

set_target_properties(beam_host_artifact PROPERTIES BEAM_HOST_ARTIFACT "${_beam_host_beam}")

beam_add_bytecode_inventory(beam_services_inventory NAME services
    MODULES ${BEAM_SERVICES_MODULES} INPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/beam/services/ebin"
    INSTALL_ROOT lib/beam/services DEPENDS beam_services_modules)
