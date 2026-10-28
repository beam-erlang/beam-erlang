# Ordinary archive support has a public zlib-module dependency, separate from
# the VM's native compression backend. File-backed input additionally uses files.
set(BEAM_ARCHIVE_MODULES prim_zip)
set(BEAM_ARCHIVE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/prim_zip.erl")
set(BEAM_ARCHIVE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/zip_internal.hrl")
set(BEAM_ARCHIVE_BEAM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/ebin/prim_zip.beam")
set(BEAM_ARCHIVE_REQUIRED_MODULES zlib)
set(BEAM_ARCHIVE_FILE_REQUIRED_MODULES prim_file)
set(BEAM_PRIM_ZIP_BEAM "${BEAM_ARCHIVE_BEAM_FILES}" PARENT_SCOPE)
foreach(_value IN ITEMS MODULES SOURCES HEADERS BEAM_FILES REQUIRED_MODULES FILE_REQUIRED_MODULES)
    set(BEAM_ARCHIVE_${_value} "${BEAM_ARCHIVE_${_value}}" PARENT_SCOPE)
endforeach()

set(_ordinary "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin/prim_zip.beam")
add_custom_command(OUTPUT "${_ordinary}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib/beam/runtime/beam/ebin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ebin/prim_zip.beam" "${_ordinary}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ebin/prim_zip.beam" VERBATIM)
add_custom_target(beam_archive_artifacts DEPENDS "${_ordinary}")
set_target_properties(beam_archive_artifacts PROPERTIES
    BEAM_INVENTORY_MODULES prim_zip BEAM_INVENTORY_FILES "${_ordinary}")
