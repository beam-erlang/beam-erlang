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
