# Public manuals and the canonical standalone example, staged without source paths.
file(GLOB_RECURSE _manual_inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/docs/*")
set(_example_source "${CMAKE_SOURCE_DIR}/tests/installed/sdk/consumer/first-call")
set(_example_inputs)
foreach(_file IN ITEMS README.md CMakeLists.txt EmbedBytes.cmake answer.erl main.c)
    list(APPEND _example_inputs "${_example_source}/${_file}")
endforeach()
set(_package_manual_args)
if(BEAM_PACKAGE_SOURCE_DIR)
    get_filename_component(_manual_package "${BEAM_PACKAGE_SOURCE_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_SOURCE_DIR}")
    foreach(_file IN ITEMS package-format.md commands.md)
        list(APPEND _manual_inputs "${_manual_package}/docs/${_file}")
    endforeach()
    list(APPEND _package_manual_args --package "${_manual_package}")
endif()
set(_manual_output "${CMAKE_BINARY_DIR}/sdk-documentation")
add_custom_command(OUTPUT "${_manual_output}/.stamp"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/cmake/stage_documentation.py"
        --source "${CMAKE_SOURCE_DIR}" --output "${_manual_output}" ${_package_manual_args}
    DEPENDS ${_manual_inputs} ${_example_inputs}
        "${CMAKE_SOURCE_DIR}/cmake/stage_documentation.py"
    COMMENT "Staging installed SDK manuals and first-call example" VERBATIM)
add_custom_target(beam_sdk_documentation ALL DEPENDS "${_manual_output}/.stamp")
install(DIRECTORY "${_manual_output}/" DESTINATION share/doc/beam
    PATTERN .stamp EXCLUDE)
