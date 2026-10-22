# Full product identity: <upstream ERTS>-r<revision>[-preview.<number>].
get_filename_component(_beam_version_file "${CMAKE_CURRENT_LIST_DIR}/../BEAM_VERSION" ABSOLUTE)
file(STRINGS "${_beam_version_file}" BEAM_RELEASE_VERSION)
if(NOT BEAM_RELEASE_VERSION MATCHES "^([0-9]+(\\.[0-9]+)+)-r([1-9][0-9]*)(-preview\\.([1-9][0-9]*))?$")
    message(FATAL_ERROR
        "BEAM_VERSION must contain <ERTS>-r<revision>[-preview.<number>]")
endif()
set(BEAM_ERTS_VERSION "${CMAKE_MATCH_1}")
set(BEAM_DISTRIBUTION_REVISION "${CMAKE_MATCH_3}")
set(BEAM_PREVIEW_NUMBER "${CMAKE_MATCH_5}")
set(BEAM_RELEASE_CHANNEL "stable")
if(NOT BEAM_PREVIEW_NUMBER STREQUAL "")
    set(BEAM_RELEASE_CHANNEL "preview")
endif()

# Release counters travel as JSON numbers and must round-trip through JavaScript.
foreach(_field IN ITEMS BEAM_DISTRIBUTION_REVISION BEAM_PREVIEW_NUMBER)
    string(LENGTH "${${_field}}" _counter_length)
    if(_counter_length GREATER 16 OR
            (_counter_length EQUAL 16 AND "${${_field}}" STRGREATER "9007199254740991"))
        message(FATAL_ERROR "BEAM_VERSION release counters must not exceed 9007199254740991")
    endif()
endforeach()

string(REPLACE "." ";" _beam_erts_parts "${BEAM_ERTS_VERSION}")
foreach(_part IN LISTS _beam_erts_parts)
    if(NOT _part MATCHES "^(0|[1-9][0-9]*)$")
        message(FATAL_ERROR "BEAM_VERSION ERTS components must not have leading zeros")
    endif()
endforeach()

# Upstream maintenance versions may have more components than CMake accepts.
# Numeric discovery is only a projection; SDK consumers check the full identity.
list(GET _beam_erts_parts 0 _beam_erts_major)
list(GET _beam_erts_parts 1 _beam_erts_minor)
list(LENGTH _beam_erts_parts _beam_erts_count)
set(_beam_erts_patch 0)
if(_beam_erts_count GREATER 2)
    list(GET _beam_erts_parts 2 _beam_erts_patch)
endif()
set(BEAM_PROJECT_VERSION "${_beam_erts_major}.${_beam_erts_minor}.${_beam_erts_patch}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_beam_version_file}")
